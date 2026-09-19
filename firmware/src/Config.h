#pragma once
// =============================================================================
// Config.h -- Central hardware pin map, timing constants, thresholds.
// Change hardware wiring or tuning values here; nothing else should hardcode
// a pin number or a magic timing/threshold value.
// =============================================================================
#include <Arduino.h>

// ------------------------------- Pin Map ------------------------------------
// PMS5003 (UART2). Sensor TX -> ESP32 RX, Sensor RX -> ESP32 TX.
#define PIN_PMS_RX      16
#define PIN_PMS_TX      17

// MQ gas sensors (analog, ADC1 channels -- safe to use alongside WiFi later)
#define PIN_MQ2         34
// GPIO35 carried the MQ-7 in an earlier revision. There is no MQ-7 in this
// device, so the pin is deliberately left unassigned rather than reused.

// GPIO4 was the DHT11 data line. The DHT11 has been removed from this design;
// the pin is deliberately left unassigned rather than reused.

// ST7735 TFT (SPI). SCK=18 / MOSI=23 are the default VSPI pins, wired direct.
#define PIN_TFT_CS      5
#define PIN_TFT_DC      15
#define PIN_TFT_RST     2

// Navigation buttons (INPUT_PULLUP, active LOW)
#define PIN_BTN1        25
#define PIN_BTN2        26

// ------------------------------- Display -------------------------------------
#define SCREEN_WIDTH    160
#define SCREEN_HEIGHT   128

// SPI clock for the panel. Adafruit_ST7735 defaults to 16 MHz; the controller
// and typical module wiring take considerably more, and full-page redraws are
// pure pixel-pushing, so this is the single biggest lever on how fast a page
// switch paints. Back off towards 16000000 if the panel shows torn rows or
// speckle -- long jumper leads are the usual reason it has to come down.
#define TFT_SPI_HZ      26000000UL

// ------------------------------- Timing (ms) ---------------------------------
#define MQ_READ_INTERVAL_MS       100     // analog sample + EMA smoothing tick
#define STATS_SAMPLE_INTERVAL_MS  1000    // statistics accumulation tick
#define GRAPH_SAMPLE_INTERVAL_MS  2000    // history buffer push interval
// UI redraw tick. Every page is a change-aware partial update -- a tick where
// nothing moved costs a handful of snprintf/strncmp calls and touches no
// pixels -- so the rate is set by how live the readouts should feel, not by
// what the SPI bus can carry. Page transitions bypass this throttle entirely
// (UIManager::_forceFrame), so input latency is not tied to it.
#define UI_FRAME_INTERVAL_MS      60      // UI redraw tick (non-blocking)
// Reference interval for the Air Quality page's rise/fall marks. Must be well
// clear of the PM averaging window (PMS_AVERAGE_SAMPLES frames, ~4s) or the
// comparison is against a reading the average has not finished moving off.
#define AQ_TREND_INTERVAL_MS      10000

#define WARNING_DISPLAY_MS        3000    // how long a warning overlay stays up
#define WARNING_COOLDOWN_MS       6000    // min gap before the same warning can re-fire
#define PMS_STALE_TIMEOUT_MS      5000    // no valid frame in this long -> "disconnected"

// Button behaviour
#define BUTTON_DEBOUNCE_MS        30
#define BUTTON_LONGPRESS_MS       600

// ------------------------------- Sensor tuning --------------------------------
// MQ heater warm-up. The MQ-2 reads near full scale for the first minute or so
// after power-on while its heater stabilises. It is sampled and displayed
// throughout, but gas warnings -- and the MQ contribution to the composite
// environment score -- stay suppressed until this has elapsed.
// Sixty seconds clears the false-alarm spike; a full sensitivity burn-in is
// much longer, but there is no reason to keep the user waiting for that.
#define MQ_WARMUP_TIME_MS         60000UL

#define MQ_EMA_ALPHA              0.20f   // exponential smoothing factor (0..1)
#define PMS_AVERAGE_SAMPLES       4       // rolling average window for PM readings

// A single ESP32 ADC1 conversion carries tens of counts of noise, which the EMA
// alone can only trade against response time. Averaging a burst per tick
// attacks the noise instead: 16 samples is ~4x quieter for ~170us of work, so
// the EMA is left free to stay responsive.
#define MQ_OVERSAMPLE             16      // ADC samples averaged per MQ read tick

// Calibration. MQ2_CLEAN_AIR_RAW is the reading the element settles to in clean
// air; it anchors the top of the gas contribution to the environment score, so
// a wrong value skews every score the device reports. It varies part to part
// and with supply voltage, which is why the fixed number below is only a
// fallback: with MQ_AUTO_BASELINE on, the firmware adopts its own reading at
// the moment warm-up completes as the baseline instead.
//
// That assumes the air at the end of warm-up is clean. It usually is -- the
// device has been sitting on a desk for a minute -- and the plausibility band
// below rejects the case where it isn't, falling back to the fixed value. Set
// MQ_AUTO_BASELINE to 0 to always use the fixed value.
#define MQ_AUTO_BASELINE          1
#define MQ2_CLEAN_AIR_RAW         400.0f  // raw ADC reading in clean air
#define MQ_BASELINE_MIN_RAW       150.0f  // below this, the element/wiring is suspect
#define MQ_BASELINE_MAX_RAW       1200.0f // above this, the air was not clean
#define MQ_RAW_MAX                4095.0f // ESP32 12-bit ADC

