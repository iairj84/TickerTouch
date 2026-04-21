/**
 * lv_conf.h — TickerTouch LVGL 8.3.x config
 * Core 3.x: LVGL heap and draw buffers live in 8MB PSRAM via ps_malloc()
 */
#if 1
#ifndef LV_CONF_H
#define LV_CONF_H
#include <stdint.h>

#define LV_COLOR_DEPTH     16
#define LV_COLOR_16_SWAP   0
#define LV_COLOR_SCREEN_TRANSP 0

// Use PSRAM via esp_heap_caps.h — this header is C-compatible unlike Arduino's ps_malloc
// LVGL's lv_mem.c is compiled as C, so it needs a proper C declaration
#define LV_MEM_CUSTOM      1
#define LV_MEM_CUSTOM_INCLUDE "esp_heap_caps.h"
#define LV_MEM_CUSTOM_ALLOC(size)        heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define LV_MEM_CUSTOM_FREE(p)            heap_caps_free(p)
#define LV_MEM_CUSTOM_REALLOC(p, size)   heap_caps_realloc(p, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

#define LV_TICK_CUSTOM     0

#define LV_USE_LOG           1
#define LV_LOG_LEVEL         LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF        1
#define LV_USE_PERF_MONITOR  0
#define LV_USE_MEM_MONITOR   0

// Fonts — all go to flash (PROGMEM), not DRAM
// Core 3.x with PSRAM: we can enable more sizes safely
#define LV_FONT_MONTSERRAT_12  1
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_16  1
#define LV_FONT_MONTSERRAT_18  1
#define LV_FONT_MONTSERRAT_20  1
#define LV_FONT_MONTSERRAT_22  1
#define LV_FONT_MONTSERRAT_24  1
#define LV_FONT_MONTSERRAT_28  1
#define LV_FONT_MONTSERRAT_32  1
#define LV_FONT_MONTSERRAT_36  1
#define LV_FONT_MONTSERRAT_48  1

#define LV_FONT_DEFAULT    &lv_font_montserrat_14

#define LV_USE_ARC         1
#define LV_USE_BAR         1
#define LV_USE_BTN         1
#define LV_USE_BTNMATRIX   1
#define LV_USE_CANVAS      0
#define LV_USE_CHECKBOX    1
#define LV_USE_DROPDOWN    1
#define LV_USE_IMG         1
#define LV_USE_LABEL       1
#define LV_USE_LINE        1
#define LV_USE_ROLLER      1
#define LV_USE_SLIDER      1
#define LV_USE_SWITCH      1
#define LV_USE_TEXTAREA    1
#define LV_USE_TABLE       1
#define LV_USE_TABVIEW     1
#define LV_USE_TILEVIEW    0
#define LV_USE_WIN         0
#define LV_USE_SPAN        1
#define LV_USE_SPINBOX     0
#define LV_USE_SPINNER     1
#define LV_USE_ANIMIMG     0
#define LV_USE_CALENDAR    0
#define LV_USE_CHART       0
#define LV_USE_COLORWHEEL  0
#define LV_USE_IMGBTN      0
#define LV_USE_KEYBOARD    0
#define LV_USE_LED         0
#define LV_USE_LIST        0
#define LV_USE_MENU        0
#define LV_USE_METER       0
#define LV_USE_MSGBOX      0
#define LV_USE_QRCODE      0

#define LV_USE_THEME_DEFAULT  1
#define LV_USE_THEME_BASIC    1
#define LV_USE_THEME_MONO     0

#define LV_USE_FLEX       1
#define LV_USE_GRID       1
#define LV_USE_ANIMATION  1

#define LV_DRAW_COMPLEX   1
#define LV_SHADOW_CACHE_SIZE 4
#define LV_CIRCLE_CACHE_SIZE 8

#define LV_USE_GPU_STM32_DMA2D  0
#define LV_USE_GPU_SWM341_DMA   0
#define LV_USE_GPU_NXP_PXP      0
#define LV_USE_GPU_NXP_VG_LITE  0
#define LV_USE_GPU_SDL          0

#endif
#endif
