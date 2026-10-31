// Setup portal: the controller opens its own WiFi "Hinasto-XXXX"; a phone joins it and opens http://192.168.1.111,
// where a one-page form takes the home WiFi, the server address and the enrollment key. The entered values are
// tested first (WiFi, then one request to the server); only a passing test is saved, then the controller restarts
// and connects to the server with the AP gone.
#pragma once
#include <Arduino.h>
#include "settings.h"

// lan=true: no access point; the page is served on the IP the controller already has on the home WiFi
// (for testing from a computer; WiFi must already be configured)
// trusted: the person is at the device (BOOT held, serial console). A controller that is already configured but whose portal opened by itself
// (WiFi lost for minutes) only accepts a form that carries its current enrollment key: the access point is open, anybody in range could
// otherwise point the controller, with its secret, at a server of their own. An unconfigured controller needs no key.
void portalStart(const Settings& current, bool lan, bool trusted = false);
void portalLoop();
bool portalActive();
bool portalTesting();
