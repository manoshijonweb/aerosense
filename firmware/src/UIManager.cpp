#include "UIManager.h"

static const char *QUICK_MENU_LABELS[QUICK_MENU_COUNT] = {
  "Reset Statistics",
  "Wi-Fi Setup",
  "Go to Home",
  "Close Menu"
};

void UIManager::begin(DisplayManager *disp, SensorManager *sensors, ButtonManager *buttons,
                       GraphManager *graphs, StatisticsManager *stats, WarningManager *warnings,
                       WiFiManager *wifi, FirebaseManager *cloud, WifiPortal *portal) {
  _disp = disp;
  _sensors = sensors;
  _buttons = buttons;
  _graphs = graphs;
  _stats = stats;
  _warnings = warnings;
  _wifi = wifi;
  _cloud = cloud;
  _portal = portal;
  _fpsWindowStartMs = millis();
}

void UIManager::update() {
  handleInput();

  // Warnings are suppressed during Wi-Fi setup as well as in the menu: the
  // user is part-way through typing a password, and a banner that steals the
  // screen for three seconds would lose their place for a condition they can
  // do nothing about until they are finished.
  bool warningActive = _warnings->isActive() && !_menuOpen && !_wifiOpen;

  if (warningActive && !_warningShowing) {
    _disp->drawWarningBanner(_warnings->message(), _warnings->color());
    _warningShowing = true;
    return;
  }
  if (!warningActive && _warningShowing) {
    _warningShowing = false;
    _pageEntered = false;    // erase banner remnants with a full page redraw
    _forceFrame = true;      // and put it up now, not on the next scheduled tick
  }
  if (warningActive) {
    return; // freeze underlying page while the banner is up
  }

  uint32_t now = millis();
  // A transition -- new page, menu opened or closed, banner cleared -- renders
  // on the very next loop pass. Only steady-state value refreshes wait for the
  // frame tick, so what the user feels after a press is the render itself, not
  // the render plus up to a frame interval of scheduling delay.
  if (!_forceFrame && now - _lastFrameMs < UI_FRAME_INTERVAL_MS) return;
  _forceFrame = false;
  _lastFrameMs = now;

  _fpsFrameCount++;
  if (now - _fpsWindowStartMs >= 1000) {
    _fps = _fpsFrameCount;
    _fpsFrameCount = 0;
    _fpsWindowStartMs = now;
  }

  uint32_t renderStartUs = micros();
  if (_wifiOpen) {
    renderWifiSetup(!_wifiEntered);
    _wifiEntered = true;
  } else if (_menuOpen) {
    renderQuickMenu(!_menuEntered);
    _menuEntered = true;
  } else {
    renderCurrentPage(!_pageEntered);
    _pageEntered = true;
  }
  _lastRenderDurationUs = micros() - renderStartUs;
}

void UIManager::handleInput() {
  ButtonEvent evt;
  while ((evt = _buttons->popEvent()) != ButtonEvent::NONE) {
    // While the banner owns the screen it also owns the buttons: the press
    // acknowledges it and nothing else. Letting navigation through here moved
    // the page invisibly behind the banner, so the user pressed once to clear
    // the alert and found themselves somewhere they never chose.
    if (_warningShowing) {
      _warnings->dismiss();
      continue;
    }

    if (_wifiOpen) {
      handleWifiInput(evt);
    } else if (_menuOpen) {
      switch (evt) {
        case ButtonEvent::BTN1_SHORT:
          _menuIndex = (_menuIndex + QUICK_MENU_COUNT - 1) % QUICK_MENU_COUNT;
          _forceFrame = true;   // move the highlight on this pass, not the next tick
          break;
        case ButtonEvent::BTN2_SHORT:
          _menuIndex = (_menuIndex + 1) % QUICK_MENU_COUNT;
          _forceFrame = true;
          break;
        case ButtonEvent::BTN1_LONG:
          closeQuickMenu(false);
          break;
        case ButtonEvent::BTN2_LONG:
          closeQuickMenu(true);
          break;
        default: break;
      }
    } else {
      switch (evt) {
        case ButtonEvent::BTN1_SHORT:
          goToPage((Page)(((uint8_t)_page + (uint8_t)Page::PAGE_COUNT - 1) % (uint8_t)Page::PAGE_COUNT));
          break;
        case ButtonEvent::BTN2_SHORT:
          goToPage((Page)(((uint8_t)_page + 1) % (uint8_t)Page::PAGE_COUNT));
          break;
        case ButtonEvent::BTN1_LONG:
          goToPage(Page::HOME);
          break;
        case ButtonEvent::BTN2_LONG:
          openQuickMenu();
          break;
        default: break;
      }
    }
  }
}

