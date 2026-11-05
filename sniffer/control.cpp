// Heater control for the Vevor "SUN" (V1) diesel heaters: heater model, the worker task that owns the radio,
// the DHT22, the event log and the command intake. The cloud client (cloud.cpp) reads the state and feeds the commands.
// Protocol notes: docs/protocol.md.
#include "control.h"
#include <Preferences.h>
#include <esp_timer.h>
#include "smart.h"
#include "textutil.h"

SemaphoreHandle_t radioMux = nullptr;

// The server reads the version of an uploaded image from this text (see FirmwareImage.php).
#define FW_STR_(x) #x
#define FW_STR(x) FW_STR_(x)
extern const char FW_MARK[] = "HINASTO-FW:" FW_STR(FW_VERSION);
// ... and the language of the build from this one: a controller only updates to an image of its own language.
extern const char FW_LANG_MARK[] = "HINASTO-LANG:" HN_LANG_CODE;
enum Run : uint8_t { RUN_UNKNOWN, RUN_OFF, RUN_STARTING, RUN_ON, RUN_COOLING };
enum Act : uint8_t { A_ON, A_OFF, A_LEVEL, A_TEMP, A_REFRESH, A_MODE, A_SNIFF };
static const char* ACT_NAME[] = {L_ACT_ON, L_ACT_OFF, L_ACT_LEVEL, L_ACT_TEMP, L_ACT_READ, L_ACT_MODE, L_ACT_LISTEN};

struct Heater {
  const char* name;
  uint8_t addr[3];
  bool enabled;
  Run run;
  bool manual;
  int level, setpoint, chamber, volts;
  int rssi, ownTemp;          // dBm of the last reply; the heater's own temperature (screen 0x80 of the second model)
  uint8_t lastPage, lastSub;  // last plausible reply
  int lastN;
  uint32_t seen, chamberAt, voltsAt, errAt;
  const char* busy;
  const char* err;
  uint32_t pollOk, pollFail, failRun, lastKey;
  char prefix[16];  // "<name>: " in logs when two heaters are configured, empty otherwise
  uint8_t hdr;      // second frame byte of this remote (0x15 first heater, 0x12 second)
};

struct Job {
  uint8_t heater;
  Act act;
  int val;
  int cmdId;  // id of the server command this job answers, 0 for none
};

static QueueHandle_t ackQ;  // finished commands waiting to be reported to the server

static Heater heaters[2];
static QueueHandle_t jobQ;
static volatile bool abortReq = false;  // a power off request interrupts a long job
static const uint32_t POLL_MS = 700;                // the original remote polls this often
static volatile uint32_t infoEveryMs = 20000;       // chamber temperature + voltage live on other info pages
static uint32_t nextInfoAt[2] = {5000, 5000};
static volatile bool otaBusy = false;               // an OTA update is running, leave the radio alone

// millis() wraps after 49.7 days: deadlines are compared as (int32_t)(now - deadline), never with < or >=
static inline bool reached(uint32_t now, uint32_t deadline) { return (int32_t)(now - deadline) >= 0; }
// seconds since boot from a 64-bit clock: smart mode keeps its own times in them and never sees a wrap
static inline uint32_t secs() { return (uint32_t)(esp_timer_get_time() / 1000000LL); }

// ---- event log shown on the web page (ring buffer, newest wins) ----

struct LogLine {
  uint32_t id, ms;
  char text[80];
};
static const int LOG_N = 28;  // DRAM is tight (the image no longer links with 32 lines, and TLS needs the heap)
static LogLine logRing[LOG_N];
static uint32_t logNext = 1;
static SemaphoreHandle_t logMux;
static volatile bool logVerbose = false;  // also log every raw frame sent and received

void wlog(const char* fmt, ...) {
  char buf[80];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  if (xSemaphoreTake(logMux, pdMS_TO_TICKS(50)) != pdTRUE) return;
  LogLine& l = logRing[logNext % LOG_N];
  l.id = logNext++;
  l.ms = millis();
  strlcpy(l.text, buf, sizeof l.text);
  xSemaphoreGive(logMux);
}

static void hexOf(char* out, size_t n, const uint8_t* b, int len) {
  out[0] = 0;
  for (int i = 0; i < len && (size_t)(i * 3 + 3) < n; i++) sprintf(out + i * 3, "%02X ", b[i]);
}

void jsonEscape(String& out, const char* s) {
  while (*s) {
    uint8_t c = (uint8_t)*s;
    if (c == '"' || c == '\\') { out += '\\'; out += (char)c; s++; }
    else if (c < 0x20) { out += ' '; s++; }
    else if (c < 0x80) { out += (char)c; s++; }
    else {
      int n = (c >= 0xC2 && c <= 0xDF) ? 2 : (c >= 0xE0 && c <= 0xEF) ? 3 : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
      bool ok = n > 0;
      for (int i = 1; ok && i < n; i++) if (((uint8_t)s[i] & 0xC0) != 0x80) ok = false;  // also stops at the end of the text
      if (ok) { for (int i = 0; i < n; i++) out += s[i]; s += n; }
      else { out += '?'; s++; }
    }
  }
}

// ---- decoding (see docs/protocol.md): n = d1*2 + (d2>>7); level n=1..6, setpoint C = n-64, chamber C = n, volts = n ----

static bool plausible(uint8_t page, uint8_t sub, uint8_t d2) {
  if ((page & 0xF0) != 0xB0 && page != 0x80) return false;
  uint8_t lo = d2 & 0x7F;  // bit 7 is part of the value, the low bits flicker (seen 0x15 0x17 0x18 0x19 on live frames)
  if (lo != 0x00 && (lo >> 4) != 1) return false;  // rejects the 0x5555 style glitches
  sub &= 0xE0;  // only the top 3 bits are the state; the low bits are flags/stage (0x10 idle on heater 2, 0x2A while running there)
  return sub == 0x00 || sub == 0x20 || sub == 0x60 || sub == 0xA0;
}

