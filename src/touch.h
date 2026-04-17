#pragma once
/**
 * touch.h — Capacitive touch for JC4827W543 using bb_captouch
 * Library: "bb_captouch" by Larry Bank (Library Manager)
 *
 * Pins confirmed from Halo-F1 source (same board):
 *   SDA=8, SCL=4, INT=3, RST=-1
 *
 * Halo-F1 runs PORTRAIT (272x480). TickerTouch runs LANDSCAPE (480x272).
 * The GT911 raw coords are portrait-native, so mapping differs:
 *
 * Portrait  (Halo):  point.x = map(raw.y, ..., 1, 480)
 *                    point.y = 272 - map(raw.x, ..., 1, 272)
 *
 * Landscape (ours):  point.x = map(raw.x, ..., 1, 480)  -- no swap needed
 *                    point.y = map(raw.y, ..., 1, 272)
 *
 * If touch is still off after flashing, try swapping the defines below.
 */

#include <bb_captouch.h>

#define GT_SDA   8
#define GT_SCL   4
#define GT_INT   3
#define GT_RST  -1

static BBCapTouch _bbct;
static TOUCHINFO  _ti;
static int16_t    _tx = 0, _ty = 0;

// Auto-calibration bounds
static uint16_t _tminX = 0, _tmaxX = 479, _tminY = 0, _tmaxY = 271;

static inline void touch_init() {
  // RST = -1 means not connected — bb_captouch handles this internally
  // Do NOT call pinMode/digitalWrite on pin -1 (becomes 255, invalid GPIO)
  _bbct.init(GT_SDA, GT_SCL, GT_RST, GT_INT);
}

static inline bool touch_read() {
  if (!_bbct.getSamples(&_ti)) return false;

  uint16_t rx = _ti.x[0];
  uint16_t ry = _ti.y[0];

  // Update auto-calibration
  if (rx < _tminX) _tminX = rx;
  if (rx > _tmaxX) _tmaxX = rx;
  if (ry < _tminY) _tminY = ry;
  if (ry > _tmaxY) _tmaxY = ry;

  // Landscape mapping: raw X → screen X, raw Y → screen Y
  // (no axis swap needed for landscape, unlike portrait in Halo-F1)
  _tx = map(rx, _tminX, _tmaxX, 0, 479);
  _ty = map(ry, _tminY, _tmaxY, 0, 271);

  return true;
}

static inline int16_t touch_x() { return _tx; }
static inline int16_t touch_y() { return _ty; }
