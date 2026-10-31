// Webasto/Vevor RF sniffer: Wemos S2 mini + CC1101 (own minimal driver, no RadioLib)
// Serial commands: see HELP
#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include "esp32-hal-tinyusb.h"  // usb_persist_restart
#include "control.h"
#include "settings.h"
#include "ui.h"
#include "cloud.h"
#include "portal.h"

const char* HELP = "setup: wifi | wifitx <dBm> | cfg show|ssid|pass|api|key|id|ins|save|erase | cfg find | portal [lan] | restart | log | verbose 0|1 | status\ncmds: scan | f <MHz> | mode ook|fsk | thr <dBm> | cap | rssi | "
                   "heater [s] | heater1 [s] | fh <MHz> | addr <hex8> | wake | send <hex cmd> [yes] | v1 up|down|off|menu|mode|on yes|poll|restart yes | boot";

constexpr int PIN_SCK = 7, PIN_MISO = 9, PIN_MOSI = 11, PIN_CS = 12;
constexpr int PIN_GDO0 = 5, PIN_GDO2 = 3;

// CC1101 registers / strobes
constexpr uint8_t R_IOCFG2 = 0x00, R_IOCFG0 = 0x02, R_PKTCTRL0 = 0x08;
constexpr uint8_t R_FREQ2 = 0x0D, R_FREQ1 = 0x0E, R_FREQ0 = 0x0F;
constexpr uint8_t R_MDMCFG4 = 0x10, R_MDMCFG3 = 0x11, R_MDMCFG2 = 0x12, R_DEVIATN = 0x15;
constexpr uint8_t R_MCSM0 = 0x18, R_FREND0 = 0x22;
constexpr uint8_t S_RES = 0x30, S_RX = 0x34, S_IDLE = 0x36, S_FRX = 0x3A;
constexpr uint8_t ST_VERSION = 0x31, ST_RSSI = 0x34;

bool radioOk = false;
bool v1Active = false;  // radio currently configured for the V1 heater protocol
float freqMHz = 433.92;
bool ook = true;
int thrDbm = -80;

// the radio code calls this from its long loops; the LED and the button live in ui.cpp
void blinkTick() { uiTick(); }

