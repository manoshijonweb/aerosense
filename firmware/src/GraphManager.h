#pragma once
// =============================================================================
// GraphManager -- fixed-size ring buffers of recent history for the Graphs
// page, plus a self-contained scrolling line-plot renderer. New sample is
// pushed on a slow timer; drawGraph() only needs to be called when the page
// is dirty (see consumeDirty()).
// =============================================================================
#include <Arduino.h>
#include "Config.h"
#include "Theme.h"
#include "SensorManager.h"
#include "DisplayManager.h"

enum class GraphMetric : uint8_t { PM1, PM25, PM10, GAS };

class GraphManager {
public:
  void update(const SensorData &d);
  bool consumeDirty();

  void drawGraph(DisplayManager &disp, int16_t x, int16_t y, int16_t w, int16_t h,
                 uint16_t color, GraphMetric metric) const;

private:
  float _pm1[GRAPH_HISTORY_LENGTH] = {0};
  float _pm25[GRAPH_HISTORY_LENGTH] = {0};
  float _pm10[GRAPH_HISTORY_LENGTH] = {0};
  float _gas[GRAPH_HISTORY_LENGTH] = {0};

  uint8_t _head = 0;
  uint8_t _count = 0;
  uint32_t _lastSampleMs = 0;
  bool _dirty = false;

  const float *bufferFor(GraphMetric metric) const;
  float valueAt(const float *buf, uint8_t logicalIndex) const;
};