static bool applyReply(Heater& h, const uint8_t* b) {
  uint8_t page = b[5], sub = b[6];
  if (!plausible(page, sub, b[8])) return false;
  sub &= 0xE0;
  int n = b[7] * 2 + (b[8] >> 7);
  uint32_t now = millis();
  h.seen = now;
  h.lastPage = page;
  h.lastSub = sub;
  h.lastN = n;
  if (page == 0xB1) {
    switch (sub) {
      case 0x00: h.run = RUN_OFF; h.volts = n; h.voltsAt = now; break;
      case 0x30: h.run = RUN_STARTING; break;
      case 0x60: h.run = RUN_COOLING; break;
      case 0x20:
        h.run = RUN_ON;
        if (n >= 1 && n <= 6) { h.manual = true; h.level = n; }
        else if (n >= 65 && n <= 110) { h.manual = false; h.setpoint = n - 64; }
        break;
      case 0xA0: h.run = RUN_ON; h.chamber = n; h.chamberAt = now; break;
    }
  } else if (page == 0x80) {  // second heater model: an extra screen with its own room temperature
    switch (sub) {
      case 0x00: h.run = RUN_OFF; break;
      case 0x20: h.run = RUN_ON; break;
      case 0x60: h.run = RUN_COOLING; break;
    }
    h.ownTemp = n;
  } else if (page == 0xB2 && sub == 0x20) {
    h.run = RUN_ON;
    h.volts = n;
    h.voltsAt = now;
  }
  return true;
}

// ---- radio operations; all of them run with radioMux held ----

static void radioReady() {
  if (!v1Active) configureHeaterV1();
}

// human readable meaning of a reply, same decoding as applyReply
static void describe(char* out, size_t n, uint8_t page, uint8_t sub, int v) {
  if (page == 0xB1 && sub == 0x00) snprintf(out, n, L_D_OFF, v);
  else if (page == 0xB1 && sub == 0x30) snprintf(out, n, L_D_STARTING);
  else if (page == 0xB1 && sub == 0x60) snprintf(out, n, L_D_COOLING);
  else if (page == 0xB1 && sub == 0x20 && v >= 1 && v <= 6) snprintf(out, n, L_D_MANUAL, v);
  else if (page == 0xB1 && sub == 0x20 && v >= 65 && v <= 110) snprintf(out, n, L_D_SETPOINT, v - 64);
  else if (page == 0xB1 && sub == 0xA0) snprintf(out, n, L_D_PAGE_CHAMBER, v);
  else if (page == 0xB2 && sub == 0x20) snprintf(out, n, L_D_PAGE_VOLT, v);
  else if (page == 0x80) snprintf(out, n, L_D_PAGE_HEATER, v);
  else snprintf(out, n, L_D_UNKNOWN, page, sub, v);
}

// ---- odd frames: a reply of the heater whose value is out of the usual range, or that the plausibility filter refuses ----
// (an error state such as E-8 may be sent in a form nobody has decoded yet). Each distinct frame is logged once as "ANOM ..."
// and the last ANOM_N are kept in flash, because the log ring is overwritten within minutes. Console: "anom", "anom clear".

static const int ANOM_N = 8;
static const char* const ANOM_WHY[] = {"", "off-n", "volt-n", "level-n", "chamber-n", "refused"};
struct AnomEntry {
  uint32_t t;         // wall clock when known, else 0
  uint8_t frame[4];   // page, sub, d1, d2 of the reply
  uint8_t why, heater;
};
struct AnomStore {
  uint8_t n;
  AnomEntry e[ANOM_N];  // newest first
};
static uint32_t anomSeen[ANOM_N];  // keys already handled since boot, so a repeating frame costs nothing
static uint8_t anomSeenAt = 0;

static uint8_t anomalyWhy(uint8_t page, uint8_t sub, int n) {
  sub &= 0xE0;
  if (page == 0xB1 && sub == 0x00 && (n < 6 || n > 40)) return 1;  // the off page carries the supply voltage, 12 V normally
  if (page == 0xB2 && sub == 0x20 && (n < 6 || n > 40)) return 2;
  if (page == 0xB1 && sub == 0x20 && !((n >= 1 && n <= 6) || (n >= 65 && n <= 110))) return 3;
  if (page == 0xB1 && sub == 0xA0 && n > 400) return 4;
  return 0;
}

static void noteAnomaly(const Heater& h, const uint8_t* b, uint8_t why) {
  uint8_t hi = (uint8_t)(&h - heaters);
  uint32_t key = ((uint32_t)b[5] << 24) | ((uint32_t)b[6] << 16) | ((uint32_t)b[7] << 8) | (b[8] & 0x80) | hi;  // d2 low bits flicker
  for (int i = 0; i < ANOM_N; i++) if (anomSeen[i] == key) return;
  anomSeen[anomSeenAt++ % ANOM_N] = key;
  wlog("ANOM %s%s %02X %02X %02X %02X", h.prefix, ANOM_WHY[why], b[5], b[6], b[7], b[8]);
  AnomStore st = {};
  Preferences p;
  if (!p.begin("hnanom", false)) return;
  if (p.getBytesLength("st") == sizeof st) p.getBytes("st", &st, sizeof st);
  for (int i = 0; i < st.n; i++)
    if (!memcmp(st.e[i].frame, b + 5, 4) && st.e[i].heater == hi) { p.end(); return; }  // stored in an earlier run
  if (st.n < ANOM_N) st.n++;
  memmove(&st.e[1], &st.e[0], sizeof(AnomEntry) * (ANOM_N - 1));
  time_t now = time(nullptr);
  st.e[0].t = now > 1700000000 ? (uint32_t)now : 0;
  memcpy(st.e[0].frame, b + 5, 4);
  st.e[0].why = why;
  st.e[0].heater = hi;
  p.putBytes("st", &st, sizeof st);
  p.end();
}