// ---- CC1101 low level ----
uint8_t ccTransfer(uint8_t hdr, uint8_t val, bool hasVal) {
  SPI.beginTransaction(SPISettings(500000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_CS, LOW);
  delayMicroseconds(10);
  SPI.transfer(hdr);
  uint8_t r = hasVal ? SPI.transfer(val) : 0;
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();
  return r;
}
void ccStrobe(uint8_t s) { ccTransfer(s, 0, false); }
void ccWrite(uint8_t reg, uint8_t v) { ccTransfer(reg, v, true); }
uint8_t ccStatus(uint8_t reg) { return ccTransfer(0xC0 | reg, 0, true); }

float getRSSI() {
  uint8_t raw = ccStatus(ST_RSSI);
  int v = raw >= 128 ? (int)raw - 256 : raw;
  return v / 2.0f - 74.0f;
}

void setFreq(float mhz) {
  uint32_t f = (uint32_t)((double)mhz * 65536.0 / 26.0);
  ccWrite(R_FREQ2, (f >> 16) & 0xFF);
  ccWrite(R_FREQ1, (f >> 8) & 0xFF);
  ccWrite(R_FREQ0, f & 0xFF);
}

// idle -> retune -> RX (autocal runs on the idle->rx transition)
void startRx() {
  ccStrobe(S_IDLE);
  setFreq(freqMHz);
  ccWrite(R_MDMCFG2, ook ? 0x30 : 0x00);  // OOK or 2-FSK, no sync word
  ccWrite(R_FREND0, ook ? 0x11 : 0x10);
  ccStrobe(S_FRX);
  ccStrobe(S_RX);
  delay(3);
}

// bit-banged SPI byte, used for the power-on reset
uint8_t softXfer(uint8_t out) {
  uint8_t in = 0;
  for (int i = 7; i >= 0; i--) {
    digitalWrite(PIN_MOSI, (out >> i) & 1);
    delayMicroseconds(3);
    digitalWrite(PIN_SCK, HIGH);
    delayMicroseconds(3);
    in = (in << 1) | digitalRead(PIN_MISO);
    digitalWrite(PIN_SCK, LOW);
  }
  return in;
}

void softWaitReady() {
  uint32_t t = millis();
  while (digitalRead(PIN_MISO) && millis() - t < 50) {}
}

// CC1101 power-on reset (datasheet 19.1.2)
void powerOnReset() {
  pinMode(PIN_SCK, OUTPUT); pinMode(PIN_MOSI, OUTPUT); pinMode(PIN_CS, OUTPUT);
  pinMode(PIN_MISO, INPUT_PULLUP);
  digitalWrite(PIN_SCK, LOW);
  digitalWrite(PIN_CS, HIGH); delayMicroseconds(10);
  digitalWrite(PIN_CS, LOW);  delayMicroseconds(10);
  digitalWrite(PIN_CS, HIGH); delayMicroseconds(50);
  digitalWrite(PIN_CS, LOW);
  softWaitReady();
  softXfer(S_RES);
  softWaitReady();
  digitalWrite(PIN_CS, HIGH);
  delay(5);
}

// raw pulse mode: async serial data on GDO0
void configureRaw() {
  v1Active = false;
  ccStrobe(S_IDLE);
  ccWrite(R_IOCFG2, 0x2E);    // GDO2 unused (high-Z)
  ccWrite(R_IOCFG0, 0x0D);    // GDO0 = async serial data out
  ccWrite(R_PKTCTRL0, 0x30);  // async serial mode
  ccWrite(R_MDMCFG4, 0x78);   // RX bandwidth ~230 kHz, data rate exp 8
  ccWrite(R_MDMCFG3, 0x93);   // ~10 kBaud
  ccWrite(R_DEVIATN, 0x47);   // ~47 kHz deviation (FSK)
  ccWrite(R_MCSM0, 0x18);     // autocalibrate idle -> rx
}

float heaterMHz = 433.937;  // DieselPilot: 0x10B09C; the old 434.0 was ~60 kHz off, outside the RX bandwidth
uint32_t heaterAddr = 0;    // address of the paired remote, carried in bytes 2..5 of every frame
bool haveAddr = false;
uint8_t txSeq = 0;

// packet mode used by the heater and its remote (GFSK ~10 kBaud, sync 7E 3C, variable length)
void configureHeater() {
  v1Active = false;
  const uint8_t regs[][2] = {
    {0x00, 0x07}, {0x02, 0x06}, {0x03, 0x47}, {0x07, 0x04}, {0x08, 0x05},
    {0x0A, 0x00}, {0x0B, 0x06}, {0x0C, 0x00}, {0x0D, 0x10}, {0x0E, 0xB1},
    {0x0F, 0x3B}, {0x10, 0xF8}, {0x11, 0x93}, {0x12, 0x13}, {0x13, 0x22},
    {0x14, 0xF8}, {0x15, 0x26}, {0x17, 0x30}, {0x18, 0x18}, {0x19, 0x16},
    {0x1A, 0x6C}, {0x1B, 0x03}, {0x1C, 0x40}, {0x1D, 0x91}, {0x20, 0xFB},
    {0x21, 0x56}, {0x22, 0x17}, {0x23, 0xE9}, {0x24, 0x2A}, {0x25, 0x00},
    {0x26, 0x1F}, {0x2C, 0x81}, {0x2D, 0x35}, {0x2E, 0x09}, {0x09, 0x00},
    {0x04, 0x7E}, {0x05, 0x3C},
  };
  ccStrobe(S_IDLE);
  for (auto& r : regs) ccWrite(r[0], r[1]);
  setFreq(heaterMHz);
}

uint16_t crc16modbus(const uint8_t* d, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    crc ^= d[i];
    for (int b = 0; b < 8; b++) crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
  }
  return crc;
}

