#include "cloud.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#ifdef HN_DEV_OTA  // network flashing with espota, for developers: the build flag -DHN_DEV_OTA turns it on
#include <ArduinoOTA.h>
#endif
#include <Update.h>
#include <time.h>
#include <esp_heap_caps.h>
#include "control.h"
// Signed updates are optional: a build that has a public key (sniffer/signing_key.h, made by tools/ota-sign.py keygen)
// accepts only images that carry a valid signature of the maker; without the file every image the server offers is taken.
#if __has_include("signing_key.h")
#include "signing_key.h"
#include "sigverify.h"
#define HN_SIGNED 1
#endif

// The TLS buffers (2 x 16 kB) do not fit in the internal RAM that is left, so the big allocations of mbedtls go to the PSRAM.
static void* tlsCalloc(size_t n, size_t size) {
  void* p = n * size >= 512 ? heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : nullptr;
  return p ? p : heap_caps_calloc(n, size, MALLOC_CAP_8BIT);
}
static void tlsFree(void* p) { heap_caps_free(p); }
void cloudUsePsram() {
  static bool done = false;
  if (!done && psramFound()) mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree);
  done = true;
}

static Settings cfg;
static volatile CloudState state = CS_WIFI_DOWN;
static volatile bool hold = false;
static uint32_t wifiDownSince = 0, lastReconnect = 0;
#ifdef HN_DEV_OTA
static bool otaStarted = false;
#endif

static const uint32_t WIFI_LOST_MS = 180000;  // after this long without WiFi the setup portal may open
static const int MAX_ACKS = 6;  // more finished commands wait in the queue of control.cpp for the next sync
static Ack ackBuf[MAX_ACKS];
static int ackN = 0;
static uint32_t logSent = 0;  // id of the last log line the server has confirmed

void cloudHold(bool h) { hold = h; }
CloudState cloudState() { return state; }
bool cloudWifiLostLong() { return wifiDownSince && millis() - wifiDownSince > WIFI_LOST_MS; }

// the certificate check needs the clock
bool cloudSyncTime(uint32_t ms) {
  if (time(nullptr) > 1700000000) return true;
  static bool started = false;  // restarting SNTP on every call kept it from ever finishing a round
  if (!started) {
    configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
    started = true;
  }
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    if (time(nullptr) > 1700000000) return true;
    delay(200);
  }
  IPAddress ip;
  bool dnsOk = WiFi.hostByName("pool.ntp.org", ip);
  wlog(L_NTP_FAIL, (unsigned long)(ms / 1000), WiFi.dnsIP().toString().c_str(),
       WiFi.dnsIP(1).toString().c_str(), dnsOk ? ip.toString().c_str() : L_DNS_UNRESOLVED);
  return false;
}

// the moment this firmware was compiled (UTC is close enough: a day of slack is allowed by the caller)
static time_t buildTime() {
  static const char* M = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4] = "";
  int d = 0, y = 0;
  struct tm tm = {};
  if (sscanf(__DATE__, "%3s %d %d", mon, &d, &y) != 3) return 1700000000;
  const char* at = strstr(M, mon);
  if (!at) return 1700000000;
  tm.tm_year = y - 1900;
  tm.tm_mon = (int)(at - M) / 3;
  tm.tm_mday = d;
  tm.tm_hour = 12;
  setenv("TZ", "UTC0", 1);
  tzset();
  return mktime(&tm);
}

// When NTP does not answer (a network that drops it) the clock is taken from the Date header of the server itself:
// one request without certificate checks, only to read the header; everything after it is verified as usual.
bool cloudTimeFromServer(const Settings& s) {
  if (time(nullptr) > 1700000000) return true;
  if (strncmp(s.api, "https://", 8) != 0) return false;
  cloudUsePsram();
  NetworkClientSecure sc;
  sc.setInsecure();
  sc.setTimeout(10);
  HTTPClient http;
  const char* keys[] = {"Date"};
  http.collectHeaders(keys, 1);
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  if (!http.begin(sc, String(s.api) + "/api/v1/firmware")) return false;
  int code = http.GET();
  String date = code > 0 ? http.header("Date") : String();
  http.end();
  // "Fri, 30 Oct 2026 14:55:01 GMT"
  char mon[4] = "";
  int d = 0, y = 0, hh = 0, mm = 0, ss = 0;
  if (sscanf(date.c_str(), "%*3s, %d %3s %d %d:%d:%d", &d, mon, &y, &hh, &mm, &ss) != 6 || y < 2024) return false;
  static const char* M = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const char* at = strstr(M, mon);
  if (!at || (at - M) % 3) return false;
  struct tm tm = {};
  tm.tm_year = y - 1900;
  tm.tm_mon = (int)(at - M) / 3;
  tm.tm_mday = d;
  tm.tm_hour = hh;
  tm.tm_min = mm;
  tm.tm_sec = ss;
  setenv("TZ", "UTC0", 1);
  tzset();
  time_t t = mktime(&tm);
  // The header comes over a connection that is not verified (that is the point: it is only read), so an attacker on the
  // path could set any date, for example one that makes every certificate fail. Accept only a date between the day
  // this firmware was built and three years later.
  if (t < buildTime() - 86400 || t > buildTime() + 3L * 366 * 86400) return false;
  struct timeval tv = {t, 0};
  settimeofday(&tv, nullptr);
  wlog(L_TIME_FROM_SERVER, date.c_str());
  return time(nullptr) > 1700000000;
}

