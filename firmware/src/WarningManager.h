#pragma once
// =============================================================================
// WarningManager -- watches sensor thresholds in the background and drives a
// transient full-screen banner. A warning auto-dismisses after
// WARNING_DISPLAY_MS and then can't re-fire for WARNING_COOLDOWN_MS, so a
// borderline reading doesn't spam the screen every tick.
// =============================================================================
#include <Arduino.h>
#include "Config.h"
#include "Theme.h"
#include "SensorManager.h"
#include "AirQuality.h"

enum class WarningType : uint8_t {
  NONE = 0,
  SMOKE_DETECTED,
  POOR_AIR_QUALITY,
  COUNT
};

class WarningManager {
public:
  void update(const SensorData &d, AqCategory overallCategory);

  // Acknowledge the banner early. The cooldown is applied exactly as it is on a
  // timed expiry, so dismissing cannot be used to make the same warning fire
  // again immediately.
  void dismiss();

  bool isActive() const { return _activeType != WarningType::NONE; }
  const char *message() const;
  uint16_t color() const;

private:
  WarningType _activeType = WarningType::NONE;
  uint32_t _activeSinceMs = 0;
  uint32_t _cooldownUntilMs[(uint8_t)WarningType::COUNT] = {0};

  WarningType detect(const SensorData &d, AqCategory overallCategory) const;
};