void ccReadFifo(uint8_t* buf, size_t n) {
  SPI.beginTransaction(SPISettings(500000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_CS, LOW);
  delayMicroseconds(10);
  SPI.transfer(0xFF);  // FIFO burst read
  for (size_t i = 0; i < n; i++) buf[i] = SPI.transfer(0);
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();
}

void ccWriteBurst(uint8_t reg, const uint8_t* d, size_t n) {
  SPI.beginTransaction(SPISettings(500000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_CS, LOW);
  delayMicroseconds(10);
  SPI.transfer(reg | 0x40);
  for (size_t i = 0; i < n; i++) SPI.transfer(d[i]);
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();
}

// one packet from the RX FIFO: length byte, payload, RSSI, LQI|CRC_OK. 0 = nothing within timeoutMs
int heaterRx(uint8_t* b, uint32_t timeoutMs, bool stopOnKey) {
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs && !(stopOnKey && Serial.available())) {
    blinkTick();
    if (!digitalRead(PIN_GDO2)) continue;  // GDO2 high = packet received, HW CRC ok
    uint8_t n = ccStatus(0x3B) & 0x7F;      // RXBYTES
    if (n > 0 && n <= 64) ccReadFifo(b, n); else n = 0;
    ccStrobe(S_IDLE); ccStrobe(S_FRX); ccStrobe(S_RX);
    if (n) return n;
  }
  return 0;
}

// heater: 0x17 byte status frame (state, voltage, temperatures, mode, pump), remote: 9 byte command frame.
// Layouts differ between library versions, so the position of the CRC16 is searched instead of assumed.
void printPacket(const uint8_t* b, int n, uint32_t ms) {
  Serial.printf("[%lu ms] %d bytes:", (unsigned long)ms, n);
  for (int i = 0; i < n; i++) Serial.printf(" %02X", b[i]);
  if (n >= 3) Serial.printf("  rssi=%.1f lqi=%u", ((int8_t)b[n - 2]) / 2.0 - 74.0, b[n - 1] & 0x7F);
  Serial.println();
  int m = min(b[0] + 1, n - 2);  // length byte + payload actually received
  if (m < 8) return;
  for (int k = 5; k + 1 < m; k++) {
    uint16_t c = crc16modbus(b, k);
    bool be = c == (uint16_t)((b[k] << 8) | b[k + 1]);
    if (be || c == (uint16_t)((b[k + 1] << 8) | b[k])) {
      Serial.printf("   crc16 ok over 0..%d at %d (%s)\n", k - 1, k, be ? "BE" : "LE");
      break;
    }
  }
  uint32_t addr = ((uint32_t)b[2] << 24) | (b[3] << 16) | (b[4] << 8) | b[5];
  if (b[0] == 9) {
    Serial.printf("   REMOTE cmd=%02X addr=%08X seq=%u\n", b[1], (unsigned)addr, b[6]);
  } else if (m >= 17) {
    Serial.printf("   HEATER type=%02X addr=%08X state=%u err=%u volt=%.1f ambient=%d case=%u setpoint=%d mode=%s pump=%.1f\n",
                  b[1], (unsigned)addr, b[6], b[7], ((b[8] << 8) | b[9]) / 10.0, (int8_t)b[10], b[12], (int8_t)b[13],
                  b[14] == 0x32 ? "auto" : (b[14] == 0xCD ? "manual" : "?"), b[15] / 10.0);
  } else {
    return;
  }
  if (b[1] != 0xAA && (!haveAddr || addr != heaterAddr)) {
    heaterAddr = addr;
    haveAddr = true;
    Serial.printf("   address %08X saved\n", (unsigned)addr);
  }
}

// older "SUN" controller (DieselPilot V1): 433.892 MHz, 2.4 kBaud GFSK, sync 09 1A, fixed 10 bytes, no CRC
// frame: 2B 01 addr[3] cmd sub d1 d2 00
void configureHeaterV1() {
  const uint8_t regs[][2] = {
    {0x00, 0x2E}, {0x02, 0x2E}, {0x0B, 0x06}, {0x0D, 0x10}, {0x0E, 0xB0}, {0x0F, 0x71},
    {0x10, 0x86}, {0x11, 0x83}, {0x12, 0x12}, {0x13, 0x22}, {0x04, 0x09}, {0x05, 0x1A},
    {0x06, 0x0A}, {0x07, 0x04}, {0x08, 0x00}, {0x15, 0x40}, {0x18, 0x18}, {0x23, 0xFF},
  };
  ccStrobe(S_RES);
  delay(5);
  for (auto& r : regs) ccWrite(r[0], r[1]);
  v1Active = true;
}

uint8_t v1Addr[3] = {0, 0, 0};  // address of the paired remote, set by the worker from the settings (console: v1 id)
uint8_t v1Hdr = 0x15;                    // second frame byte, differs per remote (0x15 on the first heater, 0x12 on the second)

// remote -> heater, the way the original remote sends it: 2B 15 addr[3] cmd sub d1 d2 (last byte is noise on air)
void heaterTxV1(uint8_t cmd, uint8_t sub, uint8_t d1, uint8_t d2, bool verbose) {
  uint8_t f[10] = {0x2B, v1Hdr, v1Addr[0], v1Addr[1], v1Addr[2], cmd, sub, d1, d2, 0x00};
  ccStrobe(S_IDLE);
  ccStrobe(0x3B);  // flush TX
  ccWriteBurst(0x3F, f, sizeof f);
  ccStrobe(0x35);  // TX
  delay(100);
  ccStrobe(S_IDLE);
  if (!verbose) return;
  Serial.printf("TX V1:");
  for (uint8_t b : f) Serial.printf(" %02X", b);
  Serial.println();
}

// wait up to ms for a heater reply (B0..BF frame: the page is the low nibble); returns its sub byte (heater state) or -1
int v1ReadReply(uint32_t ms, uint8_t* b) {
  ccStrobe(S_IDLE); ccStrobe(S_FRX); ccStrobe(S_RX);
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    uint8_t n = ccStatus(0x3B);
    if (n & 0x80) { ccStrobe(S_IDLE); ccStrobe(S_FRX); ccStrobe(S_RX); continue; }  // RX FIFO overflow
    if ((n & 0x7F) < 12) continue;
    ccReadFifo(b, 12);
    ccStrobe(S_IDLE); ccStrobe(S_FRX); ccStrobe(S_RX);
    // B1/B2 are the info pages; the second heater model has one more screen (the heater's own temperature) that answers with 0x80
    if (((b[5] & 0xF0) == 0xB0 || b[5] == 0x80) && b[2] == v1Addr[0] && b[3] == v1Addr[1] && b[4] == v1Addr[2]) return b[6];
  }
  return -1;
}