void controlAnomDump() {
  AnomStore st = {};
  Preferences p;
  if (p.begin("hnanom", true)) {
    if (p.getBytesLength("st") == sizeof st) p.getBytes("st", &st, sizeof st);
    p.end();
  }
  Serial.printf("%d odd frame(s), newest first\n", st.n);
  for (int i = 0; i < st.n && i < ANOM_N; i++) {
    const AnomEntry& e = st.e[i];
    Serial.printf("  heater%d %s %02X %02X %02X %02X  n=%d  t=%lu\n", e.heater + 1, ANOM_WHY[e.why < 6 ? e.why : 0], e.frame[0], e.frame[1],
                  e.frame[2], e.frame[3], e.frame[2] * 2 + (e.frame[3] >> 7), (unsigned long)e.t);
  }
}

void controlAnomClear() {
  Preferences p;
  if (p.begin("hnanom", false)) { p.clear(); p.end(); }
  memset(anomSeen, 0, sizeof anomSeen);
  Serial.println("ok");
}

static bool pollOnce(Heater& h) {
  memcpy(v1Addr, h.addr, 3);
  v1Hdr = h.hdr;
  uint8_t b[12];
  heaterTxV1(0xA9, 0x00, 0x00, 0x00, false);  // the same poll the remote sends
  if (logVerbose) wlog(L_TX_POLL, h.addr[0], h.addr[1], h.addr[2]);
  if (v1ReadReply(350, b) < 0) {
    h.pollFail++;
    h.failRun++;
    if (logVerbose) wlog(L_RX_NONE);
    else if (h.failRun == 1 || h.failRun % 20 == 0) wlog(L_NO_REPLY_RUN, h.prefix, (unsigned)h.failRun);
    return false;
  }
  char hex[48];
  hexOf(hex, sizeof hex, b, 12);
  if (logVerbose) wlog("RX %s", hex);
  h.rssi = (int)((int8_t)b[10]) / 2 - 74;
  if (h.failRun) { wlog(L_REPLY_BACK, h.prefix, (unsigned)h.failRun, h.rssi); h.failRun = 0; }
  if (!applyReply(h, b)) {
    wlog(L_FRAME_BAD, hex);
    if ((b[5] & 0xF0) == 0xB0) noteAnomaly(h, b, 5);  // looks like a page of the heater but the filter refuses it
    return false;
  }
  h.pollOk++;
  if (uint8_t why = anomalyWhy(b[5], b[6], b[7] * 2 + (b[8] >> 7))) noteAnomaly(h, b, why);
  uint32_t key = ((uint32_t)b[5] << 24) | ((uint32_t)b[6] << 16) | (uint32_t)(b[7] * 2 + (b[8] >> 7));
  if (key != h.lastKey) {
    h.lastKey = key;
    char d[64];
    describe(d, sizeof d, b[5], b[6] & 0xE0, b[7] * 2 + (b[8] >> 7));
    wlog("%s%s", h.prefix, d);
  }
  return true;
}

template <typename F>
static bool waitFor(Heater& h, uint32_t ms, F cond) {
  uint32_t t0 = millis();
  do {
    if (abortReq) return false;
    if (pollOnce(h) && cond(h)) return true;
    delay(200);
  } while (millis() - t0 < ms);
  return false;
}

static void press(Heater& h, uint8_t sub, uint8_t d1) {
  memcpy(v1Addr, h.addr, 3);
  v1Hdr = h.hdr;
  heaterTxV1(0xAB, sub, d1, 0x00, false);
  const char* what = sub == 0xA9 ? (d1 ? L_BTN_DOWN : L_BTN_UP)
                   : sub == 0xAA ? (d1 ? L_BTN_NEXT : L_BTN_MODE)
                   : sub == 0xAD ? L_BTN_ON : sub == 0xAE ? L_BTN_OFF : "?";
  wlog(L_TX_BUTTON, what, sub, d1);
}

static bool anyReply(Heater& h, uint32_t ms) {
  return waitFor(h, ms, [](const Heater&) { return true; });
}

// short OK cycles the info pages: level/setpoint -> chamber -> voltage -> level/setpoint
static bool toMainPage(Heater& h) {
  for (int i = 0; i < 6; i++) {  // the cycle has 3 screens on one model, 4 on the other
    if (abortReq) return false;  // a power off is waiting
    if (!anyReply(h, 3000)) return false;
    if (h.lastPage == 0xB1 && (h.lastSub == 0x20 || h.lastSub == 0x00 || h.lastSub == 0x30 || h.lastSub == 0x60)) return true;
    press(h, 0xAA, 0x80);
    delay(1500);
  }
  return false;
}

static int readMain(Heater& h) {
  if (!toMainPage(h)) return -1;
  if (!(h.lastPage == 0xB1 && h.lastSub == 0x20)) return -1;
  return h.lastN;
}

// one UP/DOWN press on the main page; the heater answers with the old value first, so wait for a change
static int stepOnce(Heater& h, bool up, int prevN) {
  press(h, 0xA9, up ? 0x00 : 0x80);
  delay(700);
  uint32_t t0 = millis();
  while (millis() - t0 < 5000) {
    if (abortReq) return -1;
    if (pollOnce(h) && h.lastPage == 0xB1 && h.lastSub == 0x20 && h.lastN != prevN) return h.lastN;
    delay(250);
  }
  return -1;
}

// The radio link has no CRC, so one lost frame must not lose the power off: press up to three times. "Cooling" confirms
// it, and so does "off" (the heater was already off).
static const char* jobOff(Heater& h) {
  for (int attempt = 0; attempt < 3; attempt++) {
    abortReq = false;  // a second off request must not cancel the one that is running
    press(h, 0xAE, 0x00);
    delay(600);
    if (waitFor(h, 5000, [](const Heater& x) { return x.lastPage == 0xB1 && (x.lastSub == 0x60 || x.lastSub == 0x00); })) return nullptr;
  }
  return L_E_OFF_NOCONF;
}

