#include "StatisticsManager.h"

// Each metric is gated on its own validity, so a sensor that is late, absent or
// still settling contributes nothing rather than contributing a placeholder.
// Without this the session minimum for every particulate is pinned to the 0 the
// struct starts at until a PMS frame arrives, and the MQ-2 session peak is the
// warm-up spike rather than anything the air did.
void StatisticsManager::update(const SensorData &d) {
  uint32_t now = millis();
  if (now - _lastSampleMs < STATS_SAMPLE_INTERVAL_MS) return;
  _lastSampleMs = now;

  if (d.pmsConnected) {
    _pm25.feed(d.pm2_5);
    _pm10.feed(d.pm10);
  }
  if (d.mqWarmedUp) {
    _mq2.feed(d.mq2);
  }
}

void StatisticsManager::reset() {
  _pm25.reset();
  _mq2.reset();
  _pm10.reset();
}
