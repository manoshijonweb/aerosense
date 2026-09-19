#include "DisplayManager.h"

// The font tables are pulled in here and nowhere else: they are file-scope
// const arrays, so including them from the header would compile one copy into
// every translation unit that draws anything.
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

namespace {
  struct StyleDef {
    const GFXfont *font;  // nullptr = built-in 5x7 face
    uint8_t size;
    int8_t  extraH;       // leading added to the measured glyph height
  };

  // Indexed by TextStyle.
  const StyleDef STYLES[TEXT_STYLE_COUNT] = {
    { nullptr,              1, 0 },   // SMALL
    { nullptr,              2, 0 },   // SMALL_2X
    { &FreeSans9pt7b,       1, 2 },   // VALUE_LIGHT
    { &FreeSansBold9pt7b,   1, 2 },   // VALUE
    { &FreeSansBold12pt7b,  1, 2 },   // HERO
  };

  inline bool isProportional(TextStyle s) { return STYLES[(uint8_t)s].font != nullptr; }
}

void DisplayManager::begin() {
  _tft.initR(INITR_BLACKTAB);
  // Raised after init: the reset/command sequence runs at the library default,
  // then every pixel transfer afterwards goes at TFT_SPI_HZ.
  _tft.setSPISpeed(TFT_SPI_HZ);
  _tft.setRotation(1); // landscape: 160x128
  _tft.setTextWrap(false);
  measureStyles();
  _tft.fillScreen(Theme::BG);
}

// Ascent and line height are read back out of the font rather than written
// down here, so swapping a face in STYLES needs no other edit.
void DisplayManager::measureStyles() {
  for (uint8_t i = 0; i < TEXT_STYLE_COUNT; i++) {
    setStyle((TextStyle)i);
    int16_t x1, y1; uint16_t w, h;
    _tft.getTextBounds("0AW", 0, 0, &x1, &y1, &w, &h);
    _ascent[i] = -y1;                               // 0 for the built-in face
    _lineH[i]  = (int16_t)h + STYLES[i].extraH;
  }
}

void DisplayManager::setStyle(TextStyle style) {
  const StyleDef &d = STYLES[(uint8_t)style];
  _tft.setFont(d.font);       // nullptr restores the built-in face
  _tft.setTextSize(d.size);
}

int16_t DisplayManager::textWidth(TextStyle style, const char *text) {
  if (!text || !text[0]) return 0;
  const StyleDef &d = STYLES[(uint8_t)style];
  if (!d.font) return (int16_t)strlen(text) * 6 * d.size;  // exact advance
  setStyle(style);
  int16_t x1, y1; uint16_t w, h;
  _tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  return (int16_t)w + 2;      // + a little for side bearings
}

void DisplayManager::drawText(int16_t x, int16_t y, TextStyle style, uint16_t fg, const char *text) {
  setStyle(style);
  _tft.setTextColor(fg);                   // transparent: caller owns the background
  _tft.setCursor(x, y + _ascent[(uint8_t)style]);
  _tft.print(text);
}

void DisplayManager::drawTextRight(int16_t xRight, int16_t y, TextStyle style, uint16_t fg, const char *text) {
  drawText(xRight - textWidth(style, text), y, style, fg, text);
}

void DisplayManager::drawTextCentered(int16_t y, TextStyle style, uint16_t fg, const char *text) {
  drawText((SCREEN_WIDTH - textWidth(style, text)) / 2, y, style, fg, text);
}

void DisplayManager::showSplash(const char *title, const char *subtitle) {
  _tft.fillScreen(Theme::BG);

  // Top third is a solid ink block with the wordmark knocked out of it -- a
  // flat masthead rather than the old concentric rings.
  _tft.fillRect(0, 0, SCREEN_WIDTH, 52, Theme::HEADER_BG);
  _tft.fillRect(0, 52, SCREEN_WIDTH, 3, Theme::ACCENT);
  drawTextCentered(15, TextStyle::HERO, Theme::TEXT_INV, title);

  drawTextCentered(66, TextStyle::SMALL, Theme::TEXT_DIM, subtitle);

  // Five ticks standing in for a progress rule.
  for (uint8_t i = 0; i < 5; i++) {
    _tft.fillRect(48 + i * 14, 86, 8, 4, (i < 2) ? Theme::ACCENT : Theme::BORDER);
  }
  drawTextCentered(104, TextStyle::SMALL, Theme::TEXT_DIM, "warming up sensors");
}

