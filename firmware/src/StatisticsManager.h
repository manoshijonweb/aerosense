#pragma once
// =============================================================================
// StatisticsManager -- running min/max/average for the metrics shown on the
// Statistics page. Cheap O(1) accumulation, no stored history required.
// =============================================================================
#include <Arduino.h>
#include "Config.h"
#include "SensorManager.h"

struct MetricStats {
  float minV = NAN;
  float maxV = NAN;
  float sum = 0;
  uint32_t count = 0;

  float average() const { return count ? (sum / count) : 0.0f; }
  void reset() { minV = NAN; maxV = NAN; sum = 0; count = 0; }
  void feed(float value) {
    if (isnan(minV) || value < minV) minV = value;
    if (isnan(maxV) || value > maxV) maxV = value;
    sum += value;
    count++;
  }
};

class StatisticsManager {
public:
  void update(const SensorData &d);
  void reset();

  const MetricStats &pm25() const { return _pm25; }
  const MetricStats &mq2() const { return _mq2; }
  const MetricStats &pm10() const { return _pm10; }

private:
  MetricStats _pm25, _pm10, _mq2;
  uint32_t _lastSampleMs = 0;
};
