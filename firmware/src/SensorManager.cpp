#include "SensorManager.h"

void SensorManager::begin() {
  _pmsSerial.begin(9600, SERIAL_8N1, PIN_PMS_RX, PIN_PMS_TX);

  // State the ADC configuration rather than inheriting whatever the core
  // happens to default to: 12-bit, and the widest attenuation so the full
  // 0..3.3V swing of the MQ module's analog output is in range.
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_MQ2, ADC_11db);

  // Prime the MQ readings immediately so pages don't show a "0" flash on boot.
  _data.mq2 = readMq2Raw();
  _mqInitialised = true;

  // The warm-up clock starts at boot and is never restarted afterwards -- it
  // tracks the heaters, not anything the network or the UI does.
  _mqWarmupStartMs = millis();
}

void SensorManager::update() {
  updatePMS();
  updateMQ();
  updateMqWarmup();

  bool connected = (millis() - _lastValidPmsFrameMs) < PMS_STALE_TIMEOUT_MS && _lastValidPmsFrameMs != 0;

  // A dropout invalidates the window: averaging samples from before the gap
  // with samples from after it reports a level that existed at neither time.
  // Clearing on the falling edge means the first reading after a reconnect is
  // that frame alone, then the window refills normally.
  // The published values are deliberately left standing at the last real
  // reading rather than zeroed: zero is a valid, excellent PM level, so a
  // zeroed readout claims pristine air at exactly the moment the device knows
  // nothing. Staleness is signalled instead -- the header dot, the System
  // Status row, and a missing AQI on Home.
  if (_pmsWasConnected && !connected) resetPmAverage();
  _pmsWasConnected = connected;
  _data.pmsConnected = connected;
}

void SensorManager::resetPmAverage() {
  _pmHistoryIndex = 0;
  _pmHistoryCount = 0;
}

// -----------------------------------------------------------------------------
// PMS5003 -- read every byte currently sitting in the UART buffer (never
// blocks waiting for more) and feed a tiny state machine that resyncs on the
// 0x42 0x4D start marker and validates the frame checksum before accepting it.
// -----------------------------------------------------------------------------
void SensorManager::updatePMS() {
  while (_pmsSerial.available() > 0) {
    uint8_t b = (uint8_t)_pmsSerial.read();

    if (_pmsIndex == 0) {
      if (b == 0x42) _pmsBuffer[_pmsIndex++] = b;
      // else: not a start byte, keep discarding until we see one.
      continue;
    }
    if (_pmsIndex == 1) {
      if (b == 0x4D) {
        _pmsBuffer[_pmsIndex++] = b;
      } else {
        // Not a valid second start byte -- resync.
        _pmsIndex = 0;
        if (b == 0x42) _pmsBuffer[_pmsIndex++] = b;
      }
      continue;
    }

    _pmsBuffer[_pmsIndex++] = b;
    if (_pmsIndex >= PMS_FRAME_LEN) {
      if (tryParsePmsFrame()) {
        _lastValidPmsFrameMs = millis();
      }
      _pmsIndex = 0; // ready for the next frame regardless of validity
    }
  }
}