void DisplayManager::drawHeader(const char *title) {
  // Inverted chrome: a solid ink band with the title knocked out of it, closed
  // by a full-width accent rule.
  //
  // The title is set in the built-in face at 1x, not the proportional one. The
  // band is 18px with 2px of that spent on the rule, and a 9pt line box is ~15px
  // -- the title effectively filled the whole band and read as oversized for
  // what is chrome rather than content. At 1x the built-in font is drawn at its
  // native resolution, so it stays crisp (unlike SMALL_2X, which scales a bitmap
  // and visibly blocks up). Titles are short and uppercase, which is exactly
  // what this face handles well.
  const int16_t bandH = Theme::HEADER_H - 2;   // usable height above the rule
  _tft.fillRect(0, 0, SCREEN_WIDTH, Theme::HEADER_H, Theme::HEADER_BG);
  _tft.fillRect(0, Theme::HEADER_H - 2, SCREEN_WIDTH, 2, Theme::ACCENT);
  drawText(8, (bandH - lineHeight(TextStyle::SMALL)) / 2, TextStyle::SMALL,
           Theme::TEXT_INV, title);
}

void DisplayManager::drawFooter(const char *hint, uint8_t pageIndex, uint8_t pageCount) {
  const int16_t y = SCREEN_HEIGHT - Theme::FOOTER_H;
  _tft.fillRect(0, y, SCREEN_WIDTH, Theme::FOOTER_H, Theme::FOOTER_BG);
  _tft.drawFastHLine(0, y, SCREEN_WIDTH, Theme::BORDER);
  drawText(6, y + 3, TextStyle::SMALL, Theme::TEXT_DIM, hint);

  // Page dots at the right: position in the carousel, which the old footer
  // never showed (and its hint string was wider than the screen anyway).
  if (pageCount == 0) return;
  const int16_t pitch = 7, cy = y + 7;
  for (uint8_t i = 0; i < pageCount; i++) {
    int16_t cx = SCREEN_WIDTH - 8 - (int16_t)(pageCount - 1 - i) * pitch;
    if (i == pageIndex) _tft.fillRect(cx - 2, cy - 2, 5, 5, Theme::ACCENT);
    else                _tft.fillRect(cx - 1, cy - 1, 3, 3, Theme::BORDER);
  }
}

void DisplayManager::clearBody(uint16_t bg) {
  _tft.fillRect(0, Theme::BODY_Y, SCREEN_WIDTH, Theme::BODY_H, bg);
}

void DisplayManager::drawCard(int16_t x, int16_t y, int16_t w, int16_t h,
                               uint16_t accent, uint16_t fill) {
  _tft.fillRect(x, y, w, h, fill);
  _tft.drawRect(x, y, w, h, Theme::BORDER);
  _tft.fillRect(x, y, Theme::KEYLINE, h, accent);
}

bool DisplayManager::drawTextField(int16_t x, int16_t y, TextStyle style, uint16_t fg, uint16_t bg,
                                    char *cache, uint8_t cacheLen, const char *newText) {
  if (strncmp(cache, newText, cacheLen) == 0) return false;

  // Proportional glyphs are drawn transparently by Adafruit_GFX, so the field
  // has to clear itself: erase whichever of the two strings is wider.
  int16_t oldW = textWidth(style, cache);
  int16_t newW = textWidth(style, newText);
  int16_t wipeW = (oldW > newW) ? oldW : newW;
  if (wipeW > 0) {
    const bool prop = isProportional(style);
    _tft.fillRect(prop ? x - 1 : x, y, wipeW + (prop ? 2 : 0), lineHeight(style), bg);
  }

  drawText(x, y, style, fg, newText);

  strncpy(cache, newText, cacheLen - 1);
  cache[cacheLen - 1] = '\0';
  return true;
}

bool DisplayManager::drawBar(int16_t x, int16_t y, int16_t w, int16_t h,
                              uint8_t &lastPercent, uint8_t newPercent,
                              uint16_t fillColor, uint16_t trackColor) {
  if (newPercent > 100) newPercent = 100;
  if (lastPercent == newPercent) return false;

  // Capsule track with the fill inset inside it, rather than a hard-edged
  // rectangle sitting on the background.
  uint8_t r = (h >= 6) ? 2 : 1;
  _tft.fillRoundRect(x, y, w, h, r, trackColor);
  int16_t fillW = (int32_t)(w - 2) * newPercent / 100;
  if (fillW > 0) _tft.fillRect(x + 1, y + 1, fillW, h - 2, fillColor);
  _tft.drawRoundRect(x, y, w, h, r, Theme::BORDER);

  lastPercent = newPercent;
  return true;
}