static bool needsClock(const Settings& s) {
  return !s.insecure && strncmp(s.api, "https://", 8) == 0;
}

// One POST. `reuse` keeps the TLS connection open between calls (the sync task); the probe opens its own.
static int post(const Settings& s, const String& body, String& resp, String& err, bool reuse, bool sendKey) {
  static NetworkClientSecure secureShared;
  static NetworkClient plainShared;
  NetworkClientSecure secureOwn;
  NetworkClient plainOwn;
  bool tls = strncmp(s.api, "https://", 8) == 0;
  NetworkClientSecure& sc = reuse ? secureShared : secureOwn;
  NetworkClient& pl = reuse ? plainShared : plainOwn;
  if (tls) {
    if (s.insecure) sc.setInsecure();
    else sc.useBuiltinCACertBundle();
  }
  sc.setTimeout(10);

  HTTPClient http;
  http.setReuse(reuse);
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  String url = String(s.api) + "/api/v1/sync";
  bool began = tls ? http.begin(sc, url) : http.begin(pl, url);
  if (!began) { err = L_E_BAD_SERVER; return -100; }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Id", deviceId());
  http.addHeader("X-Device-Secret", s.secret);
  if (sendKey) http.addHeader("X-Enroll-Key", s.ekey);  // needed to register; once approved the secret is enough
  int code = http.POST(body);
  if (code > 0) resp = http.getString();
  else {
    char buf[100] = "";
    if (tls) sc.lastError(buf, sizeof buf);
    err = buf[0] ? String(buf) : http.errorToString(code);
    wlog(L_HTTP_HEAP, code, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    if (reuse) sc.stop();
  }
  http.end();
  return code;
}

// ---- tiny reader for the server's answer (its own flat JSON) ----

static bool field(const String& obj, const char* key, String& out) {
  String k = String("\"") + key + "\":";
  int i = obj.indexOf(k);
  if (i < 0) return false;
  i += k.length();
  if (obj[i] == '"') {
    int e = obj.indexOf('"', i + 1);
    if (e < 0) return false;
    out = obj.substring(i + 1, e);
  } else {
    int e = i;
    while (e < (int)obj.length() && obj[e] != ',' && obj[e] != '}' && obj[e] != ']') e++;
    out = obj.substring(i, e);
  }
  return true;
}

static int fieldInt(const String& obj, const char* key, int def) {
  String v;
  return field(obj, key, v) ? v.toInt() : def;
}

static void pushAck(int id, bool ok, const char* msg) {
  if (ackN >= MAX_ACKS) return;
  ackBuf[ackN].id = id;
  ackBuf[ackN].ok = ok;
  strlcpy(ackBuf[ackN].msg, msg, sizeof ackBuf[ackN].msg);
  ackN++;
}

static void runCommands(const String& resp) {
  int arr = resp.indexOf("\"commands\":[");
  if (arr < 0) return;
  int pos = arr;
  for (;;) {
    int s = resp.indexOf('{', pos);
    if (s < 0) break;
    int e = resp.indexOf('}', s);
    if (e < 0) break;
    String obj = resp.substring(s, e + 1);
    pos = e + 1;
    String action;
    int id = fieldInt(obj, "id", 0);
    if (!id || !field(obj, "action", action)) continue;
    String why;
    if (!controlSubmit(fieldInt(obj, "heater", 0), action, fieldInt(obj, "value", 0), id, why)) {
      wlog(L_CMD_REJECTED, id, why.c_str());
      pushAck(id, false, why.c_str());
    }
  }
}

// ---- update offered by the server ----

static uint32_t updateRetryAt = 0;

// Feeds what HTTPClient decodes (identity or chunked) straight into the flash writer.
struct UpdateSink : public Stream {
  size_t count = 0;
#ifdef HN_SIGNED
  OtaVerifier* verifier = nullptr;
#endif
  size_t write(const uint8_t* b, size_t n) override {
    size_t w = Update.write(const_cast<uint8_t*>(b), n);
    count += w;
#ifdef HN_SIGNED
    if (verifier) verifier->feed(b, w);
#endif
    return w;
  }
  size_t write(uint8_t) override { return 0; }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
};

// Downloads the image the server offered, checks its size and MD5 and makes it the next boot image.
static bool installUpdate(int version, int size, const String& md5, const String& file, String& why) {
  NetworkClientSecure secure;
  // Code is only taken from a server whose certificate has been checked: plain HTTP, or HTTPS without the check, lets
  // anybody on the path hand the controller a firmware of their own.
  bool tls = strncmp(cfg.api, "https://", 8) == 0;
  if (!tls || cfg.insecure) { why = L_E_OTA_NO_TLS; return false; }
  secure.useBuiltinCACertBundle();
  secure.setTimeout(15);
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(15000);
  String url = String(cfg.api) + "/api/v1/" + file + "?v=" + version;  // the server names the file: one per language
  if (!http.begin(secure, url)) { why = L_E_BAD_URL; return false; }
  http.addHeader("X-Device-Id", deviceId());
  http.addHeader("X-Device-Secret", cfg.secret);
  int code = http.GET();
  if (code != 200) { why = "HTTP " + String(code); http.end(); return false; }
  int len = http.getSize();  // -1 when the server answers in chunks (nginx does for PHP): the size of the offer is then the reference
  if (len >= 0 && len != size) { char b[64]; snprintf(b, sizeof b, L_E_SIZE, len, size); why = b; http.end(); return false; }
  if (!Update.begin(size)) { why = Update.errorString(); http.end(); return false; }
  Update.setMD5(md5.c_str());
  UpdateSink sink;
#ifdef HN_SIGNED
  OtaVerifier verifier;
  verifier.begin();
  sink.verifier = &verifier;
#endif
  http.writeToStream(&sink);
  http.end();
  if (sink.count != (size_t)size) { char b[64]; snprintf(b, sizeof b, L_E_GOT, (unsigned)sink.count, size); why = b; Update.abort(); return false; }
#ifdef HN_SIGNED
  const char* sigWhy = "";  // checked BEFORE Update.end(): that call is what makes the image the next boot image
  if (!verifier.finish(HN_PUBKEY, (uint32_t)version, HN_LANG_CODE, &sigWhy)) {
    Serial.printf("update refused: %s\n", sigWhy);
    why = L_E_BAD_SIGNATURE;
    Update.abort();
    return false;
  }
#endif
  if (!Update.end()) { why = Update.errorString(); return false; }  // also checks the MD5 and the image header
  return true;
}

static void maybeUpdate(const String& resp) {
  int at = resp.indexOf("\"update\":{");
  if (at < 0 || millis() < 60000 || (int32_t)(millis() - updateRetryAt) < 0 || !controlIdle()) return;
  String obj = resp.substring(at, resp.indexOf('}', at) + 1);
  int version = fieldInt(obj, "version", 0), size = fieldInt(obj, "size", 0);
  String md5, file;
  if (version <= FW_VERSION || size < 100000 || !field(obj, "md5", md5) || md5.length() != 32) return;
  if (!field(obj, "file", file) || file.length() > 20) file = "firmware";  // servers from before languages offer no file name
  wlog(L_UPD_START, version, size);
  controlSetOtaBusy(true);  // the radio waits while the flash is written
  String why;
  if (installUpdate(version, size, md5, file, why)) {
    wlog(L_UPD_DONE);
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP.restart();
  }
  controlSetOtaBusy(false);
  updateRetryAt = millis() + 600000;  // try again in 10 minutes
  wlog(L_UPD_FAIL, why.c_str());
}

// ---- sync task ----

static void syncTask(void*) {
  int badRequests = 0;
  uint32_t failures = 0;
  for (;;) {
    if (hold || WiFi.status() != WL_CONNECTED) {
      if (WiFi.status() != WL_CONNECTED) state = CS_WIFI_DOWN;
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    if (needsClock(cfg) && !cloudSyncTime(10000) && !cloudTimeFromServer(cfg)) {
      state = CS_NO_SERVER;
      wlog(L_NO_TIME_CERT);
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }
    Ack a;
    while (ackN < MAX_ACKS && controlPopAck(a)) ackBuf[ackN++] = a;

    String logs;
    uint32_t lastLog = logCollect(logSent, logs, 20);
    String body;
    body.reserve(1536);
    body = String("{\"fw\":\"") + FW_VERSION + "\",\"lang\":\"" HN_LANG_CODE "\",\"rssi\":" + WiFi.RSSI() + ",\"uptime\":" + (millis() / 1000) + ",";
    body += controlStateJson();
    body += ",\"acks\":[";
    for (int i = 0; i < ackN; i++) {
      if (i) body += ',';
      body += "{\"id\":" + String(ackBuf[i].id) + ",\"ok\":" + (ackBuf[i].ok ? "true" : "false") + ",\"msg\":\"";
      jsonEscape(body, ackBuf[i].msg);
      body += "\"}";
    }
    body += "],\"log\":[" + logs + "]}";

    String resp, err;
    int code = post(cfg, body, resp, err, true, state != CS_ONLINE);  // a failure puts the state back, so the key goes again
    uint32_t wait = 3000;
    if (code == 200) {
      failures = 0;
      badRequests = 0;
      String status;
      field(resp, "status", status);
      int retry = fieldInt(resp, "retry", 3);
      wait = constrain(retry, 1, 600) * 1000UL;
      if (status == "approved") {
        state = CS_ONLINE;
        ackN = 0;
        logSent = lastLog;
        runCommands(resp);
        maybeUpdate(resp);
      } else {
        state = status == "rejected" ? CS_REJECTED : CS_PENDING;
      }
    } else {
      state = CS_NO_SERVER;
      failures++;
      if (code == 400) {  // the server refuses this very body: drop what it may not like, or we would never get through
        if (++badRequests >= 3) { ackN = 0; logSent = lastLog; badRequests = 0; wlog(L_SRV_400); }
      }
      if (failures == 1 || failures % 20 == 0) wlog(L_SRV_FAIL, code, err.c_str(), (unsigned)failures);
      wait = min<uint32_t>(3000 + failures * 2000UL, 30000);
    }
    vTaskDelay(pdMS_TO_TICKS(wait));
  }
}

// ---- WiFi and OTA ----

void cloudSetup(const Settings& s) {
  cfg = s;
  cloudUsePsram();
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(deviceId());
  WiFi.setAutoReconnect(true);
  WiFi.begin(cfg.ssid, cfg.wpass);
  if (settingsEnsureSecret(cfg)) settingsSave(cfg);  // first boot: made with the radio running, so the random numbers are real
  wifiDownSince = millis();
#ifdef HN_DEV_OTA
  ArduinoOTA.setHostname(deviceId());
  ArduinoOTA.setPassword(cfg.ekey);  // the key from the panel doubles as the update password
  ArduinoOTA.onStart([]() { controlSetOtaBusy(true); wlog(L_OTA); });
  ArduinoOTA.onEnd([]() { controlSetOtaBusy(false); });
  ArduinoOTA.onError([](ota_error_t) { controlSetOtaBusy(false); });
#endif
  xTaskCreate(syncTask, "sync", 8192, nullptr, 1, nullptr);
}

void cloudLoop() {
  uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    if (wifiDownSince) { wlog(L_WIFI_UP, WiFi.localIP().toString().c_str(), WiFi.RSSI()); wifiDownSince = 0; }
#ifdef HN_DEV_OTA
    if (!otaStarted) { ArduinoOTA.begin(); otaStarted = true; }
    ArduinoOTA.handle();
#endif
  } else {
    if (!wifiDownSince) { wifiDownSince = now ? now : 1; wlog(L_WIFI_DOWN); }
    if (!hold && now - lastReconnect > 30000) { lastReconnect = now; WiFi.reconnect(); }
  }
}

// ---- used by the setup portal ----

int cloudProbe(const Settings& s, String& message) {
  cloudUsePsram();
  String body = String("{\"fw\":\"") + FW_VERSION + "\",\"uptime\":" + (millis() / 1000) + "}";
  String resp, err;
  int code = post(s, body, resp, err, false, true);
  if (code == 200) {
    String st;
    field(resp, "status", st);
    if (st == "approved") message = L_PROBE_APPROVED;
    else if (st == "rejected") message = L_PROBE_REJECTED;
    else message = L_PROBE_PENDING;
  } else if (code == 403) message = L_PROBE_403;
  else if (code == 401) message = L_PROBE_401;
  else if (code == 404) message = L_PROBE_404;
  else if (code == 429) message = L_PROBE_429;
  else if (code == 400) message = L_PROBE_400;
  else if (code > 0) message = L_PROBE_HTTP + String(code) + ".";
  else {
    message = L_PROBE_CONNECT + err;
    if (!s.insecure && strncmp(s.api, "https://", 8) == 0) message += L_PROBE_CERT_HINT;
  }
  return code;
}
