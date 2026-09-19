#include "WarningManager.h"

WarningType WarningManager::detect(const SensorData &d, AqCategory overallCategory) const {
  // The MQ heater reads near full scale while warming up. Suppress -- never
  // re-threshold -- the gas alarm until it has settled; the threshold below is
  // the original one and applies unchanged from that point on.
  if (d.mqWarmedUp) {
    if (d.mq2 >= THRESHOLD_MQ2_DANGER) return WarningType::SMOKE_DETECTED;
  }
  if (overallCategory == AqCategory::HAZARDOUS || overallCategory == AqCategory::POOR) {
    return WarningType::POOR_AIR_QUALITY;
  }
  return WarningType::NONE;
}

void WarningManager::update(const SensorData &d, AqCategory overallCategory) {
  uint32_t now = millis();

  if (_activeType != WarningType::NONE) {
    if (now - _activeSinceMs >= WARNING_DISPLAY_MS) dismiss();
    return; // don't evaluate a new warning while one is already showing
  }

  WarningType candidate = detect(d, overallCategory);
  if (candidate == WarningType::NONE) return;

  // Signed difference, not `now >= deadline`: millis() wraps at ~49.7 days, and
  // a plain comparison across the wrap suppresses every warning for the length
  // of a cooldown that was set just before it.
  if ((int32_t)(now - _cooldownUntilMs[(uint8_t)candidate]) >= 0) {
    _activeType = candidate;
    _activeSinceMs = now;
  }
}

void WarningManager::dismiss() {
  if (_activeType == WarningType::NONE) return;
  _cooldownUntilMs[(uint8_t)_activeType] = millis() + WARNING_COOLDOWN_MS;
  _activeType = WarningType::NONE;
}

const char *WarningManager::message() const {
  switch (_activeType) {
    case WarningType::SMOKE_DETECTED:   return "Smoke Detected!";
    case WarningType::POOR_AIR_QUALITY: return "Poor Air Quality";
    default:                            return "";
  }
}

uint16_t WarningManager::color() const {
  switch (_activeType) {
    case WarningType::SMOKE_DETECTED:   return Theme::HAZARDOUS;
    case WarningType::POOR_AIR_QUALITY: return Theme::POOR;
    default:                            return Theme::TEXT;
  }
}
