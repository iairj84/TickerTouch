#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include "../config.h"

#define SCREENSAVER_TIMEOUT_S 600

namespace Screensaver {
  using BrightnessFn = void(*)(uint8_t);
  void begin(lv_obj_t *mainScreen, BrightnessFn cb);
  void tick();
  void onTouch();
  bool isActive();
  void setIdleTimeout(uint32_t seconds);
}
