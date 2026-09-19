#include "AirQuality.h"

namespace {
  // Linear map with clamping -- used to turn a raw sensor value into a 0..100 sub-score.
  float clampMapInverted(float value, float goodAt, float badAt) {
    // goodAt maps to 100, badAt (and beyond) maps to 0.
    if (badAt == goodAt) return 100.0f;
    float t = (value - goodAt) / (badAt - goodAt);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return 100.0f - t * 100.0f;
  }

  // ---- AQI breakpoint tables (US EPA, 24-hour averaging period) -------------
  // Concentrations in ug/m3, which is exactly what the PMS5003 reports, so no
  // unit conversion is involved. Each row is one band: the AQI within a band is
  // a straight linear interpolation between its endpoints.
  struct AqiBand { float cLow, cHigh; uint16_t iLow, iHigh; };

  const AqiBand PM25_BANDS[] = {
    {   0.0f,  12.0f,   0,  50 },
    {  12.1f,  35.4f,  51, 100 },
    {  35.5f,  55.4f, 101, 150 },
    {  55.5f, 150.4f, 151, 200 },
    { 150.5f, 250.4f, 201, 300 },
    { 250.5f, 350.4f, 301, 400 },
    { 350.5f, 500.4f, 401, 500 },
  };

  const AqiBand PM10_BANDS[] = {
    {   0.0f,  54.0f,   0,  50 },
    {  55.0f, 154.0f,  51, 100 },
    { 155.0f, 254.0f, 101, 150 },
    { 255.0f, 354.0f, 151, 200 },
    { 355.0f, 424.0f, 201, 300 },
    { 425.0f, 504.0f, 301, 400 },
    { 505.0f, 604.0f, 401, 500 },
  };

  // The standard AQI equation:
  //   AQI = (I_high - I_low) / (C_high - C_low) * (C - C_low) + I_low
  uint16_t aqiFromBands(float c, const AqiBand *bands, uint8_t count) {
    if (c <= 0.0f) return 0;
    // Above the top of the table the index is capped at 500 ("beyond AQI").
    if (c >= bands[count - 1].cHigh) return 500;

    for (uint8_t i = 0; i < count; i++) {
      const AqiBand &b = bands[i];
      // Bands are contiguous once rounded, so anything at or below cHigh that
      // wasn't caught by an earlier band belongs to this one.
      if (c <= b.cHigh) {
        float span = b.cHigh - b.cLow;
        if (span <= 0.0f) return b.iLow;
        float value = (float)(b.iHigh - b.iLow) / span * (c - b.cLow) + b.iLow;
        if (value < 0.0f) value = 0.0f;
        return (uint16_t)(value + 0.5f);
      }
    }
    return 500;
  }
}

AqCategory AirQuality::categoryForPM25(uint16_t pm25) {
  if (pm25 <= THRESHOLD_PM25_GOOD) return AqCategory::EXCELLENT;
  if (pm25 <= THRESHOLD_PM25_MODERATE) return AqCategory::GOOD;
  if (pm25 <= THRESHOLD_PM25_POOR) return AqCategory::MODERATE;
  if (pm25 <= THRESHOLD_PM25_HAZARDOUS) return AqCategory::POOR;
  return AqCategory::HAZARDOUS;
}

const char *AirQuality::categoryLabel(AqCategory cat) {
  switch (cat) {
    case AqCategory::EXCELLENT: return "Excellent";
    case AqCategory::GOOD:      return "Good";
    case AqCategory::MODERATE:  return "Moderate";
    case AqCategory::POOR:      return "Poor";
    default:                    return "Hazardous";
  }
}

uint16_t AirQuality::categoryColor(AqCategory cat) {
  switch (cat) {
    case AqCategory::EXCELLENT: return Theme::EXCELLENT;
    case AqCategory::GOOD:      return Theme::GOOD;
    case AqCategory::MODERATE:  return Theme::MODERATE;
    case AqCategory::POOR:      return Theme::POOR;
    default:                    return Theme::HAZARDOUS;
  }
}

