// =============================================================================
// Kavya / AeroSense -- Portable ESP32 Environmental Monitoring Desk Device
//
// Hardware: ESP32 DevKit V1, PMS5003, MQ-2, 1.8" ST7735 TFT,
// two navigation buttons. See Config.h for the full pin map.
//
// Architecture: every concern lives in its own manager (Sensor, Button,
// Display, Graph, Statistics, Warning, UI/AirQuality) so loop() itself stays a
// thin, non-blocking dispatcher. See the individual .h files.
//
// Connectivity is optional and entirely self-disabling. With an empty
// Secrets.h no radio is ever started and nothing leaves the device -- the
// firmware behaves exactly as the offline build did. Fill in Secrets.h and the
// same binary connects to Wi-Fi and mirrors every reading to the Firebase
// Realtime Database. See FIREBASE_SETUP.md.
//
// All network work happens off the critical path: WiFiManager is a polled
// state machine that never blocks, and FirebaseManager does its TLS on a
// dedicated task pinned to core 0. loop() below stays a thin dispatcher.
//
// PlatformIO note: this is the former Kavya.ino, renamed to main.cpp. The only
// change is the explicit <Arduino.h> include -- PlatformIO compiles plain C++
// and does not inject it (or generate function prototypes) the way the Arduino
// IDE does for .ino files.
// =============================================================================
#include <Arduino.h>

#include "Config.h"
#include "Theme.h"
#include "SensorManager.h"
#include "ButtonManager.h"
#include "DisplayManager.h"
#include "AirQuality.h"
#include "GraphManager.h"
#include "StatisticsManager.h"
#include "WarningManager.h"
#include "UIManager.h"
#include "WiFiManager.h"
#include "WifiPortal.h"
#include "FirebaseManager.h"

SensorManager sensors;
ButtonManager buttons;
DisplayManager display;
GraphManager graphs;
StatisticsManager statistics;
WarningManager warnings;
UIManager ui;
// The Arduino loop task gets 8 KB of stack by default. That is ample for the
// monitoring firmware, but the Wi-Fi setup portal runs a DNS server and a web
// server from inside loop(), and WebServer does a great deal of String work on
// the caller's stack while parsing a request and building a response. 8 KB is
// not enough for that, and the overflow shows up as the device resetting the
// moment the setup screen is opened rather than as anything more informative.
SET_LOOP_TASK_STACK_SIZE(16384);

WiFiManager wifi;
WifiPortal portal;
FirebaseManager cloud;

void setup() {
  Serial.begin(115200);

  display.begin();
  display.showSplash("AeroSense", "Air Quality Monitor");

  sensors.begin();
  buttons.begin();

  portal.begin(&wifi, &cloud);
  ui.begin(&display, &sensors, &buttons, &graphs, &statistics, &warnings, &wifi, &cloud, &portal);

  // The uploader is the only consumer of the network, so it decides whether
  // there is any reason to power the radio at all: with no Firebase config in
  // Secrets.h begin() returns false, WiFiManager is never started, and the
  // build is offline in the literal sense -- WiFi.mode() is never called.
  // Started after the UI so a slow association cannot delay the first frame.
  if (cloud.begin(&wifi)) {
    wifi.begin();
  }

  uint32_t splashStart = millis();
  while (millis() - splashStart < 1500) {
    // Non-blocking splash hold: keep sampling so first readings aren't stale.
    // delay(1) rather than a bare spin so the scheduler gets the core back --
    // this is the one place in the firmware that holds it for more than a tick.
    // Buttons are deliberately not polled: a press here would be queued and
    // then acted on the instant the first page appears.
    sensors.update();
    delay(1);
  }
}

void loop() {
  // Background monitoring always runs, independent of what the display is doing.
  sensors.update();
  buttons.update();

  const SensorData &data = sensors.data();
  statistics.update(data);
  graphs.update(data);

  uint8_t score = AirQuality::overallScore(data);
  AqCategory overallCategory = AirQuality::categoryForScore(score);
  warnings.update(data, overallCategory);

  // Connectivity. wifi.update() is a polled state machine; cloud.publish()
  // only copies the snapshot under a spinlock -- the upload itself happens on
  // the core 0 task. Neither can stall the loop.
  // Unconditional: WiFiManager no-ops until something starts the radio, and
  // Wi-Fi Setup in the Quick Menu can start it even on a build with no cloud
  // configured. Gating this on cloud.enabled() would leave that screen unable
  // to finish a connection.
  wifi.update();
  // Services DNS and HTTP for the setup access point. A no-op unless a setup
  // session is open, and each call is a few hundred microseconds, so it sits in
  // the loop rather than on a task.
  portal.update();
  cloud.publish(data, score, overallCategory);

  // UI rendering is internally throttled and never blocks the loop above.
  ui.update();
}