// any V1 sized frame, whatever its address (used by the web sniffer); b gets 12 bytes (10 + RSSI + LQI)
bool v1RawFrame(uint8_t* b, uint32_t ms) {
  ccStrobe(S_IDLE); ccStrobe(S_FRX); ccStrobe(S_RX);
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    uint8_t n = ccStatus(0x3B);
    if (n & 0x80) { ccStrobe(S_IDLE); ccStrobe(S_FRX); ccStrobe(S_RX); continue; }  // RX FIFO overflow
    if ((n & 0x7F) < 12) continue;
    ccReadFifo(b, 12);
    ccStrobe(S_IDLE); ccStrobe(S_FRX); ccStrobe(S_RX);
    return true;
  }
  return false;
}

// poll like the remote does (A9 00 00 00); prints the heater state whenever it changes
int v1Poll(uint32_t t0, int* last) {
  uint8_t b[12];
  heaterTxV1(0xA9, 0x00, 0x00, 0x00, false);
  int sub = v1ReadReply(400, b);
  int key = sub < 0 ? -1 : ((b[5] << 8) | sub);
  if (key != *last) {
    if (sub < 0) Serial.printf("[%lu s] no reply\n", (unsigned long)((millis() - t0) / 1000));
    else Serial.printf("[%lu s] state page=%02X sub=%02X d1=%02X d2=%02X\n", (unsigned long)((millis() - t0) / 1000), b[5], b[6], b[7], b[8]);
    *last = key;
  }
  return sub;
}

// wait for the cool down after power off (sub 0x60 -> 0x00), then power on and check that it started
void v1Restart() {
  configureHeaterV1();
  Serial.println("restart: polling until the heater is cooled down, then power on. Any char aborts.");
  uint32_t t0 = millis();
  int last = -2, idle = 0, noReply = 0;
  while (millis() - t0 < 600000UL && !Serial.available()) {
    int sub = v1Poll(t0, &last);
    noReply = sub < 0 ? noReply + 1 : 0;
    if (noReply >= 15) { Serial.println("no reply from the heater, aborting"); return; }
    if ((sub == 0x20 || sub == 0xA0) && millis() - t0 > 20000UL) { Serial.println("heater still running, the power off was not accepted, aborting"); return; }
    idle = sub == 0x00 ? idle + 1 : 0;
    if (idle >= 3) {
      Serial.println("cooled down, sending power on");
      heaterTxV1(0xAB, 0xAD, 0x80, 0x00, true);
      uint32_t t1 = millis();
      while (millis() - t1 < 20000UL) {
        int s = v1Poll(t0, &last);
        if (s == 0x20 || s == 0xA0) { Serial.println("heater started"); return; }
        delay(250);
      }
      Serial.println("no start seen within 20 s");
      return;
    }
    delay(250);
  }
  Serial.println("restart aborted or timed out");
}

void heaterListenV1(uint32_t ms) {
  ccStrobe(S_IDLE); ccStrobe(S_FRX); ccStrobe(S_RX);
  delay(5);
  uint32_t t0 = millis();
  uint8_t b[12];
  while (millis() - t0 < ms && !Serial.available()) {
    blinkTick();
    uint8_t n = ccStatus(0x3B);
    if (n & 0x80) { ccStrobe(S_IDLE); ccStrobe(S_FRX); ccStrobe(S_RX); continue; }  // RX FIFO overflow
    if ((n & 0x7F) < 12) continue;
    ccReadFifo(b, 12);
    ccStrobe(S_IDLE); ccStrobe(S_FRX); ccStrobe(S_RX);
    Serial.printf("[%lu ms] V1:", (unsigned long)(millis() - t0));
    for (int i = 0; i < 12; i++) Serial.printf(" %02X", b[i]);
    Serial.printf("  addr=%02X%02X%02X cmd=%02X sub=%02X d1=%02X d2=%02X rssi=%.1f lqi=%u\n", b[2], b[3], b[4], b[5], b[6], b[7], b[8],
                  ((int8_t)b[10]) / 2.0 - 74.0, b[11] & 0x7F);
  }
}