uint8_t AirQuality::overallScore(const SensorData &d) {
  float pm25Score = clampMapInverted(d.pm2_5, THRESHOLD_PM25_GOOD, THRESHOLD_PM25_HAZARDOUS);
  float pm10Score = clampMapInverted(d.pm10, THRESHOLD_PM25_GOOD * 1.5f, THRESHOLD_PM25_HAZARDOUS * 1.5f);
  float weighted;
  if (d.mqWarmedUp) {
    // Weights previously carried by the climate terms are redistributed across
    // the four remaining sensors, keeping the total at 1.0.
    // Measured against this device's own settled clean-air reading, not a
    // nominal one -- see mq2Baseline in SensorData.
    float mq2Score = clampMapInverted(d.mq2, d.mq2Baseline, THRESHOLD_MQ2_DANGER);
    // Three sensors carry the score; the weights are renormalised over the
    // 0.80 they used to share with the MQ-7 so the scale is unchanged.
    weighted = (pm25Score * 0.40f + pm10Score * 0.20f + mq2Score * 0.20f) / 0.80f;
  } else {
    // Warming up: the MQ terms would read as "hazardous" and drag the score to
    // POOR, firing a spurious air-quality warning. Fall back to particulates
    // only, renormalised over their 0.60 share so the scale is unchanged.
    weighted = (pm25Score * 0.40f + pm10Score * 0.20f) / 0.60f;
  }

  if (weighted < 0) weighted = 0;
  if (weighted > 100) weighted = 100;
  return (uint8_t)(weighted + 0.5f);
}

AqCategory AirQuality::categoryForScore(uint8_t score) {
  if (score >= 90) return AqCategory::EXCELLENT;
  if (score >= 75) return AqCategory::GOOD;
  if (score >= 55) return AqCategory::MODERATE;
  if (score >= 35) return AqCategory::POOR;
  return AqCategory::HAZARDOUS;
}

// -----------------------------------------------------------------------------
// Air Quality Index
// -----------------------------------------------------------------------------
uint16_t AirQuality::calculatePM25AQI(uint16_t pm25) {
  return aqiFromBands((float)pm25, PM25_BANDS, sizeof(PM25_BANDS) / sizeof(PM25_BANDS[0]));
}

uint16_t AirQuality::calculatePM10AQI(uint16_t pm10) {
  return aqiFromBands((float)pm10, PM10_BANDS, sizeof(PM10_BANDS) / sizeof(PM10_BANDS[0]));
}

uint16_t AirQuality::calculateOverallAQI(const SensorData &d) {
  // PM2.5 is the primary pollutant; PM10 is evaluated alongside it and the
  // higher of the two wins. The MQ2 is a raw uncalibrated ADC count with no
  // defensible conversion to ug/m3 or ppm, so it is intentionally excluded
  // from the index -- it keeps its own status readout elsewhere.
  uint16_t a25 = calculatePM25AQI(d.pm2_5);
  uint16_t a10 = calculatePM10AQI(d.pm10);
  return (a25 >= a10) ? a25 : a10;
}

bool AirQuality::aqiValid(const SensorData &d) {
  return d.pmsConnected;
}

AqiCategory AirQuality::getAQICategory(uint16_t aqi) {
  if (aqi <= 50) return AqiCategory::GOOD;
  if (aqi <= 100) return AqiCategory::MODERATE;
  if (aqi <= 150) return AqiCategory::SENSITIVE;
  if (aqi <= 200) return AqiCategory::UNHEALTHY;
  if (aqi <= 300) return AqiCategory::VERY_UNHEALTHY;
  return AqiCategory::HAZARDOUS;
}

const char *AirQuality::aqiCategoryLabel(AqiCategory cat) {
  // Short forms: the full "Unhealthy for Sensitive Groups" cannot fit a 160px
  // panel at a legible size.
  switch (cat) {
    case AqiCategory::GOOD:           return "GOOD";
    case AqiCategory::MODERATE:       return "MODERATE";
    case AqiCategory::SENSITIVE:      return "SENSITIVE";
    case AqiCategory::UNHEALTHY:      return "UNHEALTHY";
    case AqiCategory::VERY_UNHEALTHY: return "V.UNHEALTHY";
    default:                          return "HAZARDOUS";
  }
}

uint16_t AirQuality::aqiCategoryColor(AqiCategory cat) {
  // Mapped onto the palette the rest of the UI already speaks, rather than
  // introducing EPA's own colours.
  switch (cat) {
    case AqiCategory::GOOD:      return Theme::GOOD;
    case AqiCategory::MODERATE:  return Theme::MODERATE;
    case AqiCategory::SENSITIVE: return Theme::POOR;
    default:                     return Theme::HAZARDOUS;
  }
}
