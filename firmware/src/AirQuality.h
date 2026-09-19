#pragma once
// =============================================================================
// AirQuality -- turns raw sensor numbers into human categories: a PM2.5
// category for the Air Quality page, one composite "overall environment"
// score/category for the internal warning logic, and the standards-based Air
// Quality Index shown on the Home page and uploaded to Firebase.
//
// The AQI half is deliberately kept as pure functions of a concentration, with
// no knowledge of how the sensors are read -- see the breakpoint tables in
// AirQuality.cpp.
// =============================================================================
#include <Arduino.h>
#include "Config.h"
#include "Theme.h"
#include "SensorManager.h"

enum class AqCategory : uint8_t { EXCELLENT, GOOD, MODERATE, POOR, HAZARDOUS };

// US EPA AQI bands. Only these six exist in the standard.
enum class AqiCategory : uint8_t {
  GOOD,            //   0 - 50
  MODERATE,        //  51 - 100
  SENSITIVE,       // 101 - 150  (Unhealthy for Sensitive Groups)
  UNHEALTHY,       // 151 - 200
  VERY_UNHEALTHY,  // 201 - 300
  HAZARDOUS        // 301 - 500
};

namespace AirQuality {
  AqCategory categoryForPM25(uint16_t pm25);
  const char *categoryLabel(AqCategory cat);
  uint16_t categoryColor(AqCategory cat);

  // Composite 0..100 environmental score from every live sensor.
  uint8_t overallScore(const SensorData &d);
  AqCategory categoryForScore(uint8_t score);

  // ---- Air Quality Index (US EPA breakpoint interpolation) ------------------
  // Sub-indices for the two particulate pollutants the PMS5003 measures in the
  // standard's own units (ug/m3). Both return 0..500.
  uint16_t calculatePM25AQI(uint16_t pm25);
  uint16_t calculatePM10AQI(uint16_t pm10);

  // Overall AQI = the WORST of the pollutant sub-indices, per the standard.
  // Never averaged.
  uint16_t calculateOverallAQI(const SensorData &d);

  // AQI is only meaningful while the PMS5003 is actually delivering frames;
  // the MQ sensors are uncalibrated raw ADC and deliberately take no part.
  bool aqiValid(const SensorData &d);

  AqiCategory getAQICategory(uint16_t aqi);
  const char *aqiCategoryLabel(AqiCategory cat);  // short form, fits the TFT
  uint16_t aqiCategoryColor(AqiCategory cat);     // mapped onto the existing palette
}