// print everything heard for ms milliseconds; any key stops
void heaterListen(uint32_t ms) {
  uint32_t t0 = millis();
  uint8_t b[64];
  ccStrobe(S_FRX);
  ccStrobe(S_RX);
  delay(3);
  while (millis() - t0 < ms && !Serial.available()) {
    int n = heaterRx(b, ms - (millis() - t0), true);
    if (n) printPacket(b, n, millis() - t0);
  }
}

void heaterEnter() {
  configureHeater();
  Serial.printf("Heater mode %.3f MHz GFSK, sync 7E3C\n", heaterMHz);
}

void heaterLeave() {
  while (Serial.available()) Serial.read();
  configureRaw();
  startRx();
}

// one command frame, repeated like the real remote; the heater acts on the first valid copy
bool heaterTx(uint8_t cmd) {
  if (!haveAddr) { Serial.println("no address: 'addr <hex8>' or listen to the remote first"); return false; }
  static const uint8_t pa[8] = {0x00, 0x12, 0x0E, 0x34, 0x60, 0xC5, 0xC1, 0xC0};
  ccStrobe(S_IDLE);
  ccWriteBurst(0x3E, pa, 8);
  for (int i = 0; i < 10; i++) {
    uint8_t f[10] = {9, cmd, (uint8_t)(heaterAddr >> 24), (uint8_t)(heaterAddr >> 16), (uint8_t)(heaterAddr >> 8),
                     (uint8_t)heaterAddr, txSeq++, 0, 0, 0};
    uint16_t c = crc16modbus(f, 7);
    f[7] = c >> 8;
    f[8] = c & 0xFF;
    ccStrobe(S_IDLE);
    ccStrobe(0x3B);  // flush TX
    ccWriteBurst(0x3F, f, sizeof f);
    ccStrobe(0x35);  // TX
    uint32_t t = millis();
    while (!digitalRead(PIN_GDO0) && millis() - t < 20) {}   // sync word sent
    while (digitalRead(PIN_GDO0) && millis() - t < 100) {}   // end of packet
    delay(10);
  }
  ccStrobe(S_IDLE);
  Serial.printf("TX cmd=%02X addr=%08X x10\n", cmd, (unsigned)heaterAddr);
  return true;
}

// 23 wake (status poll) | 24 mode | 3C up | 3E down | 2B power (needs "yes")
void heaterSend(uint8_t cmd) {
  heaterEnter();
  if (heaterTx(cmd)) {
    Serial.println("reply:");
    heaterListen(2500);
  }
  heaterLeave();
}

void initRadio() {
  powerOnReset();
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, -1);
  uint8_t ver = ccStatus(ST_VERSION);
  Serial.printf("CC1101 VERSION=0x%02X\n", ver);
  if (ver != 0x14 && ver != 0x04) {
    Serial.println("CC1101 not found (check wiring)");
    return;
  }
  configureRaw();
  radioOk = true;
  startRx();
  Serial.printf("CC1101 ok, %.3f MHz, %s\n", freqMHz, ook ? "OOK" : "FSK");
}

// ---- raw pulse capture ----
constexpr size_t MAX_EDGES = 4000;
volatile uint32_t edgeT[MAX_EDGES];
volatile size_t edgeN = 0;

void IRAM_ATTR onEdge() {
  if (edgeN < MAX_EDGES) edgeT[edgeN++] = micros();
}

void scan() {
  struct Band { float from, to; } bands[] = {{314, 316}, {433, 435}, {868, 870}};
  Serial.println("Scan 10 s - hold a remote button / keep sending");
  constexpr int N = 60;
  float peak[3][N];
  for (auto& b : peak) for (float& v : b) v = -127;
  uint32_t t0 = millis();
  while (millis() - t0 < 10000) {
    blinkTick();
    for (int b = 0; b < 3; b++) {
      int i = 0;
      for (float f = bands[b].from; f < bands[b].to && i < N; f += 0.05, i++) {
        ccStrobe(S_IDLE);
        setFreq(f);
        ccStrobe(S_RX);
        delayMicroseconds(1800);
        float r = getRSSI();
        if (r > peak[b][i]) peak[b][i] = r;
      }
    }
  }
  for (int b = 0; b < 3; b++) {
    int best = 0;
    for (int i = 0; i < N; i++) if (peak[b][i] > peak[b][best]) best = i;
    Serial.printf("band %.0f-%.0f: peak %.1f dBm at %.3f MHz\n",
                  bands[b].from, bands[b].to, peak[b][best], bands[b].from + best * 0.05);
  }
  startRx();
}