bool DisplayManager::drawSegmentedBar(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t segments,
                                      uint8_t &lastLevel, uint8_t newLevel, uint16_t color) {
  if (newLevel > segments) newLevel = segments;
  if (lastLevel == newLevel) return false;

  int16_t gap = 2;
  int16_t segW = (w - gap * (segments - 1)) / segments;

  // Borderless ticks: at this size a per-segment outline just muddies them.
  for (uint8_t i = 0; i < segments; i++) {
    int16_t segX = x + i * (segW + gap);
    _tft.fillRect(segX, y, segW, h, (i < newLevel) ? color : Theme::TRACK);
  }

  lastLevel = newLevel;
  return true;
}

uint8_t DisplayManager::percentOf(float value, float minV, float maxV) {
  if (maxV <= minV) return 0;
  float pct = (value - minV) * 100.0f / (maxV - minV);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return (uint8_t)pct;
}

bool DisplayManager::drawZoneGauge(int16_t x, int16_t y, int16_t w, int16_t h,
                                    float value, float minV, float maxV, float warnV, float dangerV,
                                    int16_t &lastMarkerX, uint16_t bg) {
  uint8_t goodFrac = percentOf(warnV, minV, maxV);
  uint8_t warnFrac = percentOf(dangerV, minV, maxV);
  uint8_t valuePct = percentOf(value, minV, maxV);
  int16_t markerX = x + (int32_t)w * valuePct / 100;

  if (markerX == lastMarkerX) return false;

  // Clear the strip *and* the marker gutter above it. The old needle
  // overhung the bands, so repainting the bands alone left stubs behind.
  _tft.fillRect(x - 4, y - 6, w + 8, h + 6, bg);

  int16_t goodW = (int32_t)w * goodFrac / 100;
  int16_t warnW = (int32_t)w * warnFrac / 100 - goodW;
  int16_t dangerW = w - goodW - warnW;

  _tft.fillRect(x, y, goodW, h, Theme::GOOD);
  _tft.fillRect(x + goodW, y, warnW, h, Theme::MODERATE);
  _tft.fillRect(x + goodW + warnW, y, dangerW, h, Theme::HAZARDOUS);
  _tft.drawRect(x, y, w, h, Theme::BORDER);

  // Marker: a pointer hanging over the strip instead of a bar crossing it.
  int16_t mx = markerX;
  if (mx < x + 4) mx = x + 4;
  if (mx > x + w - 5) mx = x + w - 5;
  _tft.fillTriangle(mx - 4, y - 6, mx + 4, y - 6, mx, y - 1, Theme::TEXT);

  lastMarkerX = markerX;
  return true;
}

void DisplayManager::drawIcon(int16_t x, int16_t y, uint8_t s, IconType icon, uint16_t color) {
  switch (icon) {
    case IconType::THERMOMETER: {
      int16_t stemW = s * 0.30f;
      int16_t stemX = x + (s - stemW) / 2;
      _tft.fillRoundRect(stemX, y, stemW, (int16_t)(s * 0.65f), stemW / 2, color);
      _tft.fillCircle(x + s / 2, y + (int16_t)(s * 0.75f), (int16_t)(s * 0.30f), color);
      break;
    }
    case IconType::DROPLET: {
      _tft.fillTriangle(x + s / 2, y, x + (int16_t)(s * 0.15f), y + (int16_t)(s * 0.6f),
                         x + (int16_t)(s * 0.85f), y + (int16_t)(s * 0.6f), color);
      _tft.fillCircle(x + s / 2, y + (int16_t)(s * 0.62f), (int16_t)(s * 0.35f), color);
      break;
    }
    case IconType::LEAF: {
      _tft.fillCircle(x + (int16_t)(s * 0.5f), y + (int16_t)(s * 0.5f), (int16_t)(s * 0.42f), color);
      _tft.drawFastVLine(x + s / 2, y + (int16_t)(s * 0.5f), (int16_t)(s * 0.4f), color);
      break;
    }
    case IconType::FLAME: {
      _tft.fillTriangle(x + s / 2, y, x + (int16_t)(s * 0.15f), y + s,
                         x + (int16_t)(s * 0.85f), y + s, color);
      _tft.fillCircle(x + s / 2, y + (int16_t)(s * 0.7f), (int16_t)(s * 0.18f), color);
      break;
    }
    case IconType::WARNING: {
      _tft.drawTriangle(x + s / 2, y, x, y + s, x + s, y + s, color);
      _tft.drawTriangle(x + s / 2, y + 1, x + 1, y + s - 1, x + s - 1, y + s - 1, color);
      _tft.drawFastVLine(x + s / 2, y + (int16_t)(s * 0.35f), (int16_t)(s * 0.3f), color);
      _tft.drawPixel(x + s / 2, y + (int16_t)(s * 0.78f), color);
      break;
    }
    case IconType::CHECK: {
      _tft.drawLine(x, y + s / 2, x + s / 3, y + s, color);
      _tft.drawLine(x + s / 3, y + s, x + s, y, color);
      break;
    }
    case IconType::CROSS: {
      _tft.drawLine(x, y, x + s, y + s, color);
      _tft.drawLine(x, y + s, x + s, y, color);
      break;
    }
    case IconType::SIGNAL: {
      int16_t barW = s / 5;
      for (uint8_t i = 0; i < 4; i++) {
        int16_t barH = (int16_t)(s * (0.25f + 0.25f * i));
        _tft.fillRect(x + i * (barW + 1), y + s - barH, barW, barH, color);
      }
      break;
    }
    case IconType::CHART: {
      _tft.drawLine(x, y + s * 0.7f, x + s * 0.3f, y + s * 0.3f, color);
      _tft.drawLine(x + s * 0.3f, y + s * 0.3f, x + s * 0.6f, y + s * 0.6f, color);
      _tft.drawLine(x + s * 0.6f, y + s * 0.6f, x + s, y, color);
      break;
    }
    case IconType::CLOCK: {
      int16_t r = s / 2;
      _tft.drawCircle(x + r, y + r, r, color);
      _tft.drawLine(x + r, y + r, x + r, y + r - r * 0.6f, color);
      _tft.drawLine(x + r, y + r, x + r + r * 0.4f, y + r, color);
      break;
    }
  }
}

