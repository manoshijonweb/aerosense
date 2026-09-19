#pragma once
// =============================================================================
// SensorManager -- owns all physical sensor I/O.
// Every read path is non-blocking: PMS5003 is parsed byte-by-byte as bytes
// arrive, the MQ2 is sampled + exponentially smoothed on a timer.
// =============================================================================
#include <Arduino.h>
#include "Config.h"

// Snapshot of everything the rest of the firmware needs to know about the
// physical environment. Passed around by const reference -- read only.
struct SensorData {
  // Particulate matter, PMS5003 atmospheric values (ug/m3), rolling-averaged.
  uint16_t pm1_0 = 0;
  uint16_t pm2_5 = 0;
  uint16_t pm10  = 0;
  bool pmsConnected = false;

  // Gas sensor, smoothed raw ADC counts (0..4095)
  float mq2 = 0.0f;

  // MQ heater warm-up state, measured from power-on. The readings above stay
  // live throughout, but must be treated as unreliable -- and must not raise
  // warnings, feed statistics or feed the score -- until mqWarmedUp goes true.
  bool mqWarmedUp = false;
  uint16_t mqWarmupRemainingSec = 0;

  // The clean-air reading the gas score is measured against. Settled at the end
  // of warm-up when MQ_AUTO_BASELINE is on, otherwise the Config constant.
  float mq2Baseline = MQ2_CLEAN_AIR_RAW;
};

class SensorManager {
public:
  void begin();
  void update();

  const SensorData &data() const { return _data; }

private:
  SensorData _data;

  // ---- PMS5003 (non-blocking frame parser) ----
  HardwareSerial _pmsSerial{2};
  static const uint8_t PMS_FRAME_LEN = 32;
  uint8_t _pmsBuffer[PMS_FRAME_LEN];
  uint8_t _pmsIndex = 0;
  uint32_t _lastValidPmsFrameMs = 0;
  bool _pmsWasConnected = false;   // edge detect, so a dropout can clear the average

  uint16_t _pm1History[PMS_AVERAGE_SAMPLES] = {0};
  uint16_t _pm25History[PMS_AVERAGE_SAMPLES] = {0};
  uint16_t _pm10History[PMS_AVERAGE_SAMPLES] = {0};
  uint8_t _pmHistoryIndex = 0;
  uint8_t _pmHistoryCount = 0;

  void updatePMS();
  bool tryParsePmsFrame(); // validates checksum + extracts values from _pmsBuffer
  void resetPmAverage();   // drop the window so stale samples can't survive a dropout

  // ---- MQ2 ----
  uint32_t _lastMqReadMs = 0;
  uint32_t _mqWarmupStartMs = 0;   // stamped once in begin(), i.e. at power-on
  bool _mqInitialised = false;
  static float readMq2Raw();       // oversampled single reading, in ADC counts
  void updateMQ();
  void updateMqWarmup();
};
