#pragma once
// =============================================================================
// UIManager -- owns page navigation, the quick menu, and the warning overlay.
// Each page has its own render function (split between UIManager.cpp for the
// simpler pages and UIPages.cpp for the rest) plus a small cache struct so
// DisplayManager's change-aware helpers know what's already on screen.
// Rendering is throttled to UI_FRAME_INTERVAL_MS and pages redraw chrome only
// once per visit (`full`) -- every other tick is a value-only partial update.
// =============================================================================
#include <Arduino.h>
#include "Config.h"
#include "Theme.h"
#include "DisplayManager.h"
#include "SensorManager.h"
#include "ButtonManager.h"
#include "AirQuality.h"
#include "GraphManager.h"
#include "StatisticsManager.h"
#include "WarningManager.h"
#include "WiFiManager.h"
#include "WifiPortal.h"
#include "FirebaseManager.h"

enum class Page : uint8_t {
  HOME = 0,
  AIR_QUALITY,
  GAS,
  DASHBOARD,
  SYSTEM_STATUS,
  GRAPHS,
  STATISTICS,
  PAGE_COUNT
};

static const uint8_t QUICK_MENU_COUNT = 4;

// The Wi-Fi setup screen lives behind the Quick Menu rather than in the page
// carousel: it is a configuration action, not a reading to cycle past, and the
// seven monitoring pages stay as they were. The screen itself only reports --
// the choosing and the typing happen on the phone, through WifiPortal.

// Gas banding, shared by the Home strip and the Gas page so both always speak
// the same word and the same colour for a given reading.
static inline const char *gasStatusWord(float raw) {
  if (raw >= THRESHOLD_MQ2_DANGER) return "DANGER";
  if (raw >= THRESHOLD_MQ2_HIGH)   return "HIGH";
  if (raw >= THRESHOLD_MQ2_MEDIUM) return "MEDIUM";
  return "LOW";
}

static inline uint16_t gasStatusColor(float raw) {
  if (raw >= THRESHOLD_MQ2_DANGER) return Theme::HAZARDOUS;
  if (raw >= THRESHOLD_MQ2_HIGH)   return Theme::POOR;
  if (raw >= THRESHOLD_MQ2_MEDIUM) return Theme::MODERATE;
  return Theme::GOOD;
}

class UIManager {
public:
  // wifi/cloud are optional: an offline build passes neither, and the header
  // then carries no connectivity glyph at all rather than a permanently dead
  // one. Everything else works identically either way.
  void begin(DisplayManager *disp, SensorManager *sensors, ButtonManager *buttons,
             GraphManager *graphs, StatisticsManager *stats, WarningManager *warnings,
             WiFiManager *wifi = nullptr, FirebaseManager *cloud = nullptr,
             WifiPortal *portal = nullptr);
  void update();

private:
  DisplayManager *_disp = nullptr;
  SensorManager *_sensors = nullptr;
  ButtonManager *_buttons = nullptr;
  GraphManager *_graphs = nullptr;
  StatisticsManager *_stats = nullptr;
  WarningManager *_warnings = nullptr;
  WiFiManager *_wifi = nullptr;
  FirebaseManager *_cloud = nullptr;
  WifiPortal *_portal = nullptr;

  Page _page = Page::HOME;
  bool _pageEntered = false;
  uint32_t _lastFrameMs = 0;
  // Set by anything that changes what should be on screen, so the next loop
  // pass renders without waiting out the frame throttle.
  bool _forceFrame = true;

  bool _menuOpen = false;
  uint8_t _menuIndex = 0;
  bool _menuEntered = false;

  // ---- Wi-Fi setup screen ----
  // Repainted whole-screen when the portal's state or its countdown changes,
  // rather than through the change-aware field helpers: it is a short-lived
  // modal that changes a few times a minute, so per-field caching would be
  // cost without benefit.
  bool _wifiOpen = false;
  bool _wifiEntered = false;
  bool _wifiDirty = true;
  PortalState _wifiLastState = PortalState::OFF;
  uint16_t _wifiLastSecs = 0;

  void openWifiSetup();
  void closeWifiSetup();
  void handleWifiInput(ButtonEvent evt);
  void renderWifiSetup(bool full);

  bool _warningShowing = false;

  uint32_t _fpsWindowStartMs = 0;
  uint16_t _fpsFrameCount = 0;
  uint16_t _fps = 0;
  uint32_t _lastRenderDurationUs = 0;

  // Live sensor-link dot in the header; cached so it only repaints on change.
  uint16_t _headerDot = 0;

  // Connectivity glyph in the header. Signal strength is the bar count, cloud
  // sync state is the colour -- one 11x8 widget answers both "is it on the
  // network" and "is the data getting out", which are the two questions a user
  // actually has. Cached so a steady link costs no pixels.
  uint8_t _headerWifiBars = 255;
  uint16_t _headerWifiColor = 0;
  void renderHeaderLink(bool full);

  void handleInput();
  void goToPage(Page p);
  void renderCurrentPage(bool full);

  void openQuickMenu();
  void closeQuickMenu(bool execute);
  void renderQuickMenu(bool full);

  void renderHome(bool full);
  void renderAirQuality(bool full);
  void renderGas(bool full);
  void renderDashboard(bool full);
  void renderSystemStatus(bool full);
  void renderGraphs(bool full);
  void renderStatistics(bool full);

  const char *pageTitle(Page p) const;
  const char *pageFooterHint() const;

  struct HomeCache {
    char aqi[8] = "";
    char aqiCategory[14] = "";
    char pm1[8] = "";
    char pm25[8] = "";
    char pm10[8] = "";
    char gas[10] = "";
    char gasStatus[10] = "";
    uint8_t gasLevel = 255;
    uint16_t heroAccent = 0;
  } _home;

  struct AirQualityCache {
    char pm1[12] = "";
    char pm25[12] = "";
    char pm10[12] = "";
    char category[16] = "";
    uint16_t bannerColor = 0;
    uint8_t pm1Bar = 255, pm25Bar = 255, pm10Bar = 255;
    // Trend is a measurement over time, so its reference is sampled on its own
    // slow cadence rather than per frame -- comparing a reading against the one
    // from 60ms ago says nothing. `drawn` is the usual screen-state cache so a
    // mark that hasn't moved isn't repainted underneath itself every frame.
    float prevPm[3] = {-1, -1, -1};
    char trend[3][2] = {"=", "=", "="};
    char trendDrawn[3][2] = {"", "", ""};
    uint32_t lastTrendMs = 0;
  } _aq;

  struct GasCache {
    char mq2[12] = "";
    char mq2Status[10] = "";
    int16_t mq2MarkerX = -32000;
    char peak[12] = "";
    char warmup[22] = "";
    uint16_t accent = 0;
  } _gasCache;

  struct DashboardCache {
    char values[4][10] = {"", "", "", ""};
    bool livePulse = false;
    uint32_t lastPulseMs = 0;
  } _dash;

  struct StatusCache {
    char uptime[16] = "";
    char heap[16] = "";
    char fps[10] = "";
    char frameTime[16] = "";
    char pmsStatus[14] = "";
    char mqStatus[14] = "";
  } _sys;

  struct StatsCache {
    char rows[3][20] = {"", "", ""};
  } _statsCache;
};
