#include "WiFiManager.h"
#include "Secrets.h"

#include <WiFi.h>

// The ESP32 core keeps the last successful SSID/password in its own NVS blob
// and reuses them when WiFi.begin() is called with no arguments. Rather than
// duplicating that store, the firmware only records whether such a set exists.
#include <Preferences.h>
static Preferences prefs;
static const char *PREFS_NAMESPACE = "aerosense";
static const char *PREFS_KEY_PROVISIONED = "wifiProv";

// strlen on a string literal folds at compile time, so these cost nothing.
static inline bool haveStoredCreds() { return strlen(WIFI_SSID) > 0; }
static inline bool haveV2Key() { return strlen(SMARTCONFIG_V2_KEY) == 16; }

void WiFiManager::ensureStarted() {
  if (_started) return;
  prefs.begin(PREFS_NAMESPACE, false);
  _hasProvisioned = prefs.getBool(PREFS_KEY_PROVISIONED, false);
  _hasStoredCreds = haveStoredCreds();

  WiFi.mode(WIFI_STA);
  WiFi.persistent(true);   // the core keeps the last good SSID/password for us
  // Auto-reconnect is left off deliberately: this state machine owns retry
  // timing, and the core's own reconnect loop would race it.
  WiFi.setAutoReconnect(false);
  WiFi.setSleep(true);     // modem sleep -- the upload duty cycle is tiny

  _started = true;
  enterState(WifiState::RADIO_OFF);
}

void WiFiManager::begin() {
  ensureStarted();

  if (!_hasStoredCreds && !_hasProvisioned) {
    // Nothing to try. Go straight to listening for a phone rather than burning
    // WIFI_ATTEMPTS_BEFORE_SC pointless attempts on credentials that do not
    // exist -- this is the out-of-the-box state for a unit with an empty
    // Secrets.h, and the fastest path to getting it onto a network.
    startSmartConfig();
    return;
  }

  // Prefer whichever source last worked. A provisioned unit has been put on
  // its network by hand more recently than the firmware was built.
  _useProvisioned = _hasProvisioned;
  startAttempt();
}

// ---------------------------------------------------------------- scanning --
void WiFiManager::startScan() {
  ensureStarted();
  WiFi.scanDelete();
  // Async: the call returns immediately and scanStatus() reports progress, so
  // the UI keeps rendering and the sensors keep sampling during the ~2s sweep.
  // A blocking scan here would freeze the screen and is the one thing this
  // firmware never does.
  WiFi.scanNetworks(true, false);
}

int WiFiManager::scanStatus() const { return WiFi.scanComplete(); }

void WiFiManager::scanSsid(uint8_t i, char *out, size_t n) const {
  String s = WiFi.SSID(i);
  strncpy(out, s.c_str(), n - 1);
  out[n - 1] = '\0';
}

int8_t WiFiManager::scanRssi(uint8_t i) const { return (int8_t)WiFi.RSSI(i); }

bool WiFiManager::scanLocked(uint8_t i) const {
  return WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
}

// ------------------------------------------------------- manual connection --
void WiFiManager::connectManual(const char *ssid, const char *pass) {
  ensureStarted();
  WiFi.stopSmartConfig();
  WiFi.disconnect(false, false);
  _manual = ManualResult::PENDING;
  _failedAttempts = 0;
  Serial.printf("[wifi] manual connect to \"%s\"\n", ssid);
  WiFi.begin(ssid, pass);
  enterState(WifiState::CONNECTING);
}

void WiFiManager::enterState(WifiState s) {
  _state = s;
  _stateEnteredMs = millis();
}

