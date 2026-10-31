// Everything the controller needs to know that is not in the firmware: kept in flash (NVS), set through the setup portal.
#pragma once
#include <Arduino.h>

struct HeaterId {
  bool on = false;          // is there a heater on this slot
  uint8_t addr[3] = {0, 0, 0};
  uint8_t hdr = 0x15;       // second frame byte of this heater's remote
};

struct Settings {
  char ssid[33] = "";
  char wpass[65] = "";
  char api[96] = "";        // base URL of the server without a trailing slash, e.g. https://hinasto.example.com
  char ekey[48] = "";       // enrollment key shown by the panel
  char secret[48] = "";     // generated once; the server only keeps its hash
  bool insecure = false;    // do not verify the TLS certificate (serial console only)
  HeaterId heater;          // one controller drives one heater
};

// true when WiFi, server and key are all set. The secret may still be empty: settingsEnsureSecret makes it once the radio runs.
bool settingsLoad(Settings& s);
bool settingsSave(const Settings& s);
// The secret of this controller towards one server: 32 hex characters from the hardware random generator. That generator is only
// truly random while the radio runs, so call these with WiFi started (the portal and cloudSetup do).
void settingsNewSecret(Settings& s);
bool settingsEnsureSecret(Settings& s);  // makes one when there is none, true when it did
// forgets WiFi, server, key and heater, and the secret too (the panel will see a new controller)
void settingsErase();                  // factory reset: also forgets smart mode
void settingsClearSmart();             // forget smart mode (on/off, target, what it believes about the heater)

const char* deviceId();  // hinasto-<MAC>, the same on every boot

// "A1B2C3" or "A1B2C3:15"; an empty text means "no heater" and gives on=false
bool parseHeaterId(const char* text, HeaterId& out);
void formatHeaterId(const HeaterId& h, char* out, size_t n);

// what a person typed as the server (a domain or an IP, nothing else is needed): drops a scheme, a port and a path.
// out gets the bare host; false when nothing usable is left
bool normalizeHost(const char* typed, char* out, size_t n);
// "https://host/path" typed on the serial console: adds https:// when missing, drops spaces and a trailing slash
void normalizeApiUrl(const char* typed, char* out, size_t n);
