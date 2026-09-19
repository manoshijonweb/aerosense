// =============================================================================
// UIPages.cpp -- render functions for Air Quality, Gas, Graphs and Statistics.
// Split out from UIManager.cpp purely to keep any single file from growing
// unwieldy; all methods still belong to UIManager.
// =============================================================================
#include "UIManager.h"

// Text laid over a filled status colour. Everything in the palette is dark
// enough to take the light ink except amber, which needs the opposite.
static uint16_t inkOn(uint16_t fill) {
  return (fill == Theme::MODERATE) ? Theme::BLACK : Theme::TEXT_INV;
}

// -----------------------------------------------------------------------------
// Page 1: Air Quality -- a full-width category banner in the band colour, then
// one row per particulate size: label, value, capsule bar, trend mark.
// -----------------------------------------------------------------------------
void UIManager::renderAirQuality(bool full) {
  const SensorData &d = _sensors->data();
  AqCategory cat = AirQuality::categoryForPM25(d.pm2_5);
  uint16_t catColor = AirQuality::categoryColor(cat);

  const int16_t x = 6, bannerY = 20, bannerH = 16;
  const int16_t barX = 62, barW = 76, barH = 8, trendX = 144;
  const int16_t rowY[3] = {42, 66, 90};

  if (full) {
    _aq.pm1[0] = 0; _aq.pm25[0] = 0; _aq.pm10[0] = 0; _aq.category[0] = 0;
    _aq.pm1Bar = 255; _aq.pm25Bar = 255; _aq.pm10Bar = 255;
    _aq.bannerColor = 0;
    // Screen state only -- prevPm/lastTrendMs carry the trend across visits.
    for (uint8_t i = 0; i < 3; i++) _aq.trendDrawn[i][0] = 0;

    static const char *rowLabels[3] = {"PM1.0", "PM2.5", "PM10"};
    for (uint8_t i = 0; i < 3; i++) {
      _disp->drawText(x, rowY[i], TextStyle::SMALL, Theme::TEXT_DIM, rowLabels[i]);
      if (i > 0) _disp->tft().drawFastHLine(x, rowY[i] - 6, SCREEN_WIDTH - 12, Theme::BORDER);
    }
  }

  // The banner is a solid block of the band colour, so it is repainted (and
  // its text forced to redraw) whenever the band moves.
  if (_aq.bannerColor != catColor) {
    _disp->tft().fillRect(4, bannerY, SCREEN_WIDTH - 8, bannerH, catColor);
    _disp->drawTextRight(SCREEN_WIDTH - 10, bannerY + 4, TextStyle::SMALL, inkOn(catColor), "PM2.5 BAND");
    _aq.bannerColor = catColor;
    _aq.category[0] = 0;
  }

  char buf[16];
  snprintf(buf, sizeof(buf), "%s", AirQuality::categoryLabel(cat));
  _disp->drawTextField(x + 4, bannerY + 4, TextStyle::SMALL, inkOn(catColor), catColor,
                        _aq.category, sizeof(_aq.category), buf);

  auto trendChar = [](float prev, float now) -> char {
    // No reference yet reads as "steady", not as "falling" -- the mark is
    // colour-coded, and green for improvement the device has not observed is a
    // claim it cannot make in its first interval.
    if (prev < 0) return '=';
    if (now > prev + 1.0f) return '+';
    if (now < prev - 1.0f) return '-';
    return '=';
  };

  // PM1.0
  snprintf(buf, sizeof(buf), "%u", d.pm1_0);
  _disp->drawTextField(x, rowY[0] + 8, TextStyle::VALUE, Theme::TEXT, Theme::BG, _aq.pm1, sizeof(_aq.pm1), buf);
  _disp->drawBar(barX, rowY[0] + 10, barW, barH, _aq.pm1Bar,
                  DisplayManager::percentOf(d.pm1_0, 0, THRESHOLD_PM25_HAZARDOUS), catColor);

  // PM2.5
  snprintf(buf, sizeof(buf), "%u", d.pm2_5);
  _disp->drawTextField(x, rowY[1] + 8, TextStyle::VALUE, Theme::TEXT, Theme::BG, _aq.pm25, sizeof(_aq.pm25), buf);
  _disp->drawBar(barX, rowY[1] + 10, barW, barH, _aq.pm25Bar,
                  DisplayManager::percentOf(d.pm2_5, 0, THRESHOLD_PM25_HAZARDOUS), catColor);

  // PM10
  snprintf(buf, sizeof(buf), "%u", d.pm10);
  _disp->drawTextField(x, rowY[2] + 8, TextStyle::VALUE, Theme::TEXT, Theme::BG, _aq.pm10, sizeof(_aq.pm10), buf);
  _disp->drawBar(barX, rowY[2] + 10, barW, barH, _aq.pm10Bar,
                  DisplayManager::percentOf(d.pm10, 0, THRESHOLD_PM25_HAZARDOUS * 1.5f), catColor);

  // Trend marks. Re-evaluated on AQ_TREND_INTERVAL_MS, not per frame: the
  // reference has to be far enough back that the reading can have moved, or
  // every mark sits on "=" permanently. The clock runs whether or not this page
  // is on screen, so arriving at it shows a real trend rather than a reset one.
  uint32_t nowMs = millis();
  if (nowMs - _aq.lastTrendMs >= AQ_TREND_INTERVAL_MS) {
    _aq.lastTrendMs = nowMs;
    const float live[3] = { (float)d.pm1_0, (float)d.pm2_5, (float)d.pm10 };
    for (uint8_t i = 0; i < 3; i++) {
      _aq.trend[i][0] = trendChar(_aq.prevPm[i], live[i]);
      _aq.prevPm[i] = live[i];
    }
  }

  for (uint8_t i = 0; i < 3; i++) {
    char t = _aq.trend[i][0];
    uint16_t c = (t == '+') ? Theme::HAZARDOUS : (t == '-') ? Theme::GOOD : Theme::TEXT_DIM;
    _disp->drawTextField(trendX, rowY[i] + 10, TextStyle::SMALL, c, Theme::BG,
                          _aq.trendDrawn[i], sizeof(_aq.trendDrawn[i]), _aq.trend[i]);
  }
}