static const char* jobOn(Heater& h) {
  if (!anyReply(h, 4000)) return L_E_NO_REPLY;
  if (!(h.lastPage == 0xB1 && h.lastSub == 0x00)) return L_E_NOT_OFF;
  press(h, 0xAD, 0x80);
  delay(1200);
  return waitFor(h, 20000, [](const Heater& x) { return x.lastPage == 0xB1 && (x.lastSub == 0x20 || x.lastSub == 0x30); }) ? nullptr : L_E_ON_NOCONF;
}

// the long OK toggles manual <-> setpoint mode; from setpoint it always lands in manual level 1
static bool toggleMode(Heater& h, bool wantManual, int* n) {
  press(h, 0xAA, 0x00);
  delay(1200);
  uint32_t t0 = millis();
  while (millis() - t0 < 6000) {
    if (abortReq) return false;
    if (pollOnce(h) && h.lastPage == 0xB1 && h.lastSub == 0x20) {
      bool manual = h.lastN >= 1 && h.lastN <= 6;
      bool setpoint = h.lastN >= 65 && h.lastN <= 110;
      if ((wantManual && manual) || (!wantManual && setpoint)) { *n = h.lastN; return true; }
    }
    delay(250);
  }
  return false;
}

static const char* walkTo(Heater& h, int n, int target, int maxSteps) {
  for (int i = 0; i < maxSteps && n != target; i++) {
    if (abortReq) return L_E_ABORTED;
    int want = n < target ? n + 1 : n - 1;
    int got = stepOnce(h, n < target, n);
    wlog(L_STEP, want, got);
    if (got != want) return L_E_STUCK;
    n = got;
  }
  return n == target ? nullptr : L_E_NOT_REACHED;
}

// manual and auto are never mixed: level only in manual mode, temperature only in auto mode,
// switching between them is its own job
static const char* jobLevel(Heater& h, int level) {
  int n = readMain(h);
  if (n < 0) return L_E_SILENT;
  if (n < 1 || n > 6) return L_E_IS_AUTO;
  return walkTo(h, n, level, 8);
}

static const char* jobTemp(Heater& h, int celsius) {
  int n = readMain(h);
  if (n < 0) return L_E_SILENT;
  if (n < 65 || n > 110) return L_E_IS_MANUAL;
  return walkTo(h, n, 64 + celsius, 40);
}

static const char* jobMode(Heater& h, bool wantManual) {
  int n = readMain(h);
  if (n < 0) return L_E_SILENT;
  bool isManual = n >= 1 && n <= 6;
  if (!isManual && !(n >= 65 && n <= 110)) return L_E_UNEXPECTED;
  if (isManual == wantManual) return nullptr;  // already there
  return toggleMode(h, wantManual, &n) ? nullptr : L_E_MODE_FAIL;
}

// like waitFor, but gives up as soon as a command is queued so a user action never waits for a page walk
template <typename F>
static bool waitForYielding(Heater& h, uint32_t ms, F cond) {
  uint32_t t0 = millis();
  do {
    if (abortReq || uxQueueMessagesWaiting(jobQ) > 0) return false;
    if (pollOnce(h) && cond(h)) return true;
    delay(200);
  } while (millis() - t0 < ms);
  return false;
}

static const char* jobRefresh(Heater& h) {
  if (readMain(h) < 0) return L_E_SILENT;
  press(h, 0xAA, 0x80);
  delay(1200);
  bool ok = waitForYielding(h, 6000, [](const Heater& x) { return x.lastPage == 0xB1 && x.lastSub == 0xA0; });
  if (ok) {
    press(h, 0xAA, 0x80);
    delay(1200);
    ok = waitForYielding(h, 6000, [](const Heater& x) { return x.lastPage == 0xB2; });
  }
  toMainPage(h);  // back to level/setpoint whatever happened
  if (!ok && uxQueueMessagesWaiting(jobQ) > 0) return nullptr;  // interrupted by a command, not a failure
  return ok ? nullptr : L_E_INFO_FAIL;
}

// Listen to every V1 frame in range, whatever its address, and log what it hears: used to find the address
// of another remote (wake it near the board). Logs a frame only when its content changes.
static const char* jobSniff(int seconds) {
  struct Seen { uint8_t a[3]; uint8_t hdr1; uint16_t n; uint32_t key; };
  Seen seen[6];
  int ns = 0;
  uint16_t junk = 0;
  wlog(L_LISTEN_START, seconds);
  uint32_t t0 = millis();
  while (millis() - t0 < (uint32_t)seconds * 1000UL) {
    if (abortReq) break;
    uint8_t b[12];
    if (!v1RawFrame(b, 500)) continue;
    if (b[0] != 0x2B) { junk++; continue; }
    int k = -1;
    for (int i = 0; i < ns; i++) if (!memcmp(seen[i].a, b + 2, 3)) k = i;
    if (k < 0) {
      if (ns >= 6) { junk++; continue; }
      k = ns++;
      memcpy(seen[k].a, b + 2, 3);
      seen[k].hdr1 = b[1];
      seen[k].n = 0;
      seen[k].key = 0xFFFFFFFF;
      wlog(L_LISTEN_NEW, b[2], b[3], b[4], b[1]);
    }
    seen[k].n++;
    uint32_t key = ((uint32_t)b[5] << 24) | ((uint32_t)b[6] << 16) | ((uint32_t)b[7] << 8) | (b[8] & 0x80);
    if (key != seen[k].key) {
      seen[k].key = key;
      wlog(L_LISTEN_FRAME, b[2], b[3], b[4], b[5], b[6], b[7], b[8], (int)((int8_t)b[10]) / 2 - 74);
    }
  }
  for (int i = 0; i < ns; i++) wlog(L_LISTEN_ADDR, seen[i].a[0], seen[i].a[1], seen[i].a[2], (unsigned)seen[i].n);
  if (!ns) wlog(L_LISTEN_NONE, (unsigned)junk);
  return nullptr;
}