void capture() {
  Serial.printf("Waiting for RSSI > %d dBm (%.3f MHz, %s)...\n", thrDbm, freqMHz, ook ? "OOK" : "FSK");
  uint32_t t0 = millis();
  while (getRSSI() < thrDbm) {
    if (millis() - t0 > 30000) { Serial.println("timeout"); return; }
    blinkTick();
    delayMicroseconds(200);
  }
  edgeN = 0;
  attachInterrupt(PIN_GDO0, onEdge, CHANGE);
  delay(400);
  detachInterrupt(PIN_GDO0);
  size_t n = edgeN;
  Serial.printf("Captured %u edges (alternating durations in us):\n", (unsigned)n);
  for (size_t i = 1; i < n; i++) {
    Serial.printf("%lu%c", (unsigned long)(edgeT[i] - edgeT[i - 1]), (i % 24 == 0) ? '\n' : ' ');
  }
  Serial.println("\n--end--");
}

uint8_t consoleAddr[3] = {0, 0, 0};  // remote the console v1 commands talk to (set with "v1 id") (the web worker changes v1Addr in between)
uint8_t consoleHdr = 0x15;

Settings cfg;  // the settings in flash; edited from the serial console with "cfg ..." and written with "cfg save"

// "cfg ..." commands: the same settings the setup portal edits, for work at the desk
bool handleCfg(const String& line) {
  if (!line.startsWith("cfg")) return false;
  String arg = line.length() > 4 ? line.substring(4) : "";
  arg.trim();
  int sp = arg.indexOf(' ');
  String what = sp < 0 ? arg : arg.substring(0, sp);
  String val = sp < 0 ? "" : arg.substring(sp + 1);
  val.trim();
  if (what == "show" || what == "") {
    char h1[16];
    formatHeaterId(cfg.heater, h1, sizeof h1);
    Serial.printf("id %s\nssid %s\npass %s\napi %s\nkey %s\nheater %s\nins %d\n", deviceId(), cfg.ssid, cfg.wpass[0] ? "(set)" : "(empty)",
                  cfg.api, cfg.ekey[0] ? "(set)" : "(empty)", h1, cfg.insecure);
  }
  else if (what == "ssid") strlcpy(cfg.ssid, val.c_str(), sizeof cfg.ssid);
  else if (what == "pass") strlcpy(cfg.wpass, val.c_str(), sizeof cfg.wpass);
  else if (what == "api") normalizeApiUrl(val.c_str(), cfg.api, sizeof cfg.api);
  else if (what == "key") strlcpy(cfg.ekey, val.c_str(), sizeof cfg.ekey);
  else if (what == "ins") cfg.insecure = val == "1";
  else if (what == "id") {
    if (!parseHeaterId(val == "-" ? "" : val.c_str(), cfg.heater)) Serial.println("remote id: AABBCC[:HH] or - for none");
  }
  else if (what == "find") {  // wake the remote and press a button; the address heard most often is taken
    HeaterId id;
    int n = 0;
    Serial.println("listening 25 s, use the remote...");
    xSemaphoreGive(radioMux);  // handle() runs with the radio locked, the search takes it itself
    bool ok = controlFindRemote(25, id, &n);
    xSemaphoreTake(radioMux, portMAX_DELAY);
    if (ok) { cfg.heater = id; char b[16]; formatHeaterId(id, b, sizeof b); Serial.printf("found %s (%d frames), not saved yet\n", b, n); }
    else Serial.printf("not found (best candidate %d frames)\n", n);
  }
  else if (what == "save") { settingsSave(cfg); Serial.println("saved, restarting"); Serial.flush(); delay(200); ESP.restart(); }
  else if (what == "erase") { settingsErase(); Serial.println("erased, restarting"); Serial.flush(); delay(200); ESP.restart(); }
  else Serial.println("cfg show|ssid|pass|api|key|id|find|ins|save|erase");
  return true;
}

