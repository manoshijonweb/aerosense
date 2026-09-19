#include "WifiPortal.h"
#include "FirebaseManager.h"
#include <WiFi.h>

namespace {

// The page the phone gets. Kept in PROGMEM and deliberately plain: it has to
// render on whatever browser the captive-portal sheet happens to use, which on
// older Android is not a modern one. No external requests of any kind -- the
// device is not on the internet yet, so anything fetched from a CDN would hang.
const char PORTAL_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>AeroSense Wi-Fi</title><style>
*{box-sizing:border-box}body{margin:0;padding:18px;font:16px system-ui,-apple-system,"Segoe UI",sans-serif;
background:#14161a;color:#f2f3f5}h1{font-size:19px;margin:0 0 4px}p.sub{margin:0 0 18px;color:#9aa1ab;font-size:14px}
ul{list-style:none;margin:0 0 16px;padding:0;border:1px solid #2b2f36;border-radius:10px;overflow:hidden}
li{padding:13px 14px;border-bottom:1px solid #2b2f36;display:flex;align-items:center;gap:10px;cursor:pointer}
li:last-child{border-bottom:0}li.sel{background:#1d5cab}li .n{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
li .m{color:#9aa1ab;font-size:13px}li.sel .m{color:#dce6f5}
input,button{width:100%;padding:13px;font:inherit;border-radius:10px;border:1px solid #2b2f36}
input{background:#0e1013;color:#f2f3f5;margin-bottom:12px}
button{background:#2a78d6;color:#fff;border-color:#2a78d6;font-weight:600}
button[disabled]{opacity:.5}#msg{margin-top:14px;font-size:14px;min-height:20px}
.ok{color:#5ed17a}.bad{color:#ff8a8a}.mut{color:#9aa1ab}
</style></head><body>
<h1>AeroSense Wi-Fi</h1><p class="sub">Pick a network for the monitor to use.</p>
<ul id="list"><li class="mut">Scanning...</li></ul>
<input id="pw" type="password" placeholder="Wi-Fi password" autocomplete="off">
<button id="go" disabled>Connect</button><div id="msg"></div>
<script>
var sel=null;
function load(){fetch('/scan').then(function(r){return r.json()}).then(function(n){
var u=document.getElementById('list');u.innerHTML='';
if(!n.length){u.innerHTML='<li class="mut">No networks found</li>';return}
n.forEach(function(x){var li=document.createElement('li');
li.innerHTML='<span class="n"></span><span class="m">'+x.r+' dBm'+(x.l?' &#128274;':'')+'</span>';
li.querySelector('.n').textContent=x.s;
li.onclick=function(){sel=x.s;
[].forEach.call(u.children,function(c){c.className=''});li.className='sel';
document.getElementById('go').disabled=false;
document.getElementById('pw').placeholder=x.l?'Wi-Fi password':'No password needed';};
u.appendChild(li)})})}
document.getElementById('go').onclick=function(){
if(!sel)return;var b=this;b.disabled=true;
document.getElementById('msg').className='mut';
document.getElementById('msg').textContent='Connecting...';
fetch('/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
body:'s='+encodeURIComponent(sel)+'&p='+encodeURIComponent(document.getElementById('pw').value)})
.then(function(){poll(b)})};
function poll(b){fetch('/status').then(function(r){return r.json()}).then(function(j){
var m=document.getElementById('msg');
if(j.state=='SUCCESS'){m.className='ok';m.textContent='Connected. The monitor is on '+j.ssid+' - you can close this page.';return}
if(j.state=='FAILED'){m.className='bad';m.textContent='Could not connect. Check the password, and that the network is 2.4 GHz.';b.disabled=false;return}
setTimeout(function(){poll(b)},1200)})}
load();
</script></body></html>)HTML";

const char *stateName(PortalState s) {
  switch (s) {
    case PortalState::OFF:        return "OFF";
    case PortalState::STARTING:   return "STARTING";
    case PortalState::WAITING:    return "WAITING";
    case PortalState::JOINED:     return "JOINED";
    case PortalState::CONNECTING: return "CONNECTING";
    case PortalState::SUCCESS:    return "SUCCESS";
    case PortalState::FAILED:     return "FAILED";
  }
  return "?";
}

// Minimal JSON string escaping, into a caller-supplied buffer. SSIDs are
// arbitrary bytes and routinely contain quotes and backslashes; one unescaped
// quote breaks the whole list rather than one entry, so this is not optional.
void jsonEscapeInto(const char *in, char *out, size_t n) {
  size_t o = 0;
  for (const char *p = in; *p && o + 2 < n; ++p) {
    if (*p == '"' || *p == '\\') { out[o++] = '\\'; out[o++] = *p; }
    else if ((uint8_t)*p < 0x20)  { out[o++] = ' '; }
    else out[o++] = *p;
  }
  out[o] = '\0';
}

}  // namespace

// Asking for a session does not start one. The uploader may be inside a
// Firebase call right now, and changing the radio underneath it leaves that
// call spinning on core 0 until the task watchdog resets the device. So this
// only raises the request; update() completes it once the coast is clear.
void WifiPortal::start() {
  if (!_wifi) return;
  if (_cloud) _cloud->setPaused(true);
  _target[0] = '\0';
  _startedMs = millis();
  _state = PortalState::STARTING;
  Serial.println(F("[portal] requested; waiting for the uploader to stand down"));
}

void WifiPortal::beginSession() {
  _wifi->ensureStarted();

  // AP_STA, not AP: the station side has to stay available so the attempt can
  // run while the phone is still holding its connection to this page.
  WiFi.mode(WIFI_AP_STA);
  const char *pw = (strlen(WIFI_AP_PASSWORD) >= 8) ? WIFI_AP_PASSWORD : nullptr;
  WiFi.softAP(WIFI_AP_SSID, pw);

  // Answer every DNS query with our own address, which is what makes phones
  // pop the page up by themselves instead of the user typing an IP.
  _dns.setErrorReplyCode(DNSReplyCode::NoError);
  _dns.start(53, "*", WiFi.softAPIP());

  routes();
  _http.begin();

  _lastScanMs = 0;
  _state = PortalState::WAITING;
  _wifi->clearManualResult();
  _wifi->startScan();

  Serial.printf("[portal] AP \"%s\" up at %s\n", WIFI_AP_SSID,
                WiFi.softAPIP().toString().c_str());
}

void WifiPortal::stop() {
  if (_state == PortalState::OFF) return;
  _http.stop();
  _dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  _state = PortalState::OFF;
  if (_cloud) _cloud->setPaused(false);
  Serial.println(F("[portal] closed"));
}

uint16_t WifiPortal::secondsLeft() const {
  if (_state == PortalState::OFF) return 0;
  uint32_t elapsed = millis() - _startedMs;
  if (elapsed >= WIFI_PORTAL_TIMEOUT_MS) return 0;
  return (uint16_t)((WIFI_PORTAL_TIMEOUT_MS - elapsed) / 1000);
}

uint8_t WifiPortal::clients() const { return WiFi.softAPgetStationNum(); }

void WifiPortal::update() {
  if (_state == PortalState::OFF) return;

  if (_state == PortalState::STARTING) {
    // Wait for the uploader to leave the SDK. The timeout is a deliberate
    // escape hatch: if it is already wedged, the session is the user's way out
    // of a broken network, so it must not be blocked forever by the very
    // problem it exists to fix.
    bool clear = !_cloud || !_cloud->busy();
    if (clear || millis() - _startedMs > 8000) {
      if (!clear) Serial.println(F("[portal] uploader still busy -- starting anyway"));
      beginSession();
    }
    return;
  }

  _dns.processNextRequest();
  _http.handleClient();

  // Rescan only while nobody has joined yet. A scan makes the radio hop
  // channels, which stalls the access point for a second or two -- harmless
  // when there is no phone on it, but it drops the page mid-use otherwise.
  if (_state == PortalState::WAITING &&
      millis() - _lastScanMs > 15000 && _wifi->scanStatus() >= 0) {
    _lastScanMs = millis();
    _wifi->startScan();
  }

  if (_state == PortalState::WAITING && clients() > 0) _state = PortalState::JOINED;
  else if (_state == PortalState::JOINED && clients() == 0) _state = PortalState::WAITING;

  if (_state == PortalState::CONNECTING) {
    ManualResult r = _wifi->manualResult();
    if (r == ManualResult::OK) { _state = PortalState::SUCCESS; _successMs = millis(); }
    else if (r == ManualResult::FAILED) _state = PortalState::FAILED;
  }

  // Hold the AP open for a few seconds after success so the phone's next poll
  // still finds it and can say "connected" -- tearing it down the instant the
  // station joins leaves the page hanging on a dead socket instead.
  if (_state == PortalState::SUCCESS && millis() - _successMs > 6000) {
    stop();
    return;
  }
  if (millis() - _startedMs >= WIFI_PORTAL_TIMEOUT_MS) {
    Serial.println(F("[portal] timed out"));
    stop();
  }
}

void WifiPortal::routes() {
  _http.on("/", HTTP_GET, [this]() { handleRoot(); });
  _http.on("/scan", HTTP_GET, [this]() { handleScan(); });
  _http.on("/connect", HTTP_POST, [this]() { handleConnect(); });
  _http.on("/status", HTTP_GET, [this]() { handleStatus(); });
  _http.onNotFound([this]() { handleNotFound(); });
}

void WifiPortal::handleRoot() {
  _http.sendHeader("Cache-Control", "no-store");
  _http.send_P(200, "text/html", PORTAL_HTML);
}

void WifiPortal::handleScan() {
  // Built into a fixed buffer rather than by String concatenation. Growing a
  // String here would reallocate a few dozen times, fragmenting the heap at the
  // exact moment the AP, the DNS server and the web server all want a
  // contiguous block of it.
  static char out[WIFI_PORTAL_SCAN_MAX * 64 + 4];
  size_t len = 0;
  out[len++] = '[';

  int n = _wifi->scanStatus();
  if (n > 0) {
    if (n > WIFI_PORTAL_SCAN_MAX) n = WIFI_PORTAL_SCAN_MAX;
    for (int i = 0; i < n; i++) {
      char ssid[33];
      _wifi->scanSsid(i, ssid, sizeof(ssid));
      if (!ssid[0]) continue;                 // hidden networks cannot be picked

      char esc[70];
      jsonEscapeInto(ssid, esc, sizeof(esc));

      char row[128];
      int w = snprintf(row, sizeof(row), "%s{\"s\":\"%s\",\"r\":%d,\"l\":%d}",
                       (len > 1 ? "," : ""), esc,
                       (int)_wifi->scanRssi(i), _wifi->scanLocked(i) ? 1 : 0);
      if (w <= 0 || len + (size_t)w + 2 >= sizeof(out)) break;   // never overrun
      memcpy(out + len, row, w);
      len += w;
    }
  } else if (n == -2) {
    _wifi->startScan();                        // never started; kick one off
  }
  out[len++] = ']';
  out[len] = '\0';

  _http.sendHeader("Cache-Control", "no-store");
  _http.send(200, "application/json", out);
}

void WifiPortal::handleConnect() {
  String ssid = _http.arg("s");
  String pass = _http.arg("p");
  if (!ssid.length()) { _http.send(400, "text/plain", "missing ssid"); return; }

  strncpy(_target, ssid.c_str(), sizeof(_target) - 1);
  _target[sizeof(_target) - 1] = '\0';
  _state = PortalState::CONNECTING;
  _wifi->connectManual(_target, pass.c_str());
  _http.send(200, "application/json", "{\"ok\":true}");
}

void WifiPortal::handleStatus() {
  char esc[70];
  jsonEscapeInto(_target, esc, sizeof(esc));
  char out[160];
  snprintf(out, sizeof(out), "{\"state\":\"%s\",\"ssid\":\"%s\"}", stateName(_state), esc);
  _http.sendHeader("Cache-Control", "no-store");
  _http.send(200, "application/json", out);
}

void WifiPortal::handleNotFound() {
  // Every captive-portal probe (generate_204, hotspot-detect.html, ncsi.txt and
  // the rest) lands here. Redirecting them all to the page is what triggers the
  // "sign in to network" sheet instead of a silent "no internet" verdict.
  _http.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  _http.send(302, "text/plain", "");
}