// ------------------------------- Thresholds -----------------------------------
// Particulate matter (ug/m3, PM2.5 atmospheric)
#define THRESHOLD_PM25_GOOD        12
#define THRESHOLD_PM25_MODERATE    35
#define THRESHOLD_PM25_POOR        55
#define THRESHOLD_PM25_HAZARDOUS   150

// MQ2 raw ADC warning bands (0..4095)
#define THRESHOLD_MQ2_MEDIUM       1400
#define THRESHOLD_MQ2_HIGH         2200
#define THRESHOLD_MQ2_DANGER       3000

// ------------------------------- History buffers ------------------------------
#define GRAPH_HISTORY_LENGTH       60      // 60 samples * 2s = 2 minutes of history

// ------------------------------- Connectivity ---------------------------------
// All values in ms. Every one of these is used with millis() scheduling -- no
// blocking waits anywhere in the WiFi / Firebase paths.
#define WIFI_CONNECT_TIMEOUT_MS     15000   // give stored credentials this long per attempt
#define WIFI_RETRY_INTERVAL_MS      20000   // pause between failed connection attempts
#define WIFI_ATTEMPTS_BEFORE_SC     3       // failed attempts before assuming creds are stale
#define SMARTCONFIG_TIMEOUT_MS      180000  // give up waiting for a phone after 3 minutes

// SmartConfig protocol. Phone apps differ in which one they speak and rarely
// say so, and a listener on the wrong protocol simply never hears the phone.
// Rather than guessing, provisioning cycles through the keyless protocols --
// ESPTouch v1, then ESPTouch v1+AirKiss -- swapping every SMARTCONFIG_PHASE_MS
// until something lands.
#define SMARTCONFIG_PHASE_MS        40000

// ESPTouch v2 is only added to the cycle when a key is set here, because this
// ESP32 core forces encryption on for v2 (WiFiSTA.cpp: esp_touch_v2_enable_crypt
// = true), so v2 without a key can never decode. Must be EXACTLY 16 characters,
// and the same string must be entered in the app.
#define SMARTCONFIG_V2_KEY          ""
#define WIFI_POLL_INTERVAL_MS       200     // how often the state machine polls the radio

// ------------------------------- Setup portal ---------------------------------
// On-device Wi-Fi setup raises the device's own access point and serves a page
// where the network is picked and its password typed on a real keyboard. Two
// buttons cannot do password entry in any humane way, which is why the portal
// exists rather than an on-screen character picker.
#define WIFI_AP_SSID                "AeroSense-Setup"
// WPA2 needs at least 8 characters. Set to "" for an open access point -- easier
// to join, but for the length of the session anyone in range could point the
// device at their own network. The password is displayed on the TFT, so only
// someone who can see the device can join.
#define WIFI_AP_PASSWORD            "aerosense"
#define WIFI_PORTAL_TIMEOUT_MS      300000UL  // give up and return to monitoring
#define WIFI_PORTAL_SCAN_MAX        20        // networks listed in the page

#define FIREBASE_UPDATE_INTERVAL    5000    // nominal upload period
#define FIREBASE_MIN_BACKOFF_MS     10000   // first retry delay after a failed upload
#define FIREBASE_MAX_BACKOFF_MS     120000  // ceiling for the exponential backoff
#define FIREBASE_PUBLISH_INTERVAL   500     // how often loop() hands a fresh snapshot over
#define FIREBASE_TASK_STACK         10240   // bytes -- TLS handshake needs a deep stack
#define FIREBASE_TASK_PRIORITY      1       // below the Arduino loop task
#define FIREBASE_TASK_CORE          0       // Arduino loop() runs on core 1; keep TLS off it

// Sign-in pacing. Firebase.ready() is not a passive check -- the token request
// is made inside it -- so how often it is CALLED is the request rate. A normal
// first sign-in needs a handful of calls over a few seconds; anything still
// failing after the grace window is a misconfiguration that will not fix
// itself, and retrying it hard earns a TOO_MANY_ATTEMPTS_TRY_LATER lockout
// that outlasts the mistake.
#define FIREBASE_AUTH_POLL_MS       2000UL
#define FIREBASE_AUTH_GRACE_MS      30000UL

// The live node is overwritten in place every FIREBASE_UPDATE_INTERVAL, so it
// costs nothing to keep. History is append-only and grows without bound, so it
// is pushed far more slowly -- at one minute apart a device writes ~1440
// records a day, which the Spark (free) plan carries comfortably. Set
// FIREBASE_HISTORY_ENABLED to 0 to upload only the live snapshot.
#define FIREBASE_HISTORY_ENABLED    1
#define FIREBASE_HISTORY_INTERVAL_MS 60000UL

// Root path in the Realtime Database. The device writes under
// <FIREBASE_ROOT_PATH>/<DEVICE_ID>/, and the security rules in
// FIREBASE_SETUP.md are written against exactly this shape.
#define FIREBASE_ROOT_PATH          "/devices"