void UIManager::goToPage(Page p) {
  _page = p;
  _pageEntered = false;
  _forceFrame = true;
}

void UIManager::openQuickMenu() {
  _menuOpen = true;
  _menuIndex = 0;
  _menuEntered = false;
  _forceFrame = true;
}

void UIManager::closeQuickMenu(bool execute) {
  bool toWifi = false;
  if (execute) {
    switch (_menuIndex) {
      case 0: _stats->reset(); break;
      case 1: toWifi = true; break;
      case 2: goToPage(Page::HOME); break;
      default: break; // Close Menu
    }
  }
  _menuOpen = false;
  _pageEntered = false; // repaint the underlying page to clear the overlay
  _forceFrame = true;
  if (toWifi) openWifiSetup();
}

// The overlay now carries its own inverted title bar, so it reads as a sheet
// laid over the page rather than a slightly lighter rectangle on it.
void UIManager::renderQuickMenu(bool full) {
  Adafruit_ST7735 &tft = _disp->tft();
  // Height follows the item count rather than a fixed number, so adding a menu
  // entry cannot quietly push the last row out through the bottom of the sheet.
  const int16_t x = 14, y = 20, w = SCREEN_WIDTH - 28;
  const int16_t h = 22 + QUICK_MENU_COUNT * 18 + 4;

  if (full) {
    tft.fillRect(x, y, w, h, Theme::PANEL_HI);
    tft.drawRect(x, y, w, h, Theme::HEADER_BG);
    tft.fillRect(x, y, w, 16, Theme::HEADER_BG);
    tft.fillRect(x, y + 14, w, 2, Theme::ACCENT);
    _disp->drawText(x + 7, y + 4, TextStyle::SMALL, Theme::TEXT_INV, "QUICK MENU");
  }

  for (uint8_t i = 0; i < QUICK_MENU_COUNT; i++) {
    _disp->drawMenuItem(x + 6, y + 22 + i * 18, w - 12, 16, QUICK_MENU_LABELS[i], i == _menuIndex);
  }
}

// Short, uppercase titles: the header now sets them in a proportional bold
// face, which is wider per character than the old 5x7 text.
const char *UIManager::pageTitle(Page p) const {
  switch (p) {
    case Page::HOME:           return "HOME";
    case Page::AIR_QUALITY:    return "AIR QUALITY";
    case Page::GAS:            return "GAS";
    case Page::DASHBOARD:      return "SENSORS";
    case Page::SYSTEM_STATUS:  return "SYSTEM";
    case Page::GRAPHS:         return "TRENDS";
    case Page::STATISTICS:     return "STATS";
    default:                   return "";
  }
}

const char *UIManager::pageFooterHint() const {
  // The old hint was 34 characters -- twice what fits across 160px, so the tail
  // was simply clipped. Page dots now carry the "where am I" half of it.
  return "Nav  Hold2:Menu";
}

