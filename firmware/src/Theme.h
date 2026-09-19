#pragma once
// =============================================================================
// Theme.h -- Colour palette + shared visual constants for the whole UI.
// Keeping every colour here means the entire look can be re-skinned by
// editing one file.
//
// Current skin: "Daylight Instrument". A warm paper ground with graphite
// chrome and a deep-teal accent, replacing the earlier dark indigo/violet
// look. Cards are square-cornered with a coloured keyline down their left
// edge instead of rounded floating panels, so the colour on screen carries
// meaning (which metric, which severity) rather than just decoration.
// =============================================================================
#include <Arduino.h>
#include "Config.h"

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

namespace Theme {
  // Base surfaces. Light ground, so the device stays readable in the daylight
  // it normally sits in, with the chrome inverted to near-black for contrast.
  static const uint16_t BG        = RGB565(240, 237, 229);  // warm paper
  static const uint16_t PANEL     = RGB565(255, 255, 255);  // card fill
  static const uint16_t PANEL_HI  = RGB565(222, 229, 228);  // raised / overlay fill
  static const uint16_t HEADER_BG = RGB565(22, 38, 46);     // graphite-teal ink
  static const uint16_t FOOTER_BG = RGB565(222, 218, 208);
  static const uint16_t BORDER    = RGB565(176, 172, 160);
  static const uint16_t TRACK     = RGB565(214, 210, 200);  // unfilled bar / segment

  // Text
  static const uint16_t TEXT      = RGB565(26, 33, 42);
  static const uint16_t TEXT_DIM  = RGB565(110, 118, 128);
  static const uint16_t TEXT_INV  = RGB565(238, 243, 242);  // on HEADER_BG / ACCENT
  static const uint16_t ACCENT    = RGB565(0, 128, 138);    // deep teal

  // Status colours (shared meaning across pages: quality / severity).
  // Deliberately left in the green-amber-red family: these carry meaning, so
  // they follow convention even though the rest of the skin has moved. The
  // values are darkened from the old set because they now sit on white.
  static const uint16_t EXCELLENT = RGB565(0, 150, 136);
  static const uint16_t GOOD      = RGB565(30, 140, 60);
  static const uint16_t MODERATE  = RGB565(198, 136, 0);
  static const uint16_t POOR      = RGB565(225, 100, 12);
  static const uint16_t HAZARDOUS = RGB565(205, 30, 52);

  static const uint16_t WHITE     = RGB565(255, 255, 255);
  static const uint16_t BLACK     = RGB565(0, 0, 0);

  // Layout constants shared across pages. The header gained 2px to carry a
  // proportional title face; the footer gained 2px to carry page dots.
  static const int16_t HEADER_H = 18;
  static const int16_t FOOTER_H = 14;
  static const int16_t BODY_Y   = HEADER_H;
  static const int16_t BODY_H   = SCREEN_HEIGHT - HEADER_H - FOOTER_H;

  static const int16_t PAD      = 4;   // standard gutter
  static const int16_t KEYLINE  = 3;   // width of a card's coloured left edge
}