void WiFiManager::startAttempt() {
  WiFi.disconnect(false, false);

  if (_useProvisioned && _hasProvisioned) {
    Serial.println(F("[wifi] connecting with provisioned credentials"));
    WiFi.begin();                          // NVS credentials
  } else {
    Serial.printf("[wifi] connecting to \"%s\"\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
  enterState(WifiState::CONNECTING);
}

uint8_t WiFiManager::smartConfigPhaseCount() const {
  // ESPTouch v1, then ESPTouch v1 + AirKiss. v2 joins the cycle only when a
  // 16-character key is configured -- see the note in Config.h for why an
  // unkeyed v2 listener can never decode on this core.
  return haveV2Key() ? 3 : 2;
}

void WiFiManager::beginSmartConfigPhase() {
  WiFi.stopSmartConfig();

  smartconfig_type_t type = SC_TYPE_ESPTOUCH;
  const char *name = "ESPTouch";
  if (_scPhase == 1) {
    type = SC_TYPE_ESPTOUCH_AIRKISS;
    name = "ESPTouch+AirKiss";
  } else if (_scPhase == 2) {
    type = SC_TYPE_ESPTOUCH_V2;
    name = "ESPTouch v2";
  }

  // beginSmartConfig takes a non-const char*, so the key needs a mutable copy.
  static char key[17];
  strncpy(key, SMARTCONFIG_V2_KEY, sizeof(key) - 1);
  key[sizeof(key) - 1] = '\0';

  Serial.printf("[wifi] SmartConfig listening (%s)\n", name);
  WiFi.beginSmartConfig(type, haveV2Key() ? key : nullptr);
  _scPhaseStartMs = millis();
}

void WiFiManager::startSmartConfig() {
  WiFi.disconnect(false, false);
  _scPhase = 0;
  _scStartMs = millis();
  beginSmartConfigPhase();
  enterState(WifiState::PROVISIONING);
}

void WiFiManager::update() {
  if (!_started) return;   // radio never brought up; nothing to drive
  uint32_t now = millis();
  if (now - _lastPollMs < WIFI_POLL_INTERVAL_MS) return;
  _lastPollMs = now;

  switch (_state) {
    case WifiState::RADIO_OFF:
      break;

    case WifiState::CONNECTING: {
      if (WiFi.status() == WL_CONNECTED) {
        _failedAttempts = 0;
        strncpy(_ip, WiFi.localIP().toString().c_str(), sizeof(_ip) - 1);
        _ip[sizeof(_ip) - 1] = '\0';
        Serial.printf("[wifi] connected, ip %s, rssi %d dBm\n", _ip, WiFi.RSSI());
        if (_manual == ManualResult::PENDING) {
          // The user picked this network on the device. Remember that the
          // core's stored credentials are now the good ones, so a power cycle
          // comes back here rather than to whatever Secrets.h was built with.
          _manual = ManualResult::OK;
          _hasProvisioned = true;
          _useProvisioned = true;
          prefs.putBool(PREFS_KEY_PROVISIONED, true);
        }
        enterState(WifiState::CONNECTED);
        break;
      }
      if (now - _stateEnteredMs >= WIFI_CONNECT_TIMEOUT_MS) {
        _failedAttempts++;
        Serial.printf("[wifi] attempt %u timed out\n", _failedAttempts);
        WiFi.disconnect(false, false);
        if (_manual == ManualResult::PENDING) {
          // Report back to the screen the user is watching, and do not let a
          // hand-made attempt slide into the SmartConfig fallback underneath
          // them -- they are mid-flow and expect an answer, not a mode change.
          _manual = ManualResult::FAILED;
          enterState(WifiState::RETRY_WAIT);
          break;
        }
        if (_failedAttempts >= WIFI_ATTEMPTS_BEFORE_SC) {
          // Stored credentials are evidently stale. Stop guessing.
          _failedAttempts = 0;
          startSmartConfig();
        } else {
          // Alternate sources when both exist, so neither is starved.
          if (_hasStoredCreds && _hasProvisioned) _useProvisioned = !_useProvisioned;
          enterState(WifiState::RETRY_WAIT);
        }
      }
      break;
    }

    case WifiState::CONNECTED:
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[wifi] link lost"));
        strncpy(_ip, "0.0.0.0", sizeof(_ip));
        enterState(WifiState::RETRY_WAIT);
      }
      break;

    case WifiState::RETRY_WAIT:
      if (now - _stateEnteredMs >= WIFI_RETRY_INTERVAL_MS) startAttempt();
      break;

    case WifiState::PROVISIONING: {
      if (WiFi.smartConfigDone() && WiFi.status() == WL_CONNECTED) {
        WiFi.stopSmartConfig();
        _hasProvisioned = true;
        _useProvisioned = true;
        prefs.putBool(PREFS_KEY_PROVISIONED, true);
        _failedAttempts = 0;
        strncpy(_ip, WiFi.localIP().toString().c_str(), sizeof(_ip) - 1);
        _ip[sizeof(_ip) - 1] = '\0';
        Serial.printf("[wifi] provisioned via SmartConfig, ip %s\n", _ip);
        enterState(WifiState::CONNECTED);
        break;
      }

      if (now - _scStartMs >= SMARTCONFIG_TIMEOUT_MS) {
        // No phone showed up. Fall back to retrying stored credentials rather
        // than listening forever -- the network may simply have been down.
        Serial.println(F("[wifi] SmartConfig timed out"));
        WiFi.stopSmartConfig();
        if (_hasStoredCreds || _hasProvisioned) {
          enterState(WifiState::RETRY_WAIT);
        } else {
          Serial.println(F("[wifi] no credentials available -- radio idle"));
          WiFi.mode(WIFI_OFF);
          enterState(WifiState::RADIO_OFF);
        }
        break;
      }

      if (now - _scPhaseStartMs >= SMARTCONFIG_PHASE_MS) {
        _scPhase = (_scPhase + 1) % smartConfigPhaseCount();
        beginSmartConfigPhase();
      }
      break;
    }
  }
}

const char *WiFiManager::statusLabel() const {
  switch (_state) {
    case WifiState::RADIO_OFF:     return "Offline";
    case WifiState::CONNECTING:   return "Connecting";
    case WifiState::CONNECTED:    return "Connected";
    case WifiState::RETRY_WAIT:   return "Retrying";
    case WifiState::PROVISIONING: return "Pair phone";
  }
  return "?";
}

int8_t WiFiManager::rssi() const {
  return _state == WifiState::CONNECTED ? (int8_t)WiFi.RSSI() : 0;
}