static void runJob(const Job& j) {
  Heater& h = heaters[j.heater];
  xSemaphoreTake(radioMux, portMAX_DELAY);
  radioReady();
  if (j.act == A_OFF) abortReq = false;  // the off request itself must run
  h.busy = ACT_NAME[j.act];
  h.err = nullptr;
  wlog(L_JOB_START, h.prefix, ACT_NAME[j.act], j.val);
  uint32_t t0 = millis();
  const char* e = nullptr;
  switch (j.act) {
    case A_ON: e = jobOn(h); break;
    case A_OFF: e = jobOff(h); break;
    case A_LEVEL: e = jobLevel(h, j.val); break;
    case A_TEMP: e = jobTemp(h, j.val); break;
    case A_REFRESH: e = jobRefresh(h); break;
    case A_MODE: e = jobMode(h, j.val == 1); break;
    case A_SNIFF: e = jobSniff(j.val); break;
  }
  h.busy = nullptr;
  if (j.cmdId) {
    Ack a;
    a.id = j.cmdId;
    a.ok = e == nullptr;
    utf8Copy(a.msg, e ? e : "", sizeof a.msg);
    xQueueSend(ackQ, &a, 0);
  }
  if (e) { h.err = e; h.errAt = millis(); wlog(L_JOB_ERR, (unsigned long)(millis() - t0), e); }
  else {
    wlog(L_JOB_OK, (unsigned long)(millis() - t0));
    if (j.act != A_OFF) nextInfoAt[j.heater] = millis() + 2500;  // fresh chamber/voltage right after a change
  }
  xSemaphoreGive(radioMux);
}

// ---- room sensor: DHT22 breakout, powered from a GPIO so it can be power cycled when it hangs ----
static const int DHT_PWR_PIN = 6, DHT_DATA_PIN = 4;
static const uint32_t DHT_EVERY_MS = 5000;  // the sensor needs at least 2 s between reads
static float envT = 0, envH = 0;
static uint32_t envAt = 0, envNext = 0, envPowerAt = 0;
static uint8_t envFails = 0;
static bool envOk = false, envFailLogged = false;
static portMUX_TYPE dhtMux = portMUX_INITIALIZER_UNLOCKED;

// microseconds the line stays at `level`, or -1 after maxUs
static int32_t dhtWait(int level, uint32_t maxUs) {
  uint32_t t0 = micros();
  while (digitalRead(DHT_DATA_PIN) == level) {
    if (micros() - t0 > maxUs) return -1;
  }
  return (int32_t)(micros() - t0);
}

static bool dhtRead(float* t, float* h) {
  uint8_t d[5] = {0, 0, 0, 0, 0};
  pinMode(DHT_DATA_PIN, OUTPUT);
  digitalWrite(DHT_DATA_PIN, LOW);
  delay(2);  // start pulse, at least 1 ms
  bool ok = true;
  portENTER_CRITICAL(&dhtMux);  // about 5 ms of exact timing
  pinMode(DHT_DATA_PIN, INPUT_PULLUP);
  if (dhtWait(HIGH, 100) < 0 || dhtWait(LOW, 120) < 0 || dhtWait(HIGH, 120) < 0) ok = false;  // sensor answers: 80 us low, 80 us high
  for (int i = 0; i < 40 && ok; i++) {
    if (dhtWait(LOW, 100) < 0) { ok = false; break; }  // 50 us low
    int32_t hi = dhtWait(HIGH, 150);                    // 26 us = 0, 70 us = 1
    if (hi < 0) { ok = false; break; }
    d[i / 8] = (d[i / 8] << 1) | (hi > 45 ? 1 : 0);
  }
  portEXIT_CRITICAL(&dhtMux);
  if (!ok || (uint8_t)(d[0] + d[1] + d[2] + d[3]) != d[4]) return false;
  if (!(d[0] | d[1] | d[2] | d[3])) return false;  // an all-zero frame has a valid checksum and is a dead line, not 0.0 C / 0 %
  *h = ((d[0] << 8) | d[1]) / 10.0f;
  float temp = (((d[2] & 0x7F) << 8) | d[3]) / 10.0f;
  *t = (d[2] & 0x80) ? -temp : temp;
  return true;
}

static void dhtTick() {
  uint32_t now = millis();
  if (!reached(now, envNext)) return;
  envNext = now + DHT_EVERY_MS;
  if (now - envPowerAt < 1500) return;  // still starting after power up
  float t, h;
  if (dhtRead(&t, &h) && t > -40 && t < 80 && h >= 0 && h <= 100) {
    envT = t;
    envH = h;
    envAt = now;
    envFails = 0;
    if (!envOk) wlog(L_ENV_OK, t, h);
    envOk = true;
  } else if (++envFails >= 5) {  // about 25 s without a good reading
    envFails = 0;
    if (envOk || !envFailLogged) wlog(L_ENV_FAIL);
    envOk = false;
    envFailLogged = true;
    digitalWrite(DHT_PWR_PIN, LOW);
    delay(500);
    digitalWrite(DHT_PWR_PIN, HIGH);
    envPowerAt = millis();
  }
}

// ---- smart mode: keeps the room at the target with the DHT22 and the manual levels (logic in smart.h) ----
static Smart smart;
static SemaphoreHandle_t smartMux;  // the sync task (commands, state) and the worker task both touch `smart`

// What survives a restart or an update: on/off and the target, and what smart believes about the heater (it started it
// and it should be running; how many starts it has tried), so a heater stopped by hand while the controller restarted,
// or by a power failure, is recognised as stopped by someone else and smart switches itself off instead of starting it again.
static void smartSave() {
  Preferences p;
  if (!p.begin("hnsmart", false)) return;
  p.putBool("on", smart.enabled);
  p.putFloat("t", smart.target);
  p.putBool("won", smart.wasOn());
  p.putUChar("tries", (uint8_t)smart.tries());
  p.end();
}

static void smartStop(const char* why) {
  xSemaphoreTake(smartMux, portMAX_DELAY);
  bool was = smart.enabled;
  smart.disable();
  xSemaphoreGive(smartMux);
  if (!was) return;
  smartSave();
  wlog(L_SMART_OFF, why);
}

