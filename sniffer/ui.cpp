#include "ui.h"
#include "settings.h"

static const int PIN_LED = 15;  // onboard LED of the S2 mini
static const int PIN_BTN = 0;   // BOOT button, low when pressed (do not hold it while resetting: that is the ROM bootloader)
static const uint32_t HOLD_PORTAL_MS = 3000, HOLD_ERASE_MS = 10000;

static volatile LedMode mode = LED_BOOT;
static uint32_t pressedAt = 0;
static bool wasDown = false;
static volatile bool portalReq = false;

void uiSetup() {
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BTN, INPUT_PULLUP);
  digitalWrite(PIN_LED, HIGH);
}

void uiLed(LedMode m) { mode = m; }

bool uiPortalRequested() {
  bool r = portalReq;
  portalReq = false;
  return r;
}

// n short flashes at the start of every 2 s
static bool flashes(uint32_t t, int n) {
  uint32_t p = t % 2000;
  return p < (uint32_t)n * 300 && (p % 300) < 120;
}

static void button(uint32_t t) {
  bool down = digitalRead(PIN_BTN) == LOW;
  if (down && !wasDown) pressedAt = t;
  if (!down && wasDown) {
    if (t - pressedAt >= HOLD_PORTAL_MS && t - pressedAt < HOLD_ERASE_MS) portalReq = true;
  }
  wasDown = down;
  if (down && t - pressedAt >= HOLD_ERASE_MS) {  // still held after 10 s: factory reset
    digitalWrite(PIN_LED, LOW);
    settingsErase();
    Serial.println("settings erased, restarting");
    Serial.flush();
    delay(200);
    ESP.restart();
  }
}

void uiTick() {
  uint32_t t = millis();
  button(t);
  bool on;
  if (wasDown) {  // feedback while the button is held: steady -> fast blink once 3 s are reached (let go now) -> off at 10 s
    uint32_t held = t - pressedAt;
    on = held < HOLD_PORTAL_MS ? true : (t % 200) < 100;
  } else {
    switch (mode) {
      case LED_ONLINE: on = flashes(t, 1); break;
      case LED_PENDING: on = flashes(t, 2); break;
      case LED_NO_SERVER: on = flashes(t, 3); break;
      case LED_NO_WIFI: on = flashes(t, 4); break;
      case LED_REJECTED: on = (t % 1000) < 500; break;
      case LED_PORTAL: on = (t % 200) < 100; break;
      case LED_TESTING: on = true; break;
      case LED_RADIO_FAIL: on = (t % 100) < 50; break;
      default: on = true; break;
    }
  }
  digitalWrite(PIN_LED, on);
}
