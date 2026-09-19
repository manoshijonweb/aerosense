#include "FirebaseManager.h"
#include "WiFiManager.h"
#include "Secrets.h"

#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>   // tokenStatusCallback -- logs token refreshes

#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// The SDK objects are file-scope rather than members so that the ~2 KB of
// FirebaseData buffers never lands in the header's footprint, and so nothing
// outside this translation unit can reach the network handle.
static FirebaseData fbdo;
static FirebaseAuth fbAuth;
static FirebaseConfig fbConfig;

// Marks the task as inside a Firebase call for as long as it is in scope, so
// every exit path clears the flag -- including the early `continue`s below.
// A flag left set would stall Wi-Fi setup for its whole timeout.
struct BusyScope {
  volatile bool &f;
  explicit BusyScope(volatile bool &flag) : f(flag) { f = true; }
  ~BusyScope() { f = false; }
};

// A build with an empty Secrets.h must stay exactly as offline as it was
// before this file existed. Both checks fold at compile time.
static inline bool firebaseConfigured() {
  return strlen(FIREBASE_API_KEY) > 0 && strlen(FIREBASE_DATABASE_URL) > 0;
}

// JSON string escaping for the two credential fields below. Only the two
// characters that can break out of a JSON string need handling; a password
// containing a quote or a backslash would otherwise produce a malformed
// request body and an error that looks nothing like "bad password".
static String jsonEscape(const char *s) {
  String out;
  for (const char *p = s; *p; ++p) {
    if (*p == '"' || *p == '\\') out += '\\';
    out += *p;
  }
  return out;
}

// One sign-in attempt against Identity Toolkit, made directly rather than
// through the SDK.
//
// This exists because FirebaseCore::tokenProcessingTask() retries in a loop
// that does not return while sign-in keeps failing (FirebaseCore.cpp, "while
// (!ret && ... != token_status_ready)"). Once Firebase.ready() is called with
// credentials that cannot work -- a disabled provider, a wrong password, a
// deleted user -- the library retries as fast as the network allows, for as
// long as the mistake stands, and no pacing on this side can reach inside that
// call to stop it. That is how this project earned a TOO_MANY_ATTEMPTS_TRY_LATER
// lockout during development.
//
// So the credentials are proven here first, with exactly one request per call
// and the retry interval owned by taskLoop(). The SDK is only handed control
// once a token has actually been issued, by which point its retry loop has
// nothing to spin on.
bool FirebaseManager::probeSignIn(String &errOut) {
  WiFiClientSecure client;
  // Identity Toolkit is a Google endpoint reached over HTTPS; the risk being
  // traded away here is a MITM able to present any certificate, which would
  // see credentials that are already stored in flash on the device. Pinning a
  // root CA would be better and is the obvious hardening step, but it needs
  // the cert refreshed before it expires or the device silently stops working.
  client.setInsecure();
  client.setTimeout(10000);

  HTTPClient https;
  String url = String("https://identitytoolkit.googleapis.com/v1/accounts:"
                      "signInWithPassword?key=") + FIREBASE_API_KEY;

  if (!https.begin(client, url)) {
    errOut = "could not open connection";
    return false;
  }
  https.addHeader("Content-Type", "application/json");
  https.setTimeout(10000);

  String body = String("{\"email\":\"") + jsonEscape(FIREBASE_USER_EMAIL) +
                "\",\"password\":\"" + jsonEscape(FIREBASE_USER_PASSWORD) +
                "\",\"returnSecureToken\":true}";

  int code = https.POST(body);
  String resp = https.getString();
  https.end();

  if (code == 200) {
    errOut = "";
    return true;
  }

  // Lift Google's own error string out of the response rather than reporting
  // the HTTP status: "INVALID_LOGIN_CREDENTIALS" says what to fix, "400" does
  // not. Done by hand because pulling in a JSON parser for one field would
  // cost more flash than the whole uploader.
  int m = resp.indexOf("\"message\"");
  if (m >= 0) {
    int q1 = resp.indexOf('"', resp.indexOf(':', m) + 1);
    int q2 = (q1 >= 0) ? resp.indexOf('"', q1 + 1) : -1;
    if (q2 > q1) errOut = resp.substring(q1 + 1, q2);
  }
  if (errOut.length() == 0) errOut = String("HTTP ") + code;
  return false;
}