void UIManager::renderCurrentPage(bool full) {
  if (full) {
    _disp->drawHeader(pageTitle(_page));
    _disp->drawFooter(pageFooterHint(), (uint8_t)_page, (uint8_t)Page::PAGE_COUNT);
    _disp->clearBody();
    _headerDot = 0;
    _headerWifiBars = 255;
  }

  // Sensor-link dot lives in the header band and is live on every page, so the
  // user never has to walk to System Status to notice the PMS dropped out.
  uint16_t dot = _sensors->data().pmsConnected ? Theme::GOOD : Theme::HAZARDOUS;
  if (dot != _headerDot) {
    _disp->drawStatusDot(150, 8, 3, dot, Theme::HEADER_BG);
    _headerDot = dot;
  }

  renderHeaderLink(full);

  switch (_page) {
    case Page::HOME:           renderHome(full); break;
    case Page::AIR_QUALITY:    renderAirQuality(full); break;
    case Page::GAS:            renderGas(full); break;
    case Page::DASHBOARD:      renderDashboard(full); break;
    case Page::SYSTEM_STATUS:  renderSystemStatus(full); break;
    case Page::GRAPHS:         renderGraphs(full); break;
    case Page::STATISTICS:     renderStatistics(full); break;
    default: break;
  }
}

// -----------------------------------------------------------------------------
// Header connectivity glyph -- signal strength in the bars, cloud sync state in
// the colour. Two facts in 11x8 px, on every page.
//
// The colour is deliberately driven by the cloud rather than the radio: a
// device that is associated to an access point but failing to upload is broken
// in the way that matters, and a green glyph there would be a lie. The bars
// stay honest about signal so a weak link is still diagnosable at a glance.
// -----------------------------------------------------------------------------
void UIManager::renderHeaderLink(bool full) {
  // An offline build has no radio and no uploader, so it gets no glyph -- an
  // always-grey widget would just be clutter asking to be explained.
  if (!_wifi || !_cloud || !_cloud->enabled()) return;

  bool linked = _wifi->isConnected();

  uint8_t bars = 0;
  if (linked) {
    int8_t r = _wifi->rssi();
    // Standard-ish Wi-Fi bar thresholds. -67 dBm is the usual "good enough for
    // real-time traffic" line, which is where this device sits on a desk.
    if (r >= -55)      bars = 4;
    else if (r >= -67) bars = 3;
    else if (r >= -78) bars = 2;
    else               bars = 1;
  }

  uint16_t color;
  switch (_cloud->state()) {
    case CloudState::ONLINE:         color = Theme::GOOD;       break;
    case CloudState::AUTHENTICATING: color = Theme::MODERATE;   break;
    case CloudState::ERROR:          color = Theme::HAZARDOUS;  break;
    // WAITING_NET with a live link means the uploader has not had its first
    // turn yet; without one the radio is the thing to look at.
    default:  color = linked ? Theme::MODERATE : Theme::TEXT_DIM; break;
  }

  if (full || bars != _headerWifiBars || color != _headerWifiColor) {
    _disp->drawWifiIndicator(132, 5, bars, color, Theme::HEADER_BG);
    _headerWifiBars = bars;
    _headerWifiColor = color;
  }
}