bool SensorManager::tryParsePmsFrame() {
  // Frame length field (bytes 2..3) is a fixed 28 on the PMS5003: 13 data words
  // plus the checksum word. Checking it rejects a misaligned window that
  // happens to start with 0x42 0x4D inside the data before the checksum gets a
  // chance to accept it by coincidence.
  uint16_t frameLen = ((uint16_t)_pmsBuffer[2] << 8) | _pmsBuffer[3];
  if (frameLen != 28) return false;

  // Checksum = sum of bytes[0..29], compared against the big-endian value in bytes[30..31].
  uint16_t sum = 0;
  for (uint8_t i = 0; i < 30; i++) sum += _pmsBuffer[i];
  uint16_t checksum = ((uint16_t)_pmsBuffer[30] << 8) | _pmsBuffer[31];
  if (sum != checksum) return false; // corrupted packet -- ignore

  uint16_t pm1  = ((uint16_t)_pmsBuffer[10] << 8) | _pmsBuffer[11]; // atmospheric PM1.0
  uint16_t pm25 = ((uint16_t)_pmsBuffer[12] << 8) | _pmsBuffer[13]; // atmospheric PM2.5
  uint16_t pm10 = ((uint16_t)_pmsBuffer[14] << 8) | _pmsBuffer[15]; // atmospheric PM10

  // Sanity clamp -- PMS5003 tops out well under this; reject wild glitches.
  if (pm1 > 2000 || pm25 > 2000 || pm10 > 2000) return false;

  _pm1History[_pmHistoryIndex] = pm1;
  _pm25History[_pmHistoryIndex] = pm25;
  _pm10History[_pmHistoryIndex] = pm10;
  _pmHistoryIndex = (_pmHistoryIndex + 1) % PMS_AVERAGE_SAMPLES;
  if (_pmHistoryCount < PMS_AVERAGE_SAMPLES) _pmHistoryCount++;

  uint32_t sum1 = 0, sum25 = 0, sum10 = 0;
  for (uint8_t i = 0; i < _pmHistoryCount; i++) {
    sum1 += _pm1History[i];
    sum25 += _pm25History[i];
    sum10 += _pm10History[i];
  }
  // Round rather than truncate: at the single-digit readings that dominate
  // indoor air, dropping the remainder is a systematic bias downwards of up to
  // a whole ug/m3 on every published value, not a rounding detail.
  uint32_t half = _pmHistoryCount / 2;
  _data.pm1_0 = (sum1 + half) / _pmHistoryCount;
  _data.pm2_5 = (sum25 + half) / _pmHistoryCount;
  _data.pm10  = (sum10 + half) / _pmHistoryCount;
  return true;
}

// -----------------------------------------------------------------------------
// MQ2 -- oversampled on a fixed tick, then exponentially smoothed. The two do
// different jobs: oversampling removes conversion noise without costing any
// response time, the EMA follows the element's own settling.
// -----------------------------------------------------------------------------
float SensorManager::readMq2Raw() {
  uint32_t acc = 0;
  for (uint8_t i = 0; i < MQ_OVERSAMPLE; i++) acc += (uint32_t)analogRead(PIN_MQ2);
  return (float)acc / (float)MQ_OVERSAMPLE;
}

void SensorManager::updateMQ() {
  uint32_t now = millis();
  if (now - _lastMqReadMs < MQ_READ_INTERVAL_MS) return;
  _lastMqReadMs = now;

  float rawMq2 = readMq2Raw();
  _data.mq2 = _data.mq2 + MQ_EMA_ALPHA * (rawMq2 - _data.mq2);
}

// -----------------------------------------------------------------------------
// MQ warm-up countdown. Purely a clock -- sampling above is untouched, so the
// sensors keep settling and the UI keeps showing live values throughout.
// -----------------------------------------------------------------------------
void SensorManager::updateMqWarmup() {
  if (_data.mqWarmedUp) return; // latched: only a real reboot restarts this

  uint32_t elapsed = millis() - _mqWarmupStartMs;
  if (elapsed >= MQ_WARMUP_TIME_MS) {
    _data.mqWarmedUp = true;
    _data.mqWarmupRemainingSec = 0;

    // The settled reading at this instant is the best clean-air reference the
    // device will ever have of itself. Adopt it -- but only if it lands where a
    // clean-air reading plausibly can; outside that band something is wrong
    // (element or wiring low, genuinely dirty air high) and a fixed, wrong-but-
    // known constant beats a measured, wrong-and-unknown one.
#if MQ_AUTO_BASELINE
    if (_data.mq2 >= MQ_BASELINE_MIN_RAW && _data.mq2 <= MQ_BASELINE_MAX_RAW) {
      _data.mq2Baseline = _data.mq2;
    }
#endif
  } else {
    // Round up so the countdown reaches 0 exactly when warm-up ends.
    _data.mqWarmupRemainingSec = (uint16_t)((MQ_WARMUP_TIME_MS - elapsed + 999) / 1000);
  }
}
