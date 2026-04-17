#pragma once
/**
 * display.h — Guition JC4827W543 (NV3041A QSPI + GT911 via TouchLib)
 *
 * The TouchLib object is kept entirely inside display.cpp — its type
 * is conditionally defined by macros in TouchLib.h and cannot be safely
 * forward-declared in a shared header. All touch state is accessed via
 * the lvglTouchReadCb which is the only consumer.
 */

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include "../config.h"

namespace Display {

extern Arduino_NV3041A *panel;
extern Arduino_NV3041A *gfx;

void setBrightness(uint8_t value);
void begin();
void initLvgl();

void lvglFlushCb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *colorMap);
void lvglTouchReadCb(lv_indev_drv_t *drv, lv_indev_data_t *data);

} // namespace Display