static void smartTick() {
  static uint32_t next = 0;
  static bool savedWasOn = false;
  static int savedTries = 0;
  uint32_t now = millis();
  if (!reached(now, next)) return;
  next = now + 5000;
  Heater& h = heaters[0];
  if (!smart.enabled || !h.enabled) return;
  SmartIn in;
  in.now = secs();
  in.sensorOk = envAt && now - envAt < 120000;
  in.temp = envT;
  bool fresh = h.seen && now - h.seen < 20000;
  in.run = !fresh || h.run == RUN_UNKNOWN ? SR_UNKNOWN : h.run == RUN_OFF ? SR_OFF : h.run == RUN_COOLING ? SR_COOLING : SR_ON;
  in.manual = h.manual;
  in.level = h.level;
  in.busy = h.busy || uxQueueMessagesWaiting(jobQ) > 0;
  xSemaphoreTake(smartMux, portMAX_DELAY);
  SmartOut o = smart.step(in);
  bool wasOn = smart.wasOn();
  int tries = smart.tries();
  xSemaphoreGive(smartMux);
  if (wasOn != savedWasOn || tries != savedTries) {  // a few writes a day, only on a change
    savedWasOn = wasOn;
    savedTries = tries;
    smartSave();
  }
  if (o.act == SA_NONE) return;
  if (o.act == SA_DISABLE) { smartSave(); wlog(L_SMART_OFF, o.why); return; }
  Job j = {0, A_ON, 0, 0};
  switch (o.act) {
    case SA_START: j.act = A_ON; wlog(L_SMART_START, envT, (int)lroundf(smart.target)); break;
    case SA_MANUAL: j.act = A_MODE; j.val = 1; wlog(L_SMART_SAY, o.why); break;
    case SA_LEVEL: j.act = A_LEVEL; j.val = o.level; wlog(L_SMART_LEVEL, o.level, o.why); break;
    default: j.act = A_OFF; wlog(L_SMART_STOP, o.why); break;
  }
  if (xQueueSend(jobQ, &j, 0) == pdTRUE && !h.busy) h.busy = ACT_NAME[j.act];
}

// Background routine: poll at the remote's rate; while the heater runs, step through the chamber and voltage
// pages every infoEveryMs and come back to the main page. It never sets "busy" (the controls stay usable),
// yields to any queued command, retries a failed read quickly and then falls back to the normal interval.
static void workerTask(void*) {
  uint32_t nextPoll[2] = {0, 0};
  Run prevRun[2] = {RUN_UNKNOWN, RUN_UNKNOWN};
  uint8_t infoFails[2] = {0, 0};
  for (;;) {
    Job j;
    if (otaBusy) { vTaskDelay(pdMS_TO_TICKS(200)); continue; }
    dhtTick();
    smartTick();
    if (xQueueReceive(jobQ, &j, pdMS_TO_TICKS(150)) == pdTRUE) {
      runJob(j);
      continue;
    }
    for (int i = 0; i < 2; i++) {
      Heater& h = heaters[i];
      uint32_t now = millis();
      if (!h.enabled || !reached(now, nextPoll[i])) continue;
      nextPoll[i] = now + POLL_MS;
      if (xSemaphoreTake(radioMux, pdMS_TO_TICKS(200)) != pdTRUE) continue;
      radioReady();
      pollOnce(h);
      if (h.run == RUN_ON && prevRun[i] != RUN_ON) nextInfoAt[i] = now + 3000;  // first values soon after start
      prevRun[i] = h.run;
      if (h.run == RUN_ON && reached(now, nextInfoAt[i]) && uxQueueMessagesWaiting(jobQ) == 0) {
        const char* e = jobRefresh(h);
        if (e) {
          infoFails[i]++;
          wlog(L_INFO_FAIL, e, (unsigned)infoFails[i]);
          nextInfoAt[i] = millis() + (infoFails[i] < 3 ? 5000 : infoEveryMs);
          if (infoFails[i] >= 3) infoFails[i] = 0;
        } else {
          infoFails[i] = 0;
          nextInfoAt[i] = millis() + infoEveryMs;
        }
      }
      xSemaphoreGive(radioMux);
    }
  }
}

// ---- state and commands for the cloud client ----

// names/prefixes only matter with two heaters
static void applyNames() {
  bool two = heaters[0].enabled && heaters[1].enabled;
  for (int i = 0; i < 2; i++) {
    if (two) snprintf(heaters[i].prefix, sizeof heaters[i].prefix, "%s: ", heaters[i].name);
    else heaters[i].prefix[0] = 0;
  }
}

static String num(int v, bool known) {
  return known ? String(v) : String("null");
}

static String text(const char* s) {
  if (!s) return String("null");
  String r = "\"";
  jsonEscape(r, s);
  r += "\"";
  return r;
}