// -----------------------------------------------------------------------------
// Page 2: Gas Monitoring -- the MQ-2 as the page's one hero figure, with the
// zone gauge under it and the session peak / alarm point as a footing pair.
// -----------------------------------------------------------------------------
void UIManager::renderGas(bool full) {
  const SensorData &d = _sensors->data();
  const int16_t cardX = 4, cardY = 20, cardW = SCREEN_WIDTH - 8, cardH = 54;
  const int16_t gaugeX = 74, gaugeY = 62, gaugeW = 74, gaugeH = 8;
  const int16_t labelY = 80, valueY = 90, noteY = 106;

  // While the heater settles the live value and gauge keep updating -- the user
  // can watch them fall -- but the status word says WARMING UP rather than
  // reporting a danger level that isn't real.
  bool warming = !d.mqWarmedUp;
  uint16_t accent = warming ? Theme::ACCENT : gasStatusColor(d.mq2);

  if (full) {
    _gasCache.mq2[0] = 0;
    _gasCache.mq2Status[0] = 0;
    _gasCache.mq2MarkerX = -32000;
    _gasCache.peak[0] = 0;
    _gasCache.warmup[0] = 0;

    _disp->drawCard(cardX, cardY, cardW, cardH, accent);
    _gasCache.accent = accent;
    _disp->drawIcon(cardX + 6, cardY + 5, 12, IconType::FLAME, Theme::ACCENT);
    _disp->drawText(cardX + 22, cardY + 6, TextStyle::SMALL, Theme::TEXT_DIM, "MQ-2  SMOKE / LPG");

    _disp->drawText(6,  labelY, TextStyle::SMALL, Theme::TEXT_DIM, "SESSION PEAK");
    _disp->drawText(92, labelY, TextStyle::SMALL, Theme::TEXT_DIM, "ALARM AT");

    char t[8];
    snprintf(t, sizeof(t), "%d", THRESHOLD_MQ2_DANGER);
    _disp->drawText(92, valueY, TextStyle::VALUE, Theme::TEXT_DIM, t);
  }

  if (_gasCache.accent != accent) {
    _disp->tft().fillRect(cardX, cardY, Theme::KEYLINE, cardH, accent);
    _gasCache.accent = accent;
  }

  char buf[22];

  snprintf(buf, sizeof(buf), "%d", (int)d.mq2);
  _disp->drawTextField(cardX + 6, cardY + 18, TextStyle::HERO, Theme::TEXT, Theme::PANEL,
                        _gasCache.mq2, sizeof(_gasCache.mq2), buf);

  _disp->drawTextField(gaugeX, cardY + 20, TextStyle::SMALL, accent, Theme::PANEL,
                        _gasCache.mq2Status, sizeof(_gasCache.mq2Status),
                        warming ? "WARMING UP" : gasStatusWord(d.mq2));

  _disp->drawZoneGauge(gaugeX, gaugeY, gaugeW, gaugeH, d.mq2, 0, MQ_RAW_MAX,
                        THRESHOLD_MQ2_HIGH, THRESHOLD_MQ2_DANGER, _gasCache.mq2MarkerX, Theme::PANEL);

  snprintf(buf, sizeof(buf), "%d", (int)_stats->mq2().maxV);
  _disp->drawTextField(6, valueY, TextStyle::VALUE, Theme::TEXT, Theme::BG,
                        _gasCache.peak, sizeof(_gasCache.peak),
                        _stats->mq2().count ? buf : "--");

  // Countdown line along the bottom. Clears itself the moment warm-up finishes
  // (drawTextField erases the old string).
  if (warming) snprintf(buf, sizeof(buf), "Warming up... %us", d.mqWarmupRemainingSec);
  else         buf[0] = 0;
  _disp->drawTextField(6, noteY, TextStyle::SMALL, Theme::ACCENT, Theme::BG,
                        _gasCache.warmup, sizeof(_gasCache.warmup), buf);
}