bool FirebaseManager::begin(WiFiManager *wifi) {
  _wifi = wifi;

  if (!firebaseConfigured()) {
    Serial.println(F("[cloud] no Firebase credentials in Secrets.h -- uploader disabled"));
    _state = CloudState::NOT_CONFIGURED;
    return false;
  }

  _state = CloudState::WAITING_NET;

  xTaskCreatePinnedToCore(
      FirebaseManager::taskEntry,
      "firebase",
      FIREBASE_TASK_STACK,
      this,
      FIREBASE_TASK_PRIORITY,
      nullptr,
      FIREBASE_TASK_CORE);

  return true;
}

void FirebaseManager::publish(const SensorData &d, uint8_t score, AqCategory category) {
  if (_state == CloudState::NOT_CONFIGURED) return;

  uint32_t now = millis();
  if (now - _lastPublishMs < FIREBASE_PUBLISH_INTERVAL) return;
  _lastPublishMs = now;

  // Everything below is plain arithmetic on locals -- the spinlock is only held
  // for the struct copy itself, which is a few dozen bytes.
  Snapshot s;
  s.pm1_0 = d.pm1_0;
  s.pm2_5 = d.pm2_5;
  s.pm10 = d.pm10;
  s.pmsConnected = d.pmsConnected;
  s.mq2 = d.mq2;
  s.mq2Baseline = d.mq2Baseline;
  s.mqWarmedUp = d.mqWarmedUp;
  s.mqWarmupRemainingSec = d.mqWarmupRemainingSec;
  s.score = score;
  s.category = (uint8_t)category;
  s.aqi = AirQuality::calculateOverallAQI(d);
  s.aqiValid = AirQuality::aqiValid(d);
  s.uptimeSec = now / 1000;
  s.valid = true;

  portENTER_CRITICAL(&_lock);
  _shared = s;
  portEXIT_CRITICAL(&_lock);
}

FirebaseManager::Snapshot FirebaseManager::takeSnapshot() {
  Snapshot s;
  portENTER_CRITICAL(&_lock);
  s = _shared;
  portEXIT_CRITICAL(&_lock);
  return s;
}

void FirebaseManager::taskEntry(void *arg) {
  static_cast<FirebaseManager *>(arg)->taskLoop();
  vTaskDelete(nullptr);
}

void FirebaseManager::taskLoop() {
  const TickType_t tick = pdMS_TO_TICKS(100);

  for (;;) {
    vTaskDelay(tick);

    if (_paused) {
      _state = CloudState::WAITING_NET;
      continue;
    }

    if (!_wifi || !_wifi->isConnected()) {
      _state = CloudState::WAITING_NET;
      continue;
    }

    uint32_t now = millis();

    // ---- prove the credentials before the SDK sees them ---------------------
    // probeSignIn() makes exactly one request and returns, so the retry
    // interval below is the real request rate. Firebase.begin() is not called
    // until a token has actually been issued; see the note on probeSignIn()
    // for why handing bad credentials to the SDK cannot be undone from here.
    if (!_credentialsProven) {
      if ((int32_t)(now - _nextAuthPollMs) < 0) continue;

      _state = CloudState::AUTHENTICATING;
      String err;
      BusyScope scope(_busy);
      if (probeSignIn(err)) {
        Serial.println(F("[cloud] credentials accepted"));
        _credentialsProven = true;
        _authBackoffMs = 0;
      } else {
        _authBackoffMs = (_authBackoffMs == 0)
                             ? FIREBASE_MIN_BACKOFF_MS
                             : min<uint32_t>(_authBackoffMs * 2, FIREBASE_MAX_BACKOFF_MS);
        _nextAuthPollMs = now + _authBackoffMs;
        _state = CloudState::ERROR;
        Serial.printf("[cloud] sign-in rejected: %s -- next attempt in %lu s\n",
                      err.c_str(), (unsigned long)(_authBackoffMs / 1000));
        continue;
      }
    }

    // Start the SDK on the first connection rather than in begin(): signing in
    // needs a working link, and doing it here means a unit that boots out of
    // Wi-Fi range still authenticates the moment it finds the network.
    if (!_sdkStarted) {
      fbConfig.api_key = FIREBASE_API_KEY;
      fbConfig.database_url = FIREBASE_DATABASE_URL;
      fbAuth.user.email = FIREBASE_USER_EMAIL;
      fbAuth.user.password = FIREBASE_USER_PASSWORD;
      fbConfig.token_status_callback = tokenStatusCallback;
      fbConfig.max_token_generation_retry = 5;

      // WiFiManager owns retry timing; letting the SDK reconnect too would
      // have two state machines fighting over the radio.
      Firebase.reconnectWiFi(false);

      fbdo.setBSSLBufferSize(4096, 1024);
      fbdo.setResponseSize(2048);

      Serial.println(F("[cloud] signing in"));
      _state = CloudState::AUTHENTICATING;
      _busy = true;
      Firebase.begin(&fbConfig, &fbAuth);
      _busy = false;
      _sdkStarted = true;
    }

    // Safe to call now: the credentials above were accepted, so the library's
    // internal retry loop has a token to reach and returns promptly. Reaching
    // this with credentials revoked mid-session is the one case that can still
    // block inside the SDK -- rare enough to leave, worth knowing about.
    {
      BusyScope scope(_busy);
      if (!Firebase.ready()) {
        _state = CloudState::AUTHENTICATING;
        continue;
      }
    }

    // Signed difference rather than `now < _nextAttemptMs`: the latter stops
    // scheduling for good once millis() rolls over at ~49 days, and this device
    // is meant to sit on a desk indefinitely.
    if ((int32_t)(now - _nextAttemptMs) < 0) continue;

    Snapshot s = takeSnapshot();
    if (!s.valid) continue;   // loop() has not published a first reading yet

    bool alsoHistory = false;
#if FIREBASE_HISTORY_ENABLED
    alsoHistory = (_lastHistoryMs == 0) ||
                  (now - _lastHistoryMs >= FIREBASE_HISTORY_INTERVAL_MS);
#endif

    BusyScope scope(_busy);
    if (uploadSnapshot(s, alsoHistory)) {
      _lastUploadMs = now;
      _uploadCount++;
      _backoffMs = 0;
      _nextAttemptMs = now + FIREBASE_UPDATE_INTERVAL;
      if (alsoHistory) _lastHistoryMs = now;
      _state = CloudState::ONLINE;
    } else {
      Serial.printf("[cloud] upload failed: %s\n", fbdo.errorReason().c_str());
      _backoffMs = (_backoffMs == 0)
                       ? FIREBASE_MIN_BACKOFF_MS
                       : min<uint32_t>(_backoffMs * 2, FIREBASE_MAX_BACKOFF_MS);
      _nextAttemptMs = now + _backoffMs;
      _state = CloudState::ERROR;
      Serial.printf("[cloud] retrying in %lu ms\n", (unsigned long)_backoffMs);
    }
  }
}