String controlStateJson() {
  static const char* RUN_NAME[] = {"unknown", "off", "starting", "on", "cooling"};
  uint32_t now = millis();
  String s = "\"env\":";
  if (envAt) s += "{\"t\":" + String(envT, 1) + ",\"h\":" + String(envH, 0) + ",\"age\":" + String((now - envAt) / 1000) + "}";
  else s += "null";
  s += ",\"heaters\":[";
  bool first = true;
  for (int i = 0; i < 2; i++) {
    Heater& h = heaters[i];
    if (!h.enabled) continue;
    if (!first) s += ",";
    first = false;
    bool live = h.seen && now - h.seen < 20000;
    bool on = h.run == RUN_ON;
    s += "{\"i\":" + String(i) + ",\"name\":" + text(h.name) + ",\"run\":\"" + (live ? RUN_NAME[h.run] : "unknown") + "\"";
    s += ",\"manual\":" + String(h.manual ? "true" : "false");
    s += ",\"level\":" + num(h.level, on && h.manual);
    s += ",\"setpoint\":" + num(h.setpoint, on && !h.manual && h.setpoint > 0);
    s += ",\"chamber\":" + num(h.chamber, h.chamberAt != 0 && on);
    s += ",\"volts\":" + num(h.volts, h.voltsAt != 0);
    s += ",\"age\":" + num((now - h.seen) / 1000, h.seen != 0);
    s += ",\"busy\":" + text(h.busy);
    String sm = "null";
    if (i == 0) {
      xSemaphoreTake(smartMux, portMAX_DELAY);
      if (smart.enabled) {
        static const char* PHASE[] = {"czeka", "start", "rozgrzewka", "grzanie", "utrzymanie"};
        float rate = smart.slope(secs());
        sm = "{\"on\":true,\"target\":" + String((int)lroundf(smart.target)) + ",\"phase\":\"" + PHASE[smart.phase()] + "\"";
        if (on && h.manual) sm += ",\"level\":" + String(h.level);
        if (!isnan(rate)) sm += ",\"rate\":" + String(rate * 60, 1);  // degrees per hour
        sm += "}";
      }
      xSemaphoreGive(smartMux);
    }
    s += ",\"err\":" + text(h.err && now - h.errAt < 60000 ? h.err : nullptr) + ",\"smart\":" + sm + "}";
  }
  s += "]";
  return s;
}

bool controlSubmit(int hi, const String& a, int v, int cmdId, String& err) {
  if (hi < 0 || hi > 1 || !heaters[hi].enabled) { err = L_E_UNKNOWN_HEATER; return false; }
  Heater& h = heaters[hi];
  uint32_t now = millis();
  bool fresh = h.seen && now - h.seen < 20000;
  Job j = {(uint8_t)hi, A_REFRESH, v, cmdId};
  wlog(L_SERVER_CMD, h.prefix, a.c_str(), v);

  if (a == "smart") {  // 0 = off, 6-35 = on with this target (or a new target when it already runs)
    if (v != 0 && (v < 6 || v > 35)) { err = L_E_TEMP_RANGE; return false; }
    if (v == 0) {
      smartStop(L_STOP_PANEL_CMD);
    } else {
      if (!(envAt && now - envAt < 120000)) { err = L_E_SMART_SENSOR; return false; }
      xSemaphoreTake(smartMux, portMAX_DELAY);
      if (smart.enabled) smart.target = (float)v;
      else smart.enable((float)v, secs());
      xSemaphoreGive(smartMux);
      smartSave();
      wlog(L_SMART_TARGET, v);
    }
    Ack ak;
    ak.id = cmdId;
    ak.ok = true;
    ak.msg[0] = 0;
    xQueueSend(ackQ, &ak, 0);
    return true;
  }
  if (a == "off") {
    smartStop(L_STOP_PANEL_OFF);  // off always goes through, and smart must not start the heater again
    j.act = A_OFF;
  } else if (h.busy) {
    err = String(L_E_BUSY) + h.busy;
    return false;
  } else if (a == "on") {
    if (!fresh || h.run != RUN_OFF) { err = L_E_ON_ONLY_OFF; return false; }
    j.act = A_ON;
  } else if (a == "level") {
    if (v < 1 || v > 6) { err = L_E_LEVEL_RANGE; return false; }
    if (!fresh || h.run != RUN_ON) { err = L_E_NOT_RUNNING; return false; }
    if (!h.manual) { err = L_E_IS_AUTO; return false; }
    j.act = A_LEVEL;
  } else if (a == "temp") {
    if (v < 6 || v > 35) { err = L_E_TEMP_RANGE; return false; }
    if (!fresh || h.run != RUN_ON) { err = L_E_NOT_RUNNING; return false; }
    if (h.manual) { err = L_E_IS_MANUAL; return false; }
    j.act = A_TEMP;
  } else if (a == "mode") {
    if (v != 0 && v != 1) { err = L_E_MODE_RANGE; return false; }
    if (!fresh || h.run != RUN_ON) { err = L_E_NOT_RUNNING; return false; }
    j.act = A_MODE;
  } else if (a == "sniff") {  // listen to every V1 frame in range and log the addresses (to find another remote)
    if (v < 10 || v > 120) { err = L_E_TIME_RANGE; return false; }
    j.act = A_SNIFF;
  } else {
    err = L_E_UNKNOWN_ACTION;
    return false;
  }
  if (j.act == A_OFF) {
    // Power off wins over everything: it interrupts the running job or the background page walk (the worker clears the
    // flag when the off job starts), goes to the front of the queue, and finds room even when the queue is full.
    abortReq = true;
    if (xQueueSendToFront(jobQ, &j, 0) != pdTRUE) {
      xQueueReset(jobQ);
      if (xQueueSendToFront(jobQ, &j, 0) != pdTRUE) { abortReq = false; err = L_E_QUEUE_FULL; return false; }
    }
  } else {
    if (a != "sniff") smartStop(L_STOP_MANUAL);  // the user took over, but only for a command that is really accepted
    if (xQueueSend(jobQ, &j, 0) != pdTRUE) { err = L_E_QUEUE_FULL; return false; }
  }
  if (!h.busy) h.busy = ACT_NAME[j.act];  // reject a second request until the worker has started
  return true;
}

bool controlPopAck(Ack& a) {
  return xQueueReceive(ackQ, &a, 0) == pdTRUE;
}

uint32_t logCollect(uint32_t afterId, String& out, int maxLines) {
  uint32_t last = afterId;
  int n = 0;
  if (xSemaphoreTake(logMux, pdMS_TO_TICKS(100)) != pdTRUE) return afterId;
  uint32_t from = logNext > (uint32_t)LOG_N ? logNext - LOG_N + 1 : 1;
  if (afterId + 1 > from) from = afterId + 1;
  for (uint32_t id = from; id < logNext && n < maxLines; id++) {
    const LogLine& l = logRing[id % LOG_N];
    if (l.id != id) continue;
    if (n) out += ',';
    out += "{\"m\":\"";
    jsonEscape(out, l.text);
    out += "\"}";
    last = id;
    n++;
  }
  xSemaphoreGive(logMux);
  return last;
}