// -----------------------------------------------------------------------------
// Home -- one full-width hero card for the AQI, a row of three particulate
// chips under it, and the gas readout as a strip along the bottom edge. The
// hero's keyline carries the AQI band colour, so the page's dominant colour is
// the answer to "is the air OK".
// -----------------------------------------------------------------------------
void UIManager::renderHome(bool full) {
  const SensorData &d = _sensors->data();
  bool aqiOk = AirQuality::aqiValid(d);
  uint16_t aqi = AirQuality::calculateOverallAQI(d);
  AqiCategory aqiCat = AirQuality::getAQICategory(aqi);
  uint16_t aqiColor = aqiOk ? AirQuality::aqiCategoryColor(aqiCat) : Theme::TEXT_DIM;

  // The hero gives up 4px to the particulate chips. At 38 the AQI figure still
  // clears its card with room to spare (HERO is ~21px on a 38px card under a
  // 7px label), while 32 is what the chips need for a 9pt value to sit under
  // their label without touching the bottom edge.
  const int16_t heroX = 4, heroY = 20, heroW = SCREEN_WIDTH - 8, heroH = 38;
  const int16_t pmY = 62, pmH = 32, pmW = 48;
  const int16_t pmX[3] = {4, 56, 108};
  const int16_t gasTextY = 98, gasBarY = 108;

  if (full) {
    _home.aqi[0] = 0; _home.aqiCategory[0] = 0;
    _home.pm1[0] = 0; _home.pm25[0] = 0; _home.pm10[0] = 0;
    _home.gas[0] = 0; _home.gasStatus[0] = 0;
    _home.gasLevel = 255;

    _disp->drawCard(heroX, heroY, heroW, heroH, aqiColor);
    _home.heroAccent = aqiColor;
    _disp->drawText(heroX + 10, heroY + 4, TextStyle::SMALL, Theme::TEXT_DIM, "AIR QUALITY INDEX");

    // Particulate chips are labelled rather than iconned: three identical leaf
    // glyphs would tell the user nothing. Each keeps its own tint so the same
    // metric is the same colour here and on the Trends page.
    static const char *pmLabels[3] = {"PM1.0", "PM2.5", "PM10"};
    static const uint16_t pmTints[3] = {Theme::EXCELLENT, Theme::ACCENT, Theme::GOOD};
    for (uint8_t i = 0; i < 3; i++) {
      _disp->drawCard(pmX[i], pmY, pmW, pmH, pmTints[i]);
      _disp->drawText(pmX[i] + 7, pmY + 4, TextStyle::SMALL, Theme::TEXT_DIM, pmLabels[i]);
    }

    _disp->drawIcon(4, gasTextY - 1, 10, IconType::FLAME, Theme::ACCENT);
    _disp->drawText(18, gasTextY, TextStyle::SMALL, Theme::TEXT_DIM, "GAS");
  }

  char buf[16];

  // Hero keyline follows the band, repainted only when the band changes.
  if (_home.heroAccent != aqiColor) {
    _disp->tft().fillRect(heroX, heroY, Theme::KEYLINE, heroH, aqiColor);
    _home.heroAccent = aqiColor;
  }

  if (aqiOk) snprintf(buf, sizeof(buf), "%03u", aqi);
  else       snprintf(buf, sizeof(buf), "---");
  _disp->drawTextField(heroX + 10, heroY + 13, TextStyle::HERO, aqiColor, Theme::PANEL,
                        _home.aqi, sizeof(_home.aqi), buf);

  snprintf(buf, sizeof(buf), "%s", aqiOk ? AirQuality::aqiCategoryLabel(aqiCat) : "NO DATA");
  _disp->drawTextField(76, heroY + 26, TextStyle::SMALL, aqiColor, Theme::PANEL,
                        _home.aqiCategory, sizeof(_home.aqiCategory), buf);

  // Particulate values use the regular-weight 9pt face, not the bold one. The
  // bold at this size filled the card almost edge to edge, which is what made
  // it look oversized; the lighter face is the same body size but carries much
  // less ink, and the taller card above leaves clear space under it. The
  // built-in font at 2x was tried here and is a bitmap face -- it visibly
  // blocks up when scaled, so it is not an option for a figure the eye rests on.
  const int16_t pmValueY = pmY + 14;
  snprintf(buf, sizeof(buf), "%u", d.pm1_0);
  _disp->drawTextField(pmX[0] + 7, pmValueY, TextStyle::VALUE_LIGHT, Theme::TEXT, Theme::PANEL,
                        _home.pm1, sizeof(_home.pm1), buf);
  snprintf(buf, sizeof(buf), "%u", d.pm2_5);
  _disp->drawTextField(pmX[1] + 7, pmValueY, TextStyle::VALUE_LIGHT, Theme::TEXT, Theme::PANEL,
                        _home.pm25, sizeof(_home.pm25), buf);
  snprintf(buf, sizeof(buf), "%u", d.pm10);
  _disp->drawTextField(pmX[2] + 7, pmValueY, TextStyle::VALUE_LIGHT, Theme::TEXT, Theme::PANEL,
                        _home.pm10, sizeof(_home.pm10), buf);

  bool warming = !d.mqWarmedUp;
  snprintf(buf, sizeof(buf), "%d", (int)d.mq2);
  _disp->drawTextField(42, gasTextY, TextStyle::SMALL,
                        warming ? Theme::TEXT_DIM : Theme::TEXT, Theme::BG,
                        _home.gas, sizeof(_home.gas), warming ? "warming" : buf);

  _disp->drawTextField(92, gasTextY, TextStyle::SMALL,
                        warming ? Theme::ACCENT : gasStatusColor(d.mq2), Theme::BG,
                        _home.gasStatus, sizeof(_home.gasStatus),
                        warming ? "WARMUP" : gasStatusWord(d.mq2));

  // Full-width intensity strip along the bottom edge of the body.
  uint8_t gasLevel = DisplayManager::percentOf(d.mq2, 0, THRESHOLD_MQ2_DANGER) / 10;
  _disp->drawSegmentedBar(4, gasBarY, SCREEN_WIDTH - 8, 5, 10, _home.gasLevel, gasLevel, Theme::ACCENT);
}

