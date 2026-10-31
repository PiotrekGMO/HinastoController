#include "settings.h"
#include <Preferences.h>
#include <esp_mac.h>

static const char* NS = "hinasto";

const char* deviceId() {
  static char id[24] = "";
  if (!id[0]) {
    uint8_t m[6];
    esp_efuse_mac_get_default(m);
    snprintf(id, sizeof id, "hinasto-%02X%02X%02X%02X%02X%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  }
  return id;
}

bool parseHeaterId(const char* text, HeaterId& out) {
  String t(text);
  t.trim();
  if (t.length() == 0) { out = HeaterId(); return true; }
  uint8_t hdr = 0x15;
  int colon = t.indexOf(':');
  String a = colon >= 0 ? t.substring(0, colon) : t;
  if (colon >= 0) {
    String h = t.substring(colon + 1);
    if (h.length() != 2 || !isxdigit(h[0]) || !isxdigit(h[1])) return false;
    hdr = (uint8_t)strtoul(h.c_str(), nullptr, 16);
  }
  if (a.length() != 6) return false;
  for (int i = 0; i < 6; i++) if (!isxdigit(a[i])) return false;
  out.on = true;
  out.hdr = hdr;
  for (int i = 0; i < 3; i++) out.addr[i] = (uint8_t)strtoul(a.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
  return true;
}

void formatHeaterId(const HeaterId& h, char* out, size_t n) {
  if (!h.on) { out[0] = 0; return; }
  snprintf(out, n, "%02X%02X%02X:%02X", h.addr[0], h.addr[1], h.addr[2], h.hdr);
}

bool normalizeHost(const char* typed, char* out, size_t n) {
  String u(typed);
  u.trim();
  u.replace(" ", "");
  int s = u.indexOf("://");
  if (s >= 0) u = u.substring(s + 3);
  int slash = u.indexOf('/');
  if (slash >= 0) u = u.substring(0, slash);
  int colon = u.lastIndexOf(':');
  if (colon >= 0 && u.indexOf(']') < colon) u = u.substring(0, colon);  // a port; an IPv6 literal is not supported anyway
  for (size_t i = 0; i < u.length(); i++) {
    char c = u[i];
    if (!isalnum((unsigned char)c) && c != '.' && c != '-') { u = ""; break; }
  }
  strlcpy(out, u.c_str(), n);
  return u.length() > 0;
}

void normalizeApiUrl(const char* typed, char* out, size_t n) {
  String u(typed);
  u.trim();
  u.replace(" ", "");
  if (u.length() && u.indexOf("://") < 0) u = "https://" + u;
  while (u.endsWith("/")) u.remove(u.length() - 1);
  strlcpy(out, u.c_str(), n);
}

bool settingsLoad(Settings& s) {
  Preferences p;
  p.begin(NS, false);
  s = Settings();
  p.getString("ssid", s.ssid, sizeof s.ssid);
  p.getString("wpass", s.wpass, sizeof s.wpass);
  p.getString("api", s.api, sizeof s.api);
  p.getString("ekey", s.ekey, sizeof s.ekey);
  p.getString("secret", s.secret, sizeof s.secret);
  s.insecure = p.getBool("ins", false);
  char buf[16] = "";
  p.getString("h1", buf, sizeof buf);
  parseHeaterId(buf, s.heater);
  p.end();
  return s.ssid[0] && s.api[0] && s.ekey[0];
}

static void fillSecret(char* out) {  // 16 random bytes as 32 hex characters
  uint8_t r[16];
  esp_fill_random(r, sizeof r);
  for (int i = 0; i < 16; i++) snprintf(out + i * 2, 3, "%02x", r[i]);
}

void settingsNewSecret(Settings& s) { fillSecret(s.secret); }

bool settingsEnsureSecret(Settings& s) {
  if (s.secret[0]) return false;
  fillSecret(s.secret);
  return true;
}

bool settingsSave(const Settings& s) {
  Preferences p;
  if (!p.begin(NS, false)) return false;
  p.putString("ssid", s.ssid);
  p.putString("wpass", s.wpass);
  p.putString("api", s.api);
  p.putString("ekey", s.ekey);
  p.putString("secret", s.secret);
  p.putBool("ins", s.insecure);
  char buf[16], old[16] = "";
  formatHeaterId(s.heater, buf, sizeof buf);
  p.getString("h1", old, sizeof old);
  p.putString("h1", buf);
  p.end();
  if (strcmp(old, buf) != 0) settingsClearSmart();  // another heater: smart mode of the old one must not start it
  return true;
}

void settingsClearSmart() {
  Preferences p;
  if (p.begin("hnsmart", false)) {  // the namespace of smart mode (control.cpp)
    p.clear();
    p.end();
  }
}

void settingsErase() {
  Preferences p;
  p.begin(NS, false);
  p.clear();
  p.end();
  settingsClearSmart();  // a factory reset must not leave smart mode switched on
}