// -----------------------------------------------------------------------------
// Page 5: History Graphs -- 2x2 plots, each under its own tinted title chip,
// redrawn only when a new sample lands.
// -----------------------------------------------------------------------------
void UIManager::renderGraphs(bool full) {
  const int16_t cellW = 74, cellH = 44, gapX = 4, gapY = 4;
  const int16_t startX = 4, startY = 20;
  const int16_t chipH = 10;

  struct Cell { const char *label; uint16_t color; GraphMetric metric; };
  // With the MQ-7 gone the CO plot has nothing to show, so the fourth cell
  // carries PM1.0 instead and the 2x2 grid stays full. Tints match the ones
  // the Home and Sensors pages give the same metrics.
  static const Cell cells[4] = {
    {"PM1.0", Theme::EXCELLENT, GraphMetric::PM1},
    {"PM2.5", Theme::ACCENT,    GraphMetric::PM25},
    {"PM10",  Theme::GOOD,      GraphMetric::PM10},
    {"GAS",   Theme::MODERATE,  GraphMetric::GAS},
  };

  bool dirty = _graphs->consumeDirty();
  if (!full && !dirty) return;

  for (uint8_t i = 0; i < 4; i++) {
    int16_t x = startX + (i % 2) * (cellW + gapX);
    int16_t y = startY + (i / 2) * (cellH + gapY);

    if (full) {
      _disp->tft().fillRect(x, y, cellW, chipH, Theme::PANEL_HI);
      _disp->tft().fillRect(x, y, Theme::KEYLINE, chipH, cells[i].color);
      _disp->drawText(x + 7, y + 1, TextStyle::SMALL, Theme::TEXT, cells[i].label);
    }

    _graphs->drawGraph(*_disp, x, y + chipH, cellW, cellH - chipH, cells[i].color, cells[i].metric);
  }
}

// -----------------------------------------------------------------------------
// Page 6: Statistics -- min / avg / max per metric, as a proper table with an
// accent header rule.
// -----------------------------------------------------------------------------
void UIManager::renderStatistics(bool full) {
  // Value cell is one snprintf'd string "MIN AVG MAX" with 4-char left-justified
  // fields at SMALL (6px/char): each field+space is a 30px stride from colMin,
  // which is exactly the spacing of the three column headings.
  const int16_t x = 8, colMin = 64, colAvg = 94, colMax = 124;
  const int16_t headY = 20, headH = 14, startY = 38, rowH = 18, rowGap = 2;

  if (full) {
    for (uint8_t i = 0; i < 3; i++) _statsCache.rows[i][0] = 0;

    _disp->tft().fillRect(4, headY, SCREEN_WIDTH - 8, headH, Theme::ACCENT);
    _disp->drawText(x,      headY + 3, TextStyle::SMALL, Theme::TEXT_INV, "METRIC");
    _disp->drawText(colMin, headY + 3, TextStyle::SMALL, Theme::TEXT_INV, "MIN");
    _disp->drawText(colAvg, headY + 3, TextStyle::SMALL, Theme::TEXT_INV, "AVG");
    _disp->drawText(colMax, headY + 3, TextStyle::SMALL, Theme::TEXT_INV, "MAX");

    static const char *rowLabels[3] = {"PM2.5", "PM10", "MQ-2"};
    for (uint8_t i = 0; i < 3; i++) {
      int16_t y = startY + i * (rowH + rowGap);
      _disp->tft().fillRect(4, y, SCREEN_WIDTH - 8, rowH, Theme::PANEL);
      _disp->tft().drawFastHLine(4, y + rowH - 1, SCREEN_WIDTH - 8, Theme::BORDER);
      _disp->drawText(x, y + 5, TextStyle::SMALL, Theme::TEXT_DIM, rowLabels[i]);
    }

    _disp->drawText(x, 102, TextStyle::SMALL, Theme::TEXT_DIM, "since last reset");
  }

  auto drawRow = [&](uint8_t rowIndex, const MetricStats &m, uint8_t decimals) {
    int16_t y = startY + rowIndex * (rowH + rowGap);
    char cell[20];
    if (m.count == 0) {
      snprintf(cell, sizeof(cell), "--   --   --");
    } else if (decimals == 0) {
      snprintf(cell, sizeof(cell), "%-4d %-4d %-4d", (int)m.minV, (int)m.average(), (int)m.maxV);
    } else {
      snprintf(cell, sizeof(cell), "%-4.1f %-4.1f %-4.1f", m.minV, m.average(), m.maxV);
    }
    _disp->drawTextField(colMin, y + 5, TextStyle::SMALL, Theme::TEXT, Theme::PANEL,
                          _statsCache.rows[rowIndex], sizeof(_statsCache.rows[rowIndex]), cell);
  };

  drawRow(0, _stats->pm25(), 0);
  drawRow(1, _stats->pm10(), 0);
  drawRow(2, _stats->mq2(), 0);
}