void DisplayManager::drawMenuItem(int16_t x, int16_t y, int16_t w, int16_t h, const char *label, bool selected) {
  uint16_t fill = selected ? Theme::ACCENT : Theme::PANEL;
  uint16_t fg   = selected ? Theme::TEXT_INV : Theme::TEXT;
  _tft.fillRect(x, y, w, h, fill);
  _tft.drawRect(x, y, w, h, selected ? Theme::ACCENT : Theme::BORDER);
  // Same left keyline the cards use, so selection reads as "this row is live".
  _tft.fillRect(x, y, Theme::KEYLINE, h, selected ? Theme::TEXT_INV : Theme::BORDER);
  drawText(x + 9, y + (h - 8) / 2, TextStyle::SMALL, fg, label);
}

// Header signal meter: four rising bars, lit up to `bars`, unlit ones left as
// track grey so the widget keeps its shape at every signal level.
void DisplayManager::drawWifiIndicator(int16_t x, int16_t y, uint8_t bars, uint16_t color, uint16_t bg) {
  _tft.fillRect(x, y, 11, 8, bg);
  for (uint8_t i = 0; i < 4; i++) {
    int16_t h = 2 + i * 2;                 // 2, 4, 6, 8 px
    int16_t bx = x + i * 3;
    _tft.fillRect(bx, y + 8 - h, 2, h, (i < bars) ? color : Theme::TRACK);
  }
}

void DisplayManager::drawStatusDot(int16_t x, int16_t y, uint8_t r, uint16_t color, uint16_t bg) {
  _tft.fillRect(x - r, y - r, r * 2 + 1, r * 2 + 1, bg);
  _tft.fillCircle(x, y, r, color);
}

void DisplayManager::drawInfoBanner(const char *title, const char *line1, const char *line2, uint16_t color) {
  const int16_t h = 52, y = (SCREEN_HEIGHT - h) / 2;
  _tft.fillRect(0, y, SCREEN_WIDTH, h, Theme::PANEL);
  _tft.fillRect(0, y, SCREEN_WIDTH, 4, color);
  _tft.fillRect(0, y + h - 4, SCREEN_WIDTH, 4, color);

  drawText(8, y + 9,  TextStyle::SMALL, color,          title);
  drawText(8, y + 23, TextStyle::SMALL, Theme::TEXT,    line1);
  drawText(8, y + 35, TextStyle::SMALL, Theme::TEXT_DIM, line2);
}

void DisplayManager::drawWarningBanner(const char *message, uint16_t color) {
  // Edge-to-edge alert slab: the severity colour rules the top and bottom of
  // the band while the message stays on white, so it is legible whether the
  // severity is amber or red.
  const int16_t h = 46, y = (SCREEN_HEIGHT - h) / 2;
  _tft.fillRect(0, y, SCREEN_WIDTH, h, Theme::PANEL);
  _tft.fillRect(0, y, SCREEN_WIDTH, 4, color);
  _tft.fillRect(0, y + h - 4, SCREEN_WIDTH, 4, color);

  drawIcon(9, y + 14, 18, IconType::WARNING, color);
  drawText(34, y + 11, TextStyle::SMALL, color, "ALERT");
  drawText(34, y + 25, TextStyle::SMALL, Theme::TEXT, message);
}
