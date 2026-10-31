#include "portal.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "cloud.h"
#include "control.h"

static const IPAddress AP_IP(192, 168, 1, 111);
static const uint32_t IDLE_RESTART_MS = 10UL * 60 * 1000;  // nobody saved anything: leave the portal

static WebServer server(80);
static DNSServer dns;
static Settings current, typed;
static char apName[24];
static bool active = false, lanMode = false;
static uint32_t lastActivity = 0;
static bool needKey = false;     // the form must carry the current enrollment key (see portalStart)
static bool allowHttp = false;   // the user ticked the box: plain HTTP may be used when HTTPS does not work
static volatile bool restartNow = false;  // a failed test of a real controller ends with a restart: no access point comes back

enum Test : uint8_t { T_IDLE, T_RUNNING, T_OK, T_FAIL };
static volatile Test test = T_IDLE;
static String testMsg;
static String testStep;  // what the running test does right now

enum Find : uint8_t { FIND_IDLE, FIND_RUNNING, FIND_OK, FIND_FAIL };
static volatile Find findState = FIND_IDLE;
static char foundId[16] = "";

// The texts of the page come from lang.h (adjacent string literals are joined by the compiler); keep ' and " out of them.
static const char PAGE[] PROGMEM = R"HTML(<!doctype html><html lang=")HTML" P_LANG R"HTML("><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Hinasto</title>
<style>
body{font:16px system-ui,sans-serif;margin:0;background:#0f1a24;color:#e8eef3}
main{max-width:30em;margin:0 auto;padding:1.2em}
h1{font-size:1.3em;margin:.2em 0 .1em;color:#f59e0b}p{color:#9fb0bf;margin:.2em 0 1em;font-size:.9em}
label{display:block;margin:.9em 0 .25em;font-size:.85em;color:#9fb0bf}
input,select{width:100%;box-sizing:border-box;padding:.65em;border-radius:.5em;border:1px solid #2b3d4d;background:#162533;color:#fff;font-size:1em}
button{width:100%;margin-top:1.4em;padding:.8em;border:0;border-radius:.5em;background:#f59e0b;color:#1a1204;font-size:1.05em;font-weight:600}
button.s{margin-top:.5em;padding:.55em;background:#22303d;color:#e8eef3;font-size:.9em;font-weight:500}
input[type=checkbox]{width:auto;margin:.2em .6em 0 0}.chk{display:flex;margin:1.1em 0 0;color:#9fb0bf;font-size:.85em}
button:disabled{opacity:.5}small{display:block;color:#7d8fa0;margin-top:.3em;font-size:.8em}
#r{margin-top:1.2em;padding:.8em;border-radius:.5em;display:none}.ok{background:#12351f;color:#7ee2a0}.er{background:#3a1a1a;color:#ff9b9b}.wk{background:#22303d}
</style></head><body><main>
<h1>Hinasto Controller</h1><p>%ID%</p>
<form id="f" autocomplete="off">
<label>)HTML" P_NETWORK R"HTML(</label>
<select id="sel"><option value="">)HTML" P_SEARCHING R"HTML(</option></select>
<input type="text" name="ssid" id="ssid" placeholder=")HTML" P_SSID_PH R"HTML(" style="display:none;margin-top:.5em">
<button type="button" class="s" id="rescan">)HTML" P_RESCAN R"HTML(</button>
<label>)HTML" P_WIFI_PASS R"HTML(</label><input type="password" name="wpass">
<label>)HTML" P_SERVER R"HTML(</label><input type="text" name="api" placeholder=")HTML" P_SERVER_PH R"HTML(" autocapitalize="none" required>
<small>)HTML" P_SERVER_HINT R"HTML(</small>
<label>)HTML" P_KEY R"HTML(</label><input type="text" name="ekey" autocapitalize="none" required>
<label>)HTML" P_REMOTE R"HTML(</label><input type="text" name="id" id="id" placeholder=")HTML" P_REMOTE_PH R"HTML(" autocapitalize="characters">
<button type="button" class="s" id="find">)HTML" P_FIND R"HTML(</button>
<small>)HTML" P_FIND_HINT R"HTML(</small>
<div id="ck" style="display:none"><label>)HTML" P_CURKEY R"HTML(</label><input type="password" name="curkey" autocapitalize="none"></div>
<label class="chk"><input type="checkbox" name="allowhttp" value="1">)HTML" P_ALLOW_HTTP R"HTML(</label>
<button id="b">)HTML" P_SAVE R"HTML(</button></form><div id="r"></div>
<script>
var $=function(i){return document.getElementById(i)},f=$('f'),r=$('r'),b=$('b'),sel=$('sel'),ss=$('ssid');
function show(c,t){r.className=c;r.textContent=t;r.style.display='block'}
function J(u,o){var c=new AbortController();setTimeout(function(){c.abort()},6000);return fetch(u,Object.assign({signal:c.signal},o)).then(function(x){return x.json()})}
function nets(re){J('/scan'+(re?'?rescan=1':'')).then(function(l){
if(l.scanning){setTimeout(nets,1500);return}
sel.innerHTML='';
l.nets.forEach(function(n){var o=document.createElement('option');o.value=n.s;o.textContent=n.s+'  ('+(n.r>-60?')HTML" P_SIG_STRONG R"HTML(':n.r>-75?')HTML" P_SIG_MED R"HTML(':')HTML" P_SIG_WEAK R"HTML(')+(n.l?')HTML" P_SECURED R"HTML(':'')+')';sel.appendChild(o)});
var o=document.createElement('option');o.value='';o.textContent=')HTML" P_OTHER_NET R"HTML(';sel.appendChild(o);
sel.onchange();
}).catch(function(){setTimeout(nets,3000)})}
sel.onchange=function(){ss.style.display=sel.value?'none':'block';if(sel.value)ss.value=sel.value}
$('rescan').onclick=function(){sel.innerHTML='<option>)HTML" P_SEARCHING R"HTML(</option>';nets(1)};
nets();
var NEEDKEY=%NEEDKEY%;if(NEEDKEY)$('ck').style.display='block';
var fp;
$('find').onclick=function(){var fb=$('find');fb.disabled=true;show('wk',')HTML" P_LISTENING R"HTML(');
J('/find',{method:'POST'}).then(function(){fp=setInterval(function(){J('/findstatus').then(function(s){
if(s.s=='running')return;clearInterval(fp);fb.disabled=false;
if(s.s=='ok'){$('id').value=s.id;show('ok',')HTML" P_FOUND R"HTML('+s.id)}else show('er',s.m)})},1000)})};
var MSG=')HTML" P_DONE R"HTML(';
function done(){f.style.display='none';show('ok',MSG)}
f.onsubmit=function(e){e.preventDefault();if(sel.value)ss.value=sel.value;b.disabled=true;show('wk',')HTML" P_SENDING R"HTML(');
J('/save',{method:'POST',body:new URLSearchParams(new FormData(f))}).then(function(s){
if(s.s=='bad'){show('er',s.m);b.disabled=false}else done()
}).catch(done)};</script></main></body></html>)HTML";

static String esc(const char* s) {
  String o;
  for (; *s; s++) {
    if (*s == '&') o += "&amp;";
    else if (*s == '<') o += "&lt;";
    else if (*s == '>') o += "&gt;";
    else if (*s == '"') o += "&quot;";
    else o += *s;
  }
  return o;
}

static String jsonText(const char* s, const String& m) {
  String o = String("{\"s\":\"") + s + "\",\"m\":\"";
  jsonEscape(o, m.c_str());
  return o + "\"}";
}

static void sendPage() {
  lastActivity = millis();
  String p = FPSTR(PAGE);
  p.replace("%ID%", esc(deviceId()));  // the form itself always starts empty
  p.replace("%AP%", esc(apName));
  p.replace("%NEEDKEY%", needKey ? "1" : "0");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", p);
}

// ---- WiFi scan ----

static void handleScan() {
  lastActivity = millis();
  if (server.hasArg("rescan")) WiFi.scanDelete();
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) { server.send(200, "application/json", "{\"scanning\":true}"); return; }
  if (n < 0) {  // none yet
    WiFi.scanNetworks(true);
    server.send(200, "application/json", "{\"scanning\":true}");
    return;
  }
  String o = "{\"nets\":[";
  bool first = true;
  for (int i = 0; i < n && i < 25; i++) {  // the scan result is sorted by signal, strongest first
    String ssid = WiFi.SSID(i);
    if (!ssid.length()) continue;
    bool dup = false;
    for (int j = 0; j < i; j++) if (WiFi.SSID(j) == ssid) dup = true;
    if (dup) continue;
    if (!first) o += ',';
    first = false;
    o += "{\"s\":\"";
    jsonEscape(o, ssid.c_str());
    o += "\",\"r\":" + String(WiFi.RSSI(i)) + ",\"l\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "0" : "1") + "}";
  }
  server.send(200, "application/json", o + "]}");
}

// ---- find the remote ----

static void findTask(void*) {
  HeaterId id;
  int heard = 0;
  if (controlFindRemote(25, id, &heard)) {
    formatHeaterId(id, foundId, sizeof foundId);
    findState = FIND_OK;
  } else {
    findState = FIND_FAIL;
  }
  vTaskDelete(nullptr);
}

static void handleFind() {
  lastActivity = millis();
  if (findState != FIND_RUNNING) {
    findState = FIND_RUNNING;
    xTaskCreate(findTask, "findremote", 4096, nullptr, 1, nullptr);
  }
  server.send(200, "application/json", "{\"s\":\"running\"}");
}

static void handleFindStatus() {
  lastActivity = millis();
  if (findState == FIND_RUNNING) server.send(200, "application/json", "{\"s\":\"running\"}");
  else if (findState == FIND_OK) server.send(200, "application/json", String("{\"s\":\"ok\",\"id\":\"") + foundId + "\"}");
  else server.send(200, "application/json", jsonText("fail", P_E_FIND_FAIL));
}

// ---- the test: WiFi, clock, server ----

static const uint32_t TEST_STACK = 8192;  // measured: the TLS handshake uses about 5 kB

static void testFail(const String& msg) {
  testMsg = msg;
  test = T_FAIL;
  restartNow = !lanMode;  // the settings were not saved: the loop task restarts and the controller goes on as before
  cloudHold(false);
  vTaskDelete(nullptr);
}

static void testTask(void*) {
  Settings s = typed;
  cloudHold(true);
  vTaskDelay(pdMS_TO_TICKS(2500));  // the answer to the form and the page message must reach the phone before its network goes away
  bool already = WiFi.status() == WL_CONNECTED && WiFi.SSID() == String(s.ssid) && !strcmp(s.wpass, current.wpass);  // opened with the button while online: keep the link
  // The access point lives in the subnet 192.168.1.x like most home networks and would hide a server on that
  // network, so it is off while the test runs (the phone loses it for a moment, the result waits on this page).
  if (!lanMode) { WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA); }
  testStep = String(P_STEP_WIFI_A) + s.ssid + P_STEP_WIFI_B;
  if (!already) {
    WiFi.disconnect();
    WiFi.begin(s.ssid, s.wpass);
  }
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 25000) vTaskDelay(pdMS_TO_TICKS(250));
  if (WiFi.status() != WL_CONNECTED) {
    wl_status_t st = WiFi.status();
    testFail(st == WL_NO_SSID_AVAIL ? P_E_NO_SSID : P_E_WIFI_FAIL);
  }
  wlog("test: WiFi %s, IP %s", s.ssid, WiFi.localIP().toString().c_str());

  // the user typed only a host: try HTTPS with a full certificate check first, then plain HTTP
  char host[96];
  strlcpy(host, s.api + 8, sizeof host);
  testStep = P_STEP_TIME;
  bool clock = cloudSyncTime(12000) || cloudTimeFromServer(s);
  String msg;
  int code = -1;
  testStep = P_STEP_SERVER;
  if (clock) {
    code = cloudProbe(s, msg);
    if (code > 0) msg += P_VIA_HTTPS;
  }
  if (code <= 0 && allowHttp) {  // only when the user asked for it: plain HTTP sends the secret in the clear
    Settings plain = s;
    snprintf(plain.api, sizeof plain.api, "http://%s", host);
    String msg2;
    int code2 = cloudProbe(plain, msg2);
    if (code2 > 0) {
      s = plain;
      code = code2;
      msg = msg2 + P_VIA_HTTP;
    } else if (!clock) {
      msg = P_E_NO_TIME_HTTP + msg2;
    }
  } else if (code <= 0) {
    msg = String(clock ? msg : String(P_E_NO_TIME)) + P_E_HTTPS_HINT;  // msg holds the reason the HTTPS probe failed
  }
  wlog(L_TEST_STACK, (unsigned)uxTaskGetStackHighWaterMark(nullptr), (unsigned)TEST_STACK);
  testMsg = msg;
  if (code == 200) {
    settingsSave(s);
    wlog(L_TEST_OK);
    test = T_OK;
  } else {
    wlog(L_TEST_FAIL, msg.c_str());
    testFail(msg);
  }
  vTaskDelete(nullptr);
}

static void copyArg(const char* name, char* dst, size_t n) {
  String v = server.arg(name);
  v.trim();
  strlcpy(dst, v.c_str(), n);
}

// compares two texts in a time that does not depend on where they differ
static bool sameText(const char* a, const char* b) {
  size_t la = strlen(a), lb = strlen(b);
  uint8_t diff = la != lb;
  for (size_t i = 0; i < la && i < lb; i++) diff |= (uint8_t)(a[i] ^ b[i]);
  return diff == 0;
}

static void handleSave() {
  lastActivity = millis();
  if (test == T_RUNNING) { server.send(200, "application/json", jsonText("running", testStep)); return; }
  if (needKey) {  // opened by itself on a configured controller: the form must prove that its sender knows the key
    String given = server.arg("curkey");
    given.trim();
    if (!sameText(given.c_str(), current.ekey)) {
      delay(1500);  // slows down guessing
      server.send(200, "application/json", jsonText("bad", P_E_CURKEY));
      return;
    }
  }
  allowHttp = server.hasArg("allowhttp");
  Settings s = current;
  copyArg("ssid", s.ssid, sizeof s.ssid);
  strlcpy(s.wpass, server.arg("wpass").c_str(), sizeof s.wpass);  // may be empty: an open network
  char typedHost[96], host[96];
  copyArg("api", typedHost, sizeof typedHost);
  if (!normalizeHost(typedHost, host, sizeof host)) { server.send(200, "application/json", jsonText("bad", P_E_HOST)); return; }
  const char* oldHost = strstr(current.api, "://");
  oldHost = oldHost ? oldHost + 3 : current.api;
  snprintf(s.api, sizeof s.api, "https://%s", host);  // HTTPS unless the user allowed HTTP and HTTPS did not work
  // The secret belongs to one server. A controller sent to another host gets a new one (it has to be approved there
  // like a new controller); the old secret never reaches a host that was not its own.
  if (current.api[0] && strcmp(oldHost, host) != 0) settingsNewSecret(s);
  else settingsEnsureSecret(s);
  String key = server.arg("ekey");
  key.trim();
  strlcpy(s.ekey, key.c_str(), sizeof s.ekey);
  s.insecure = false;
  char h[16];
  copyArg("id", h, sizeof h);
  if (!parseHeaterId(h, s.heater)) { server.send(200, "application/json", jsonText("bad", P_E_ID)); return; }
  if (!s.ssid[0]) { server.send(200, "application/json", jsonText("bad", P_E_SSID)); return; }
  if (!s.ekey[0]) { server.send(200, "application/json", jsonText("bad", P_E_KEY)); return; }
  typed = s;
  test = T_RUNNING;
  testMsg = "";
  testStep = P_STEP_START;
  if (xTaskCreate(testTask, "setuptest", TEST_STACK, nullptr, 1, nullptr) != pdPASS) {
    test = T_FAIL;
    testMsg = P_E_MEM;
    wlog(L_TEST_NOMEM, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    server.send(200, "application/json", jsonText("bad", testMsg));
    return;
  }
  server.send(200, "application/json", jsonText("running", testStep));
}

static void handleStatus() {
  lastActivity = millis();
  if (test == T_RUNNING) server.send(200, "application/json", jsonText("running", testStep));
  else if (test == T_OK) server.send(200, "application/json", jsonText("ok", testMsg + P_OK_SAVED));
  else if (test == T_FAIL) server.send(200, "application/json", jsonText("fail", testMsg + P_FAIL_NOSAVE));
  else server.send(200, "application/json", jsonText("idle", ""));
}

static void startAp() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(apName);  // open: nothing secret crosses it except what the owner types for a minute
  dns.stop();
  dns.start(53, "*", AP_IP);
  WiFi.scanDelete();
  WiFi.scanNetworks(true);
  lastActivity = millis();
}

void portalStart(const Settings& cur, bool lan, bool trusted) {
  current = cur;
  lanMode = lan;
  needKey = cur.ekey[0] && !trusted;
  allowHttp = false;
  test = T_IDLE;
  lastActivity = millis();
  if (lan) {
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.mode(WIFI_STA);
      WiFi.begin(cur.ssid, cur.wpass);
    }
    Serial.printf("portal on the home WiFi: http://%s/\n", WiFi.localIP().toString().c_str());
  } else {
    snprintf(apName, sizeof apName, "Hinasto-%s", deviceId() + strlen(deviceId()) - 4);
    startAp();
    Serial.printf("portal: join WiFi \"%s\", open http://192.168.1.111/\n", apName);
  }
  server.on("/", HTTP_GET, sendPage);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/find", HTTP_POST, handleFind);
  server.on("/findstatus", HTTP_GET, handleFindStatus);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/status", HTTP_GET, handleStatus);
  server.onNotFound([]() {  // captive portal: every other address (phone connectivity checks) lands on the form
    server.sendHeader("Location", String("http://") + (lanMode ? WiFi.localIP().toString() : String("192.168.1.111")) + "/");
    server.send(302, "text/plain", "");
  });
  server.begin();
  active = true;
  wlog(L_PORTAL_OPEN, lan ? L_PORTAL_LAN : "");
}

void portalLoop() {
  if (!active) return;
  if (restartNow) { delay(500); ESP.restart(); }
  if (!lanMode) dns.processNextRequest();
  server.handleClient();
  if (test == T_OK) {  // the access point is already off; give the log a moment, then start over with the saved settings
    static uint32_t okAt = 0;
    if (!okAt) okAt = millis();
    if (millis() - okAt > 2000) { Serial.println("settings saved, restarting"); delay(100); ESP.restart(); }
  }
  if (test != T_RUNNING && millis() - lastActivity > IDLE_RESTART_MS && current.ekey[0]) ESP.restart();  // configured device: back to work
}

bool portalActive() { return active; }
bool portalTesting() { return test == T_RUNNING; }