bool FirebaseManager::uploadSnapshot(const Snapshot &s, bool alsoHistory) {
  FirebaseJson json;

  json.set("pm1_0", (int)s.pm1_0);
  json.set("pm2_5", (int)s.pm2_5);
  json.set("pm10", (int)s.pm10);
  json.set("pmsConnected", s.pmsConnected);

  json.set("mq2", s.mq2);
  json.set("mq2Baseline", s.mq2Baseline);
  json.set("mqWarmedUp", s.mqWarmedUp);
  json.set("mqWarmupRemainingSec", (int)s.mqWarmupRemainingSec);

  json.set("score", (int)s.score);
  json.set("category", AirQuality::categoryLabel((AqCategory)s.category));

  // AQI is only meaningful with live particulate data; uploading a stale or
  // fabricated index would be worse than uploading none, so the consumer is
  // told explicitly rather than having to infer it.
  json.set("aqiValid", s.aqiValid);
  if (s.aqiValid) json.set("aqi", (int)s.aqi);

  json.set("uptimeSec", (int)s.uptimeSec);
  json.set("rssi", _wifi ? (int)_wifi->rssi() : 0);

  // Server-side timestamp. The device has no RTC and only learns the time from
  // NTP inside the TLS layer, so letting the database stamp the write is both
  // simpler and more trustworthy than sending our own clock.
  json.set("ts/.sv", "timestamp");

  String base = String(FIREBASE_ROOT_PATH) + "/" + DEVICE_ID;

  if (!Firebase.RTDB.setJSON(&fbdo, (base + "/live").c_str(), &json)) return false;

  if (alsoHistory) {
    // A failed history push is not treated as an upload failure: the live node
    // is what matters, and backing the whole uploader off because an
    // append-only log hiccuped would cost live data too.
    if (!Firebase.RTDB.pushJSON(&fbdo, (base + "/history").c_str(), &json)) {
      Serial.printf("[cloud] history push failed: %s\n", fbdo.errorReason().c_str());
    }
  }

  return true;
}

const char *FirebaseManager::statusLabel() const {
  switch (_state) {
    case CloudState::NOT_CONFIGURED:       return "Off";
    case CloudState::WAITING_NET:    return "No net";
    case CloudState::AUTHENTICATING: return "Signing in";
    case CloudState::ONLINE:         return "Synced";
    case CloudState::ERROR:          return "Sync error";
  }
  return "?";
}