// -----------------------------------------------------------------------------
// Sensor Dashboard -- every sensor as a card. 2x2 grid, one tint per metric,
// value set in the proportional face with the unit tucked underneath.
// -----------------------------------------------------------------------------
void UIManager::renderDashboard(bool full) {
  const SensorData &d = _sensors->data();
  static const char *labels[4] = {"PM1.0", "PM2.5", "PM10", "MQ-2"};
  static const char *units[4]  = {"ug/m3", "ug/m3", "ug/m3", "raw adc"};
  static const uint16_t tints[4] = {Theme::EXCELLENT, Theme::ACCENT, Theme::GOOD, Theme::MODERATE};

  const int16_t cardW = 74, cardH = 42, gapX = 4;
  const int16_t startX = 4, row1Y = 20, row2Y = row1Y + cardH + 3;
  const uint8_t VALUE_COUNT = 4;

  if (full) {
    for (uint8_t i = 0; i < VALUE_COUNT; i++) _dash.values[i][0] = 0;
    for (uint8_t i = 0; i < VALUE_COUNT; i++) {
      int16_t x = startX + (i % 2) * (cardW + gapX);
      int16_t y = (i / 2 == 0) ? row1Y : row2Y;
      _disp->drawCard(x, y, cardW, cardH, tints[i]);
      _disp->drawText(x + 8, y + 5,  TextStyle::SMALL, Theme::TEXT_DIM, labels[i]);
      _disp->drawText(x + 8, y + 31, TextStyle::SMALL, Theme::TEXT_DIM, units[i]);
    }
  }

  float values[VALUE_COUNT] = { (float)d.pm1_0, (float)d.pm2_5, (float)d.pm10, d.mq2 };
  char buf[10];
  for (uint8_t i = 0; i < VALUE_COUNT; i++) {
    int16_t x = startX + (i % 2) * (cardW + gapX);
    int16_t y = (i / 2 == 0) ? row1Y : row2Y;
    snprintf(buf, sizeof(buf), "%.0f", values[i]);
    _disp->drawTextField(x + 8, y + 14, TextStyle::VALUE, tints[i], Theme::PANEL,
                          _dash.values[i], sizeof(_dash.values[i]), buf);
  }

  // A small "live" pulse below the grid to show the page is actively updating.
  uint32_t now = millis();
  if (now - _dash.lastPulseMs >= 500) {
    _dash.lastPulseMs = now;
    _dash.livePulse = !_dash.livePulse;
    _disp->tft().fillCircle(SCREEN_WIDTH - 8, 110, 3, Theme::BG);
    _disp->tft().fillCircle(SCREEN_WIDTH - 8, 110, _dash.livePulse ? 3 : 2, Theme::ACCENT);
  }
}

