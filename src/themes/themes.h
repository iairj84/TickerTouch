#pragma once
#include <lvgl.h>
#include "../../config.h"

// ── Color palettes per theme ──────────────────────────────────────────────────
struct ThemePalette {
  lv_color_t bg;           // main background
  lv_color_t bg2;          // secondary background / card
  lv_color_t text;         // primary text
  lv_color_t textMuted;    // secondary text
  lv_color_t accent;       // highlights, active tabs
  lv_color_t accentAlt;    // second accent
  lv_color_t calA;         // calendar 1 — first feed
  lv_color_t calB;         // calendar 2 — second feed
  // calA/calB are separate from accent/accentAlt on purpose: those two are too
  // close to tell apart on a 3px bar (Retro is amber vs orange, Dark indigo vs
  // violet). These are picked for hue AND brightness separation.
  lv_color_t positive;     // price up, win
  lv_color_t negative;     // price down, loss
  lv_color_t tickerBg;     // scrolling bar background
  lv_color_t tickerText;   // scrolling bar text
  lv_color_t border;       // widget borders
  const char *name;
};

namespace Themes {

// Helper: 8-bit RGB values → lv_color
#define C(r,g,b) lv_color_make(r,g,b)

inline ThemePalette get(uint8_t themeId) {
  switch (themeId) {

    // ── 0: Dark ──────────────────────────────────────────────────────────────
    case THEME_DARK:
    default:
      return {
        .bg         = C(12,  12,  18),
        .bg2        = C(24,  24,  36),
        .text       = C(230, 230, 240),
        .textMuted  = C(140, 140, 160),
        .accent     = C(99,  102, 241),   // indigo
        .accentAlt  = C(167, 139, 250),
        .calA       = C(129, 140, 248),   // indigo-400
        .calB       = C(251, 191, 36),    // amber-400
        .positive   = C(52,  211, 153),   // emerald
        .negative   = C(248, 113, 113),   // red
        .tickerBg   = C(20,  20,  30),
        .tickerText = C(210, 210, 230),
        .border     = C(40,  40,  60),
        .name       = "Dark"
      };

    // ── 1: Retro ─────────────────────────────────────────────────────────────
    case THEME_RETRO:
      return {
        .bg         = C(10,  10,  8),
        .bg2        = C(22,  22,  18),
        .text       = C(255, 186, 0),     // amber
        .textMuted  = C(180, 130, 0),
        .accent     = C(255, 186, 0),
        .accentAlt  = C(255, 100, 0),
        .calA       = C(255, 186, 0),     // amber
        .calB       = C(34,  211, 238),   // cyan
        .positive   = C(0,   255, 70),    // green phosphor
        .negative   = C(255, 60,  60),
        .tickerBg   = C(0,   0,   0),
        .tickerText = C(255, 186, 0),
        .border     = C(60,  50,  0),
        .name       = "Retro"
      };

    // ── 2: Neon ───────────────────────────────────────────────────────────────
    case THEME_NEON:
      return {
        .bg         = C(4,   0,   16),
        .bg2        = C(10,  0,   30),
        .text       = C(240, 240, 255),
        .textMuted  = C(160, 120, 220),
        .accent     = C(0,   255, 255),   // cyan
        .accentAlt  = C(255, 0,   255),   // magenta
        .calA       = C(0,   255, 255),
        .calB       = C(255, 64,  255),
        .positive   = C(0,   255, 150),
        .negative   = C(255, 50,  100),
        .tickerBg   = C(8,   0,   24),
        .tickerText = C(0,   255, 255),
        .border     = C(60,  0,   120),
        .name       = "Neon"
      };

    // ── 3: Clean (light) ──────────────────────────────────────────────────────
    case THEME_CLEAN:
      return {
        .bg         = C(248, 248, 252),
        .bg2        = C(255, 255, 255),
        .text       = C(20,  20,  30),
        .textMuted  = C(100, 100, 120),
        .accent     = C(79,  70,  229),   // indigo-600
        .accentAlt  = C(139, 92,  246),
        .calA       = C(67,  56,  202),   // indigo-700 (light bg needs depth)
        .calB       = C(13,  148, 136),   // teal-600
        .positive   = C(5,   150, 105),
        .negative   = C(220, 38,  38),
        .tickerBg   = C(240, 240, 248),
        .tickerText = C(30,  30,  50),
        .border     = C(210, 210, 230),
        .name       = "Clean"
      };

    // ── 4: Sports ─────────────────────────────────────────────────────────────
    case THEME_SPORTS:
      return {
        .bg         = C(10,  24,  10),   // dark field green
        .bg2        = C(18,  40,  18),   // medium green cards
        .text       = C(230, 255, 230),  // light green-white
        .textMuted  = C(140, 200, 140),
        .accent     = C(50,  200, 50),   // bright green
        .accentAlt  = C(255, 215, 0),    // gold
        .calA       = C(255, 215, 0),    // gold
        .calB       = C(96,  165, 250),  // sky-400
        .positive   = C(50,  200, 50),
        .negative   = C(255, 80,  80),
        .tickerBg   = C(8,   20,  8),
        .tickerText = C(255, 215, 0),    // gold ticker
        .border     = C(30,  70,  30),
        .name       = "Sports"
      };
  }
}

// Apply palette to a given LVGL display
void apply(uint8_t themeId, lv_disp_t *disp = nullptr);

} // namespace Themes
