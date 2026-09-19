#pragma once
// =============================================================================
// WiFiManager -- non-blocking station connection with SmartConfig fallback.
//
// Nothing here ever blocks. update() is a state machine polled from loop() at
// WIFI_POLL_INTERVAL_MS; the radio is never waited on, so the UI keeps its
// frame rate and the sensors keep sampling no matter what the network is doing.
//
// Credential sources, in the order they are tried:
//   1. WIFI_SSID / WIFI_PASSWORD from Secrets.h, if the SSID is non-empty.
//   2. Whatever a previous successful SmartConfig left in NVS.
// When both exist the two alternate on each failed attempt, so a unit that was
// provisioned by phone still comes back up if it is later returned to the
// network baked into the firmware, and vice versa. After
// WIFI_ATTEMPTS_BEFORE_SC consecutive failures it stops guessing and listens
// for a phone (see the SmartConfig notes in Config.h).
// =============================================================================
#include <Arduino.h>
#include "Config.h"

enum class WifiState : uint8_t {
  RADIO_OFF,    // radio up but idle -- no credentials to try, nothing running
  CONNECTING,   // association in progress
  CONNECTED,    // got an IP
  RETRY_WAIT,   // attempt failed, waiting out WIFI_RETRY_INTERVAL_MS
  PROVISIONING  // SmartConfig listening for a phone
};

// Outcome of a connection the user asked for by hand from the Wi-Fi Setup
// screen. Kept apart from the automatic retry ladder because the user is
// standing there waiting for an answer: a manual attempt reports back once and
// does not silently roll into the SmartConfig fallback.
enum class ManualResult : uint8_t { NONE, PENDING, OK, FAILED };

class WiFiManager {
public:
  void begin();
  void update();

  bool isConnected() const { return _state == WifiState::CONNECTED; }
  WifiState state() const { return _state; }

  // Short human labels, sized for the TFT status line.
  const char *statusLabel() const;

  // Only meaningful while connected; 0 otherwise.
  int8_t rssi() const;
  const char *ipAddress() const { return _ip; }

  // True once a SmartConfig session has stored credentials in NVS.
  bool provisioned() const { return _hasProvisioned; }

  // ---- On-device setup -----------------------------------------------------
  // Brings the radio up without starting any connection attempt. Safe to call
  // when begin() already ran, and it is what lets the Wi-Fi Setup screen scan
  // on a unit that has no cloud configured and would otherwise never have
  // powered the radio at all.
  void ensureStarted();

  // Asynchronous scan -- scanStatus() is -1 while running, -2 if none was
  // started, otherwise the number of networks found. Nothing here blocks.
  void startScan();
  int scanStatus() const;
  void scanSsid(uint8_t i, char *out, size_t n) const;
  int8_t scanRssi(uint8_t i) const;
  bool scanLocked(uint8_t i) const;

  // Connect to a network the user picked. On success the credentials are kept
  // by the ESP32 core's own NVS store, so the unit comes back up on this
  // network after a power cycle without Secrets.h being involved.
  void connectManual(const char *ssid, const char *pass);
  ManualResult manualResult() const { return _manual; }
  void clearManualResult() { _manual = ManualResult::NONE; }

private:
  bool _started = false;
  ManualResult _manual = ManualResult::NONE;
  WifiState _state = WifiState::RADIO_OFF;
  uint32_t _lastPollMs = 0;
  uint32_t _stateEnteredMs = 0;
  uint8_t _failedAttempts = 0;

  bool _hasStoredCreds = false;   // Secrets.h supplied an SSID
  bool _hasProvisioned = false;   // NVS holds credentials from a past SmartConfig
  bool _useProvisioned = false;   // which source the current attempt is using

  // SmartConfig protocol cycling
  uint8_t _scPhase = 0;
  uint32_t _scPhaseStartMs = 0;
  uint32_t _scStartMs = 0;

  char _ip[16] = "0.0.0.0";

  void enterState(WifiState s);
  void startAttempt();
  void startSmartConfig();
  void beginSmartConfigPhase();
  uint8_t smartConfigPhaseCount() const;
};