// -----------------------------------------------------------------------------
// System Status -- uptime, memory, sensor health, frame rate, as a zebra-striped
// key/value table rather than two loose columns of text.
// -----------------------------------------------------------------------------
void UIManager::renderSystemStatus(bool full) {
  const SensorData &d = _sensors->data();
  const int16_t labelX = 10, valueX = 88, rowH = 13, startY = 20;
  static const char *rowLabels[7] = {
    "ESP32", "PMS5003", "MQ-2", "MEMORY", "UPTIME", "FRAMERATE", "RENDER"
  };

  // Alternating row fills; each field has to erase to the fill of its own row.
  auto rowY  = [&](uint8_t i) -> int16_t { return startY + i * rowH; };
  auto rowBg = [](uint8_t i) -> uint16_t { return (i % 2 == 0) ? Theme::PANEL : Theme::BG; };

  if (full) {
    _sys.uptime[0] = 0; _sys.heap[0] = 0; _sys.fps[0] = 0;
    _sys.frameTime[0] = 0; _sys.pmsStatus[0] = 0; _sys.mqStatus[0] = 0;

    for (uint8_t i = 0; i < 7; i++) {
      _disp->tft().fillRect(4, rowY(i), SCREEN_WIDTH - 8, rowH, rowBg(i));
      _disp->drawText(labelX, rowY(i) + 3, TextStyle::SMALL, Theme::TEXT_DIM, rowLabels[i]);
    }
    // The ESP32 row is static: if the loop stalls, the whole page stalls with it.
    _disp->drawIcon(valueX, rowY(0) + 3, 8, IconType::CHECK, Theme::GOOD);
    _disp->drawText(valueX + 14, rowY(0) + 3, TextStyle::SMALL, Theme::GOOD, "RUNNING");
  }

  char buf[16];

  snprintf(buf, sizeof(buf), "%s", d.pmsConnected ? "LINKED" : "NO SIGNAL");
  _disp->drawTextField(valueX, rowY(1) + 3, TextStyle::SMALL,
                        d.pmsConnected ? Theme::GOOD : Theme::HAZARDOUS, rowBg(1),
                        _sys.pmsStatus, sizeof(_sys.pmsStatus), buf);

  // The MQ element is always sampled; this row reports heater readiness.
  if (d.mqWarmedUp) snprintf(buf, sizeof(buf), "READY");
  else              snprintf(buf, sizeof(buf), "WARMUP %us", d.mqWarmupRemainingSec);
  _disp->drawTextField(valueX, rowY(2) + 3, TextStyle::SMALL,
                        d.mqWarmedUp ? Theme::GOOD : Theme::ACCENT, rowBg(2),
                        _sys.mqStatus, sizeof(_sys.mqStatus), buf);

  snprintf(buf, sizeof(buf), "%lu KB", (unsigned long)(ESP.getFreeHeap() / 1024));
  _disp->drawTextField(valueX, rowY(3) + 3, TextStyle::SMALL, Theme::TEXT, rowBg(3),
                        _sys.heap, sizeof(_sys.heap), buf);

  uint32_t upSec = millis() / 1000;
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", (unsigned long)(upSec / 3600),
            (unsigned long)((upSec / 60) % 60), (unsigned long)(upSec % 60));
  _disp->drawTextField(valueX, rowY(4) + 3, TextStyle::SMALL, Theme::TEXT, rowBg(4),
                        _sys.uptime, sizeof(_sys.uptime), buf);

  snprintf(buf, sizeof(buf), "%u fps", _fps);
  _disp->drawTextField(valueX, rowY(5) + 3, TextStyle::SMALL, Theme::TEXT, rowBg(5),
                        _sys.fps, sizeof(_sys.fps), buf);

  snprintf(buf, sizeof(buf), "%lu us", (unsigned long)_lastRenderDurationUs);
  _disp->drawTextField(valueX, rowY(6) + 3, TextStyle::SMALL, Theme::TEXT, rowBg(6),
                        _sys.frameTime, sizeof(_sys.frameTime), buf);
}
