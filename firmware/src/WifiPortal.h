#pragma once
// =============================================================================
// WifiPortal -- on-device Wi-Fi setup by way of the device's own access point.
//
// The user opens Wi-Fi Setup from the Quick Menu; the device raises an AP,
// joins it from a phone, and is handed a page listing the networks it can see.
// They tap one, type its password on the phone's keyboard, and the device
// connects. The TFT shows the instructions and mirrors the progress, so the
// screen and the phone never disagree about what is happening.
//
// Why an AP and not an on-screen picker: this device has two buttons. A
// character-at-a-time picker is perhaps sixty presses for a normal password,
// with no way to see a typo coming. Typing it once on a real keyboard is the
// whole reason this class exists.
//
// The radio runs in AP_STA while a session is open, so the phone keeps its
// connection to the portal and can watch the attempt succeed or fail rather
// than being dropped the moment the device joins the target network.
//
// Nothing here blocks: update() services DNS and HTTP a few hundred
// microseconds at a time and returns, and the connection attempt itself is
// WiFiManager's ordinary non-blocking CONNECTING state.
// =============================================================================
#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include "Config.h"
#include "WiFiManager.h"

class FirebaseManager;   // only ever used through its pause/resume API

enum class PortalState : uint8_t {
  OFF,          // no session
  STARTING,     // uploader standing down; radio not touched yet
  WAITING,      // AP up, nobody has joined yet
  JOINED,       // a phone is associated to the AP
  CONNECTING,   // trying the network the user chose
  SUCCESS,      // joined it; credentials stored
  FAILED        // did not join; the page lets them try again
};

class WifiPortal {
public:
  void begin(WiFiManager *wifi, FirebaseManager *cloud = nullptr) {
    _wifi = wifi;
    _cloud = cloud;
  }

  void start();     // raise the AP and open the session
  void stop();      // tear everything down and return the radio to station mode
  void update();    // call every loop pass while a session is open

  PortalState state() const { return _state; }
  bool active() const { return _state != PortalState::OFF; }

  // Seconds left before the session gives up on its own.
  uint16_t secondsLeft() const;

  // What the user picked, for the status screen.
  const char *targetSsid() const { return _target; }
  uint8_t clients() const;

private:
  WiFiManager *_wifi = nullptr;
  FirebaseManager *_cloud = nullptr;
  PortalState _state = PortalState::OFF;
  uint32_t _startedMs = 0;
  uint32_t _lastScanMs = 0;
  uint32_t _successMs = 0;   // when the attempt succeeded, for the closing delay
  char _target[33] = "";

  DNSServer _dns;
  WebServer _http{80};

  void beginSession();   // the actual radio work, once it is safe to do it
  void routes();
  void handleRoot();
  void handleScan();
  void handleConnect();
  void handleStatus();
  void handleNotFound();
};