void handle(String line) {
  line.trim();
  if (handleCfg(line)) return;
  if (line == "portal" || line == "portal lan") { portalStart(cfg, line == "portal lan", true); return; }  // at the console: trusted
  if (line == "restart") { Serial.println("restarting"); Serial.flush(); delay(100); ESP.restart(); }
  if (line == "log") { controlDumpLog(); return; }
  if (line == "wifi") {  // link diagnostics for the console
    uint8_t* b = WiFi.BSSID();
    Serial.printf("wifi status %d, rssi %d dBm, channel %d, tx power %.1f dBm, bssid %02X:%02X:%02X:%02X:%02X:%02X, ip %s, dns %s %s\n", (int)WiFi.status(), WiFi.RSSI(), WiFi.channel(),
                  WiFi.getTxPower() / 4.0f, b ? b[0] : 0, b ? b[1] : 0, b ? b[2] : 0, b ? b[3] : 0, b ? b[4] : 0, b ? b[5] : 0,
                  WiFi.localIP().toString().c_str(), WiFi.dnsIP().toString().c_str(), WiFi.dnsIP(1).toString().c_str());
    return;
  }
  if (line.startsWith("wifitx ")) {  // "wifitx 8.5": WiFi transmit power in dBm for this run (a weak supply sags at full power)
    WiFi.setTxPower((wifi_power_t)(int)(line.substring(7).toFloat() * 4));
    Serial.printf("tx power now %.1f dBm\n", WiFi.getTxPower() / 4.0f);
    return;
  }
  if (line == "verbose 0" || line == "verbose 1") { controlSetVerbose(line == "verbose 1"); Serial.println("ok"); return; }
  if (line == "boot") { Serial.println("rebooting to ROM bootloader"); Serial.flush(); delay(100); usb_persist_restart(RESTART_BOOTLOADER); }
  if (!radioOk) { Serial.println("radio not initialised"); return; }
  if (line.startsWith("v1 ") || line.startsWith("heater1")) {
    if (line.startsWith("v1 ") && !line.startsWith("v1 id ") && !(consoleAddr[0] | consoleAddr[1] | consoleAddr[2])) {
      Serial.println("set the remote first: v1 id AABBCC[:HH]");
      return;
    }
    memcpy(v1Addr, consoleAddr, 3);
    v1Hdr = consoleHdr;
  }
  if (line == "scan") scan();
  else if (line == "cap") capture();
  else if (line == "heater" || line.startsWith("heater ")) {
    uint32_t s = line.length() > 7 ? line.substring(7).toInt() : 60;
    heaterEnter();
    Serial.printf("Listening %lu s, press remote buttons. Any char stops.\n", (unsigned long)s);
    heaterListen(s * 1000);
    heaterLeave();
    Serial.println("Heater mode off");
  }
  else if (line == "heater1" || line.startsWith("heater1 ")) {
    uint32_t s = line.length() > 8 ? line.substring(8).toInt() : 60;
    configureHeaterV1();
    Serial.printf("V1 mode 433.892 MHz 2.4 kBaud, listening %lu s. Any char stops.\n", (unsigned long)s);
    heaterListenV1(s * 1000);
    heaterLeave();
    Serial.println("V1 mode off");
  }
  else if (line == "v1 up" || line == "v1 down" || line == "v1 off" || line == "v1 on yes" || line == "v1 menu" || line == "v1 mode") {
    // seen from the original remote: AB A9 00 00 = up, AB A9 80 00 = down (heater power level +-1),
    // AB AE 00 00 = power off, AB AD 80 00 = power on (starts the heater, so it needs the "yes")
    configureHeaterV1();
    if (line == "v1 off") heaterTxV1(0xAB, 0xAE, 0x00, 0x00, true);
    else if (line == "v1 menu") heaterTxV1(0xAB, 0xAA, 0x80, 0x00, true);  // next info page (short OK)
    else if (line == "v1 mode") heaterTxV1(0xAB, 0xAA, 0x00, 0x00, true);  // manual <-> setpoint mode (long OK)
    else if (line == "v1 on yes") heaterTxV1(0xAB, 0xAD, 0x80, 0x00, true);
    else heaterTxV1(0xAB, 0xA9, line == "v1 down" ? 0x80 : 0x00, 0x00, true);
    Serial.println("reply:");
    heaterListenV1(3000);
    heaterLeave();
  }
  else if (line.startsWith("v1 id ")) {  // pick the remote the v1 commands talk to, e.g. "v1 id A1B2C3:12"
    unsigned a = 0, b = 0, c = 0, h = 0x15;
    if (sscanf(line.c_str() + 6, "%2x%2x%2x:%2x", &a, &b, &c, &h) >= 3) {
      consoleAddr[0] = a; consoleAddr[1] = b; consoleAddr[2] = c; consoleHdr = h;
      Serial.printf("v1 id %02X%02X%02X byte1=%02X\n", consoleAddr[0], consoleAddr[1], consoleAddr[2], consoleHdr);
    } else Serial.println("v1 id AABBCC[:HH]");
  }
  else if (line == "status") controlStatus();
  else if (line == "v1 rawpoll") {  // poll and print every frame heard, whatever its type or address
    configureHeaterV1();
    for (int i = 0; i < 8; i++) {
      heaterTxV1(0xA9, 0x00, 0x00, 0x00, false);
      uint8_t b[12];
      bool got = false;
      uint32_t t0 = millis();
      while (millis() - t0 < 500) {
        if (!v1RawFrame(b, 200)) continue;
        got = true;
        Serial.printf("raw %d:", i);
        for (int k = 0; k < 12; k++) Serial.printf(" %02X", b[k]);
        Serial.printf("  cmd=%02X sub=%02X d1=%02X d2=%02X\n", b[5], b[6], b[7], b[8]);
      }
      if (!got) Serial.printf("raw %d: -\n", i);
      delay(150);
    }
    heaterLeave();
  }
  else if (line == "v1 poll") {
    configureHeaterV1();
    uint32_t t0 = millis();
    int last = -2;
    for (int i = 0; i < 8; i++) { v1Poll(t0, &last); delay(250); }
    heaterLeave();
  }
  else if (line == "v1 restart yes") {
    v1Restart();
    heaterLeave();
  }
  else if (line.startsWith("fh ")) { heaterMHz = line.substring(3).toFloat(); Serial.printf("heater f=%.3f\n", heaterMHz); }
  else if (line.startsWith("addr ")) {
    heaterAddr = strtoul(line.substring(5).c_str(), nullptr, 16);
    haveAddr = true;
    Serial.printf("addr=%08X\n", (unsigned)heaterAddr);
  }
  else if (line == "wake") heaterSend(0x23);
  else if (line.startsWith("send ")) {
    uint8_t cmd = strtoul(line.substring(5).c_str(), nullptr, 16);
    if (cmd == 0x2B && !line.endsWith(" yes")) Serial.println("0x2B is power on/off: 'send 2B yes' to really do it");
    else if (cmd == 0x23 || cmd == 0x24 || cmd == 0x2B || cmd == 0x3C || cmd == 0x3E) heaterSend(cmd);
    else Serial.println("allowed: 23 wake | 24 mode | 3C up | 3E down | 2B power");
  }
  else if (line == "boot") { Serial.println("rebooting to ROM bootloader"); Serial.flush(); delay(100); usb_persist_restart(RESTART_BOOTLOADER); }
  else if (line == "rssi") Serial.printf("%.1f dBm\n", getRSSI());
  else if (line.startsWith("f ")) { freqMHz = line.substring(2).toFloat(); startRx(); Serial.printf("f=%.3f\n", freqMHz); }
  else if (line.startsWith("thr ")) { thrDbm = line.substring(4).toInt(); Serial.printf("thr=%d\n", thrDbm); }
  else if (line == "mode ook") { ook = true; startRx(); Serial.println("OOK"); }
  else if (line == "mode fsk") { ook = false; startRx(); Serial.println("FSK"); }
  else if (line.length()) Serial.println(HELP);
}

