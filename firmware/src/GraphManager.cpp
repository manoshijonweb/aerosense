#include "GraphManager.h"

void GraphManager::update(const SensorData &d) {
  uint32_t now = millis();
  if (now - _lastSampleMs < GRAPH_SAMPLE_INTERVAL_MS) return;
  _lastSampleMs = now;

  // All four buffers share one timeline, so a metric that has nothing valid to
  // say still occupies its slot -- it stores NAN, and the renderer breaks the
  // trace there. Recording a placeholder instead would draw a flat run at zero
  // (or at the warm-up spike) that reads as a real measurement.
  _pm1[_head]  = d.pmsConnected ? (float)d.pm1_0 : NAN;
  _pm25[_head] = d.pmsConnected ? (float)d.pm2_5 : NAN;
  _pm10[_head] = d.pmsConnected ? (float)d.pm10  : NAN;
  _gas[_head]  = d.mqWarmedUp   ? d.mq2          : NAN;

  _head = (_head + 1) % GRAPH_HISTORY_LENGTH;
  if (_count < GRAPH_HISTORY_LENGTH) _count++;
  _dirty = true;
}

bool GraphManager::consumeDirty() {
  if (!_dirty) return false;
  _dirty = false;
  return true;
}

const float *GraphManager::bufferFor(GraphMetric metric) const {
  switch (metric) {
    case GraphMetric::PM1:  return _pm1;
    case GraphMetric::PM25: return _pm25;
    case GraphMetric::PM10: return _pm10;
    default:                return _gas;
  }
}

float GraphManager::valueAt(const float *buf, uint8_t logicalIndex) const {
  uint8_t physical = (_head + GRAPH_HISTORY_LENGTH - _count + logicalIndex) % GRAPH_HISTORY_LENGTH;
  return buf[physical];
}

void GraphManager::drawGraph(DisplayManager &disp, int16_t x, int16_t y, int16_t w, int16_t h,
                              uint16_t color, GraphMetric metric) const {
  Adafruit_ST7735 &tft = disp.tft();
  // Plot field is a card surface like everything else on the page, with a
  // dashed midline so the trace has something to be read against.
  tft.fillRect(x, y, w, h, Theme::PANEL);
  tft.drawRect(x, y, w, h, Theme::BORDER);
  for (int16_t gx = x + 3; gx < x + w - 2; gx += 4) tft.drawPixel(gx, y + h / 2, Theme::TRACK);

  if (_count < 2) return;

  // Scale to the valid samples only -- one NAN slot must not collapse the range.
  const float *buf = bufferFor(metric);
  float minV = 0, maxV = 0;
  uint8_t validCount = 0;
  for (uint8_t j = 0; j < _count; j++) {
    float v = valueAt(buf, j);
    if (isnan(v)) continue;
    if (validCount == 0 || v < minV) minV = v;
    if (validCount == 0 || v > maxV) maxV = v;
    validCount++;
  }
  if (validCount < 2) return;
  if (maxV - minV < 0.5f) { maxV += 0.5f; minV -= 0.5f; }

  // prevX/prevY hold the last point actually plotted, so they survive a gap and
  // still locate the newest sample once the loop ends. havePrev is only about
  // whether the next segment may be joined to it.
  int16_t prevX = 0, prevY = 0;
  bool havePrev = false;
  for (uint8_t j = 0; j < _count; j++) {
    float v = valueAt(buf, j);
    if (isnan(v)) {
      havePrev = false;  // gap in the record: lift the pen rather than bridge it
      continue;
    }

    uint8_t k = GRAPH_HISTORY_LENGTH - _count + j;
    // Inset one pixel on each side so neither the trace nor its second row can
    // land on the frame.
    int16_t px = x + 1 + (int32_t)(w - 3) * k / (GRAPH_HISTORY_LENGTH - 1);
    float norm = (v - minV) / (maxV - minV);
    int16_t py = y + (h - 3) - (int16_t)(norm * (h - 6));
    if (havePrev) {
      // Two rows: a 1px trace disappears against a light field.
      tft.drawLine(prevX, prevY, px, py, color);
      tft.drawLine(prevX, prevY + 1, px, py + 1, color);
    }
    prevX = px;
    prevY = py;
    havePrev = true;
  }

  // Mark the newest valid sample so "now" is obvious at a glance. validCount >= 2
  // above guarantees at least one point was plotted.
  int16_t dx = prevX;
  if (dx > x + w - 3) dx = x + w - 3;
  if (dx < x + 2)     dx = x + 2;
  tft.fillRect(dx - 1, prevY - 1, 3, 3, color);
}
