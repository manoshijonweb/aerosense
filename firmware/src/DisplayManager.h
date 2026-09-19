#pragma once
// =============================================================================
// DisplayManager -- owns the physical TFT and provides every drawing
// primitive the UI pages use: header/footer chrome, cards, bars, gauges,
// simple vector icons, and change-aware "field" helpers that only touch the
// pixels that actually changed (so pages can redraw every tick without
// flicker or wasted SPI traffic).
//
// Typography: the UI mixes two faces. Labels, page titles and dense status
// strings stay on the built-in 5x7 GFX font -- at 160px wide nothing
// proportional fits the label columns, and chrome should not compete with the
// readings -- while headline numerals use a real proportional sans
// (Adafruit_GFX's FreeSans / FreeSansBold). Custom GFX fonts draw
// from the baseline and ignore the background colour, so all text goes through
// the TextStyle helpers below, which erase behind themselves and take y as the
// top of the line box regardless of which face is selected.
// =============================================================================
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include "Config.h"
#include "Theme.h"

enum class IconType : uint8_t {
  THERMOMETER,
  DROPLET,
  LEAF,
  FLAME,
  WARNING,
  CHECK,
  CROSS,
  SIGNAL,
  CHART,
  CLOCK
};

// Text roles rather than raw font pointers: pages ask for "a value" or "a
// label" and the font/size pairing is decided in one place.
enum class TextStyle : uint8_t {
  SMALL = 0,   // built-in 5x7  -- labels, units, status words, dense rows
  SMALL_2X,    // built-in 5x7 at 2x -- bitmap face, blocks up when scaled;
               //   avoid for anything the eye rests on
  VALUE_LIGHT, // FreeSans 9pt      -- secondary readouts. Same body size as
               //   VALUE but regular weight, so it carries less visual weight
               //   in a small card without dropping to a pixel face (9pt is
               //   the smallest smooth size Adafruit_GFX ships).
  VALUE,       // FreeSansBold 9pt  -- readouts, page titles, menu headings
  HERO         // FreeSansBold 12pt -- the one headline figure on a page
};
static const uint8_t TEXT_STYLE_COUNT = 5;

class DisplayManager {
public:
  void begin();
  Adafruit_ST7735 &tft() { return _tft; }

  // ---- Boot screen ----
  void showSplash(const char *title, const char *subtitle);

  // ---- Page chrome (call only on full page redraw, not every tick) ----
  void drawHeader(const char *title);
  void drawFooter(const char *hint, uint8_t pageIndex, uint8_t pageCount);
  void clearBody(uint16_t bg = Theme::BG);

  // ---- Structural primitives ----
  // A card is square-cornered with a coloured keyline down its left edge; the
  // keyline colour is how a card says which metric or severity it carries.
  void drawCard(int16_t x, int16_t y, int16_t w, int16_t h,
                uint16_t accent = Theme::ACCENT, uint16_t fill = Theme::PANEL);

  // ---- Text ----
  // y is always the top of the line box. Widths/ascents are measured from the
  // font itself in begin(), so nothing here hardcodes glyph metrics.
  void setStyle(TextStyle style);
  int16_t lineHeight(TextStyle style) const { return _lineH[(uint8_t)style]; }
  int16_t textWidth(TextStyle style, const char *text);
  void drawText(int16_t x, int16_t y, TextStyle style, uint16_t fg, const char *text);
  void drawTextRight(int16_t xRight, int16_t y, TextStyle style, uint16_t fg, const char *text);
  void drawTextCentered(int16_t y, TextStyle style, uint16_t fg, const char *text);

  // ---- Change-aware text field: redraws only when the string differs from cache ----
  bool drawTextField(int16_t x, int16_t y, TextStyle style, uint16_t fg, uint16_t bg,
                      char *cache, uint8_t cacheLen, const char *newText);

  // ---- Change-aware horizontal progress bar (0..100) ----
  bool drawBar(int16_t x, int16_t y, int16_t w, int16_t h,
               uint8_t &lastPercent, uint8_t newPercent,
               uint16_t fillColor, uint16_t trackColor = Theme::TRACK);

  // ---- Change-aware segmented "battery style" intensity bar ----
  bool drawSegmentedBar(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t segments,
                         uint8_t &lastLevel, uint8_t newLevel, uint16_t color);

  // ---- Zone gauge: coloured good/moderate/danger bands + a marker needle.
  // The needle sits in a 6px gutter above the strip, so leave room there; `bg`
  // is what that gutter gets cleared to between moves.
  bool drawZoneGauge(int16_t x, int16_t y, int16_t w, int16_t h,
                      float value, float minV, float maxV, float warnV, float dangerV,
                      int16_t &lastMarkerX, uint16_t bg = Theme::PANEL);

  // ---- Small vector icons, drawn inside a size x size box anchored at (x,y) ----
  void drawIcon(int16_t x, int16_t y, uint8_t size, IconType icon, uint16_t color);

  // ---- Menu row highlight helper (quick menu) ----
  void drawMenuItem(int16_t x, int16_t y, int16_t w, int16_t h, const char *label, bool selected);

  // ---- Warning overlay banner ----
  void drawWarningBanner(const char *message, uint16_t color);

  // ---- Two-line informational overlay ----
  void drawInfoBanner(const char *title, const char *line1, const char *line2, uint16_t color);

  // ---- Header status glyphs: 4-bar signal meter (11x8) and a small state dot ----
  void drawWifiIndicator(int16_t x, int16_t y, uint8_t bars, uint16_t color, uint16_t bg);
  void drawStatusDot(int16_t x, int16_t y, uint8_t r, uint16_t color, uint16_t bg);

  static uint8_t percentOf(float value, float minV, float maxV);

private:
  Adafruit_ST7735 _tft{PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST};

  // Per-style metrics, measured off the fonts themselves at boot.
  int16_t _ascent[TEXT_STYLE_COUNT] = {0};
  int16_t _lineH[TEXT_STYLE_COUNT]  = {8};
  void measureStyles();
};