void setup() {
  uiSetup();
  pinMode(PIN_GDO0, INPUT);
  pinMode(PIN_GDO2, INPUT);
  Serial.begin(115200);
  Serial.enableReboot(false);  // a DTR/RTS toggle when the host closes the port must not drop the board into the ROM bootloader
  delay(1500);
  initRadio();
  bool configured = settingsLoad(cfg);
  controlSetup(cfg);
  if (configured) cloudSetup(cfg);
  else portalStart(cfg, false);  // nothing to connect to yet: open the setup portal
  Serial.println(HELP);
}

static LedMode ledMode() {
  if (portalActive()) return portalTesting() ? LED_TESTING : LED_PORTAL;
  if (!radioOk) return LED_RADIO_FAIL;
  switch (cloudState()) {
    case CS_ONLINE: return LED_ONLINE;
    case CS_PENDING: return LED_PENDING;
    case CS_REJECTED: return LED_REJECTED;
    case CS_NO_SERVER: return LED_NO_SERVER;
    default: return LED_NO_WIFI;
  }
}

void loop() {
  uiLed(ledMode());
  uiTick();
  portalLoop();
  if (!portalActive()) cloudLoop();
  if (uiPortalRequested() && !portalActive()) portalStart(cfg, false, true);  // the button: the person is at the device
  else if (cloudWifiLostLong() && !portalActive()) portalStart(cfg, false);  // the WiFi settings may be wrong or the router replaced
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    xSemaphoreTake(radioMux, portMAX_DELAY);  // the web worker task uses the radio too
    handle(line);
    xSemaphoreGive(radioMux);
  }
}
