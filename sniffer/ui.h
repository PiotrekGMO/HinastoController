// The only user interface of the controller itself: the BOOT button (GPIO0) and the LED (GPIO15).
#pragma once
#include <Arduino.h>

// What the LED tells (patterns repeat every 2 s unless said otherwise):
//   1 flash  - connected to the server and approved
//   2 flashes - connected, waiting for approval in the panel
//   3 flashes - WiFi works, the server does not answer
//   4 flashes - no WiFi
//   slow blink (1 Hz) - the panel rejected this controller
//   fast blink (5 Hz) - setup portal is open, waiting for the phone
//   steady on - the portal is testing the entered settings
//   very fast blink (10 Hz) - the radio (CC1101) did not start
enum LedMode : uint8_t { LED_BOOT, LED_ONLINE, LED_PENDING, LED_NO_SERVER, LED_NO_WIFI, LED_REJECTED, LED_PORTAL, LED_TESTING, LED_RADIO_FAIL };

void uiSetup();
void uiTick();               // call often (the radio code calls it from its long loops through blinkTick)
void uiLed(LedMode m);

// Button: hold 3 s and let go -> true once (open the setup portal). Hold 10 s -> forget all settings and restart.
bool uiPortalRequested();
