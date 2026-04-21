#pragma once
/**
 * ticker_engine.h — Scrolling ticker bar manager
 *
 * Owns the full ticker string pipeline:
 *   DataManager feeds  →  segment list  →  formatted string  →  LVGL label scroll
 *
 * The engine maintains a priority-ordered list of "segments" (one per data
 * category). Each segment is rebuilt whenever its feed updates. The full
 * concatenated string is handed to screen_manager's tickerLabel every loop.
 *
 * Scroll animation is driven by a single LVGL timer registered in begin().
 * External callers only need to call begin() once and tick() every second.
 */

#include <Arduino.h>
#include <lvgl.h>
#include "../config.h"

// ── Segment types (display order on ticker) ──────────────────────────────────
enum TickerSegmentType : uint8_t {
  SEG_CLOCK   = 0,   // current time — always shown
  SEG_WEATHER = 1,
  SEG_SPORTS  = 2,
  SEG_STOCKS  = 3,
  SEG_CRYPTO  = 4,
  SEG_CAL     = 5,
  SEG_COUNT
};

// ── One displayable chunk of ticker text ─────────────────────────────────────
struct TickerSegment {
  TickerSegmentType type;
  bool              enabled;
  char              text[512];    // pre-formatted, separator-padded
  uint32_t          lastUpdated;  // millis() when text was last set
};

// ── Scroll state ──────────────────────────────────────────────────────────────
struct TickerScrollState {
  lv_obj_t  *label;     // the LVGL label being scrolled
  lv_obj_t  *container; // clipping container
  int32_t    xPos;      // current x position (starts at TFT_WIDTH, moves left)
  uint8_t    speed;     // pixels per timer tick (1–5)
};

namespace TickerEngine {

  extern TickerSegment     segments[SEG_COUNT];
  extern TickerScrollState scrollState;

  /**
   * Call once after LVGL and DataManager are both initialised.
   * Creates the LVGL label + clipping container, registers the scroll timer.
   *
   * @param container  The LVGL object that acts as the ticker bar (already
   *                   sized and positioned by screen_manager).
   */
  void begin(lv_obj_t *container);

  /**
   * Call every second from the fetch task.
   * Checks DataManager for updated feeds and rebuilds any stale segments.
   * Then rebuilds the full concatenated string if anything changed.
   */
  void tick();

  /** Force-rebuild a specific segment from current DataManager data. */
  void rebuildSegment(TickerSegmentType type);

  /** Force-rebuild all segments. */
  void rebuildAll();

  /** Concatenate all enabled segments into one scrolling string. */
  void refreshLabel();

  /** Change scroll speed (1 = slowest, 5 = fastest). Persists to NVS. */
  void setSpeed(uint8_t speed);

  /** Pause / resume scrolling (e.g. while settings overlay is open). */
  void setPaused(bool paused);

  /** Attach a new LVGL container after a theme/screen rebuild. */
  void reattach(lv_obj_t *newContainer);

  /** Returns the current full ticker string (for debug / settings preview). */
  const char* getCurrentString();

} // namespace TickerEngine
