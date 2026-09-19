#pragma once
// =============================================================================
// FirebaseManager -- uploads sensor snapshots to the Realtime Database.
//
// The TLS handshake Firebase needs is the one genuinely slow, stack-hungry
// thing in this firmware: it can hold a core for hundreds of milliseconds and
// wants ~10 KB of stack. Running it from loop() would visibly stall the UI, so
// the whole network path lives on its own FreeRTOS task pinned to core 0 --
// Arduino's loop() runs on core 1 and never touches the radio.
//
// The two sides share exactly one small POD struct, guarded by a spinlock:
//   loop()  -> publish()  copies the latest reading in   (fast, never blocks)
//   task    -> takes a copy, uploads it                  (slow, blocks freely)
// Nothing else crosses the boundary, so there is no way for a slow upload to
// hold up a sensor read or a frame.
//
// Uploads that fail back off exponentially from FIREBASE_MIN_BACKOFF_MS to
// FIREBASE_MAX_BACKOFF_MS, so a database that is down or a token that will not
// mint costs one attempt every two minutes rather than a hot retry loop.
// =============================================================================
#include <Arduino.h>
#include "Config.h"
#include "SensorManager.h"
#include "AirQuality.h"

class WiFiManager;

// NOT_CONFIGURED rather than DISABLED: esp32-hal-gpio.h defines DISABLED as a
// macro, which a scoped enumerator cannot escape.
enum class CloudState : uint8_t {
  NOT_CONFIGURED,  // no Firebase credentials in Secrets.h -- uploader never starts
  WAITING_NET,  // Wi-Fi not up yet
  AUTHENTICATING,
  ONLINE,       // last upload succeeded
  ERROR         // last upload failed; backing off
};

class FirebaseManager {
public:
  // Returns false (and stays DISABLED) when Secrets.h has no Firebase config,
  // which is what keeps an unconfigured build byte-for-byte offline.
  bool begin(WiFiManager *wifi);

  // Called from loop(). Internally throttled to FIREBASE_PUBLISH_INTERVAL;
  // copies the snapshot under a spinlock and returns immediately.
  void publish(const SensorData &d, uint8_t score, AqCategory category);

  // Held off while the Wi-Fi setup portal is open. Two reasons: the TLS session
  // holds several KB of BearSSL buffers that the access point and web server
  // need more than the uploader does, and the station is about to be pointed at
  // a different network anyway, so anything uploaded now is about to fail.
  void setPaused(bool p) { _paused = p; }
  bool paused() const { return _paused; }

  // True while the task is inside a Firebase SDK call. Those calls do not
  // return promptly when the network is unhealthy -- FirebaseCore retries in a
  // loop of its own -- so anything that is about to disturb the radio has to
  // wait for this to go false. Pulling the network out from under a call in
  // flight leaves the task spinning on core 0, which starves IDLE0 and trips
  // the task watchdog. That is a reset, not a dropped upload.
  bool busy() const { return _busy; }

  CloudState state() const { return _state; }
  const char *statusLabel() const;
  uint32_t lastUploadMs() const { return _lastUploadMs; }
  uint32_t uploadCount() const { return _uploadCount; }
  bool enabled() const { return _state != CloudState::NOT_CONFIGURED; }

private:
  // What crosses the core boundary. Plain old data, copied whole.
  struct Snapshot {
    uint16_t pm1_0 = 0;
    uint16_t pm2_5 = 0;
    uint16_t pm10 = 0;
    bool pmsConnected = false;
    float mq2 = 0.0f;
    float mq2Baseline = 0.0f;
    bool mqWarmedUp = false;
    uint16_t mqWarmupRemainingSec = 0;
    uint8_t score = 0;
    uint8_t category = 0;
    uint16_t aqi = 0;
    bool aqiValid = false;
    uint32_t uptimeSec = 0;
    bool valid = false;
  };

  WiFiManager *_wifi = nullptr;
  volatile bool _paused = false;
  volatile bool _busy = false;
  volatile CloudState _state = CloudState::NOT_CONFIGURED;

  Snapshot _shared;                 // written by loop(), read by the task
  portMUX_TYPE _lock = portMUX_INITIALIZER_UNLOCKED;
  uint32_t _lastPublishMs = 0;

  volatile uint32_t _lastUploadMs = 0;
  volatile uint32_t _uploadCount = 0;
  uint32_t _backoffMs = 0;
  uint32_t _authBackoffMs = 0;   // separate ladder for sign-in failures
  uint32_t _nextAuthPollMs = 0;    // paces probeSignIn(), and so the request rate
  bool _credentialsProven = false; // Identity Toolkit has issued us a token
  uint32_t _nextAttemptMs = 0;
  uint32_t _lastHistoryMs = 0;
  bool _sdkStarted = false;

  static void taskEntry(void *arg);
  void taskLoop();
  // One Identity Toolkit sign-in request. errOut carries Google's own error
  // string ("INVALID_LOGIN_CREDENTIALS", "TOO_MANY_ATTEMPTS_TRY_LATER", ...).
  bool probeSignIn(String &errOut);
  bool uploadSnapshot(const Snapshot &s, bool alsoHistory);
  Snapshot takeSnapshot();
};