void controlDumpLog() {
  if (xSemaphoreTake(logMux, pdMS_TO_TICKS(200)) != pdTRUE) return;
  uint32_t from = logNext > (uint32_t)LOG_N ? logNext - LOG_N + 1 : 1;
  for (uint32_t id = from; id < logNext; id++) {
    const LogLine& l = logRing[id % LOG_N];
    if (l.id == id) Serial.printf("[%lu s] %s\n", (unsigned long)(l.ms / 1000), l.text);
  }
  xSemaphoreGive(logMux);
}

// Listens for V1 frames (a remote that is awake polls its heater about every 0.7 s; both the remote and the heater
// frames carry the remote's address) and returns the address heard most often.
bool controlFindRemote(int seconds, HeaterId& out, int* heard) {
  struct Seen { uint8_t a[3]; uint8_t hdr; uint16_t n; };
  Seen seen[6];
  int ns = 0;
  if (xSemaphoreTake(radioMux, pdMS_TO_TICKS(8000)) != pdTRUE) return false;
  radioReady();
  wlog(L_FIND_START, seconds);
  uint32_t t0 = millis();
  while (millis() - t0 < (uint32_t)seconds * 1000UL) {
    uint8_t b[12];
    if (!v1RawFrame(b, 500) || b[0] != 0x2B) continue;
    int k = -1;
    for (int i = 0; i < ns; i++) if (!memcmp(seen[i].a, b + 2, 3) && seen[i].hdr == b[1]) k = i;
    if (k < 0) {
      if (ns >= 6) continue;
      k = ns++;
      memcpy(seen[k].a, b + 2, 3);
      seen[k].hdr = b[1];
      seen[k].n = 0;
    }
    seen[k].n++;
  }
  xSemaphoreGive(radioMux);
  int best = -1;
  for (int i = 0; i < ns; i++) if (best < 0 || seen[i].n > seen[best].n) best = i;
  if (heard) *heard = best < 0 ? 0 : seen[best].n;
  if (best < 0 || seen[best].n < 3) { wlog(L_FIND_NONE, ns); return false; }  // a few frames, not one stray packet
  out.on = true;
  memcpy(out.addr, seen[best].a, 3);
  out.hdr = seen[best].hdr;
  wlog(L_FIND_OK, out.addr[0], out.addr[1], out.addr[2], out.hdr, seen[best].n);
  return true;
}

bool controlIdle() {
  return !heaters[0].busy && !heaters[1].busy && uxQueueMessagesWaiting(jobQ) == 0;
}

void controlSetOtaBusy(bool busy) { otaBusy = busy; }
void controlSetVerbose(bool on) { logVerbose = on; }

void controlStatus() {
  uint32_t now = millis();
  for (int i = 0; i < 2; i++) {
    Heater& h = heaters[i];
    if (!h.enabled) continue;
    Serial.printf("signal %d dBm, own temperature of the heater %d\n", h.rssi, h.ownTemp);
    Serial.printf("%s address %02X%02X%02X:%02X run=%d manual=%d level=%d setpoint=%d chamber=%d volts=%d seen %lu s ago, replies %u/%u%s%s\n",
                  h.name, h.addr[0], h.addr[1], h.addr[2], h.hdr, (int)h.run, h.manual, h.level, h.setpoint, h.chamber, h.volts,
                  h.seen ? (unsigned long)((now - h.seen) / 1000) : 0UL, (unsigned)h.pollOk, (unsigned)(h.pollOk + h.pollFail),
                  h.busy ? " busy: " : "", h.busy ? h.busy : "");
  }
  if (envAt) Serial.printf("room %.1f C, %.0f %%\n", envT, envH);
  Serial.printf("psram: %u B, free %u B\n",(unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreePsram());
  const char* tasks[] = {"sync", "heater"};
  for (const char* n : tasks) {
    TaskHandle_t th = xTaskGetHandle(n);
    if (th) Serial.printf("stack %s: free min. %u B\n", n,(unsigned)uxTaskGetStackHighWaterMark(th));
  }
  Serial.printf("memory: free %u B, lowest %u B, largest block %u B\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
}

void controlSetup(const Settings& s) {
  (void)*(volatile const char*)FW_MARK;  // keeps the markers in the image
  (void)*(volatile const char*)FW_LANG_MARK;
  radioMux = xSemaphoreCreateMutex();
  logMux = xSemaphoreCreateMutex();
  smartMux = xSemaphoreCreateMutex();
  jobQ = xQueueCreate(4, sizeof(Job));
  ackQ = xQueueCreate(8, sizeof(Ack));
  for (int i = 0; i < 2; i++) {
    Heater& h = heaters[i];
    h = Heater();
    h.name = L_HEATER;
    if (i > 0) continue;  // one controller, one heater (the model keeps two slots, the second one is never used)
    h.enabled = s.heater.on;
    memcpy(h.addr, s.heater.addr, 3);
    h.hdr = s.heater.hdr;
  }
  applyNames();
  Preferences sp;
  if (sp.begin("hnsmart", true)) {  // smart mode survives a restart and an update
    bool on = sp.getBool("on", false);
    float target = sp.getFloat("t", 20);
    bool wasOn = sp.getBool("won", false);
    int tries = sp.getUChar("tries", 0);
    sp.end();
    if (on && heaters[0].enabled && target >= 6 && target <= 35) {
      smart.enable(target, secs());
      smart.restore(wasOn, tries);  // a heater that smart was running and that is found stopped now ends smart mode
    }
  }
  pinMode(DHT_PWR_PIN, OUTPUT);
  digitalWrite(DHT_PWR_PIN, HIGH);
  envPowerAt = millis();
  wlog(L_BOOT, FW_VERSION, __DATE__, __TIME__, heaters[0].enabled ? L_REMOTE_SET : L_REMOTE_UNSET);
  xTaskCreate(workerTask, "heater", 5120, nullptr, 1, nullptr);
}
