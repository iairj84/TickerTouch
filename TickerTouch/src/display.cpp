#include "touch.h"
#include "display.h"
#include "screensaver.h"

namespace Display {

static Arduino_DataBus *bus = nullptr;
Arduino_NV3041A        *panel = nullptr;
Arduino_NV3041A        *gfx   = nullptr;

static constexpr uint8_t  BL_CH   = 0;
static constexpr uint8_t  BL_BITS = 12;
static constexpr uint32_t BL_FREQ = 5000;

// Core 3.x: ledcAttach(pin, freq, resolution) replaces ledcSetup+ledcAttachPin
// Core 2.x: still uses ledcSetup(channel, freq, res) + ledcAttachPin(pin, channel)
// We use ledcAttach on Core 3.x (which auto-assigns channel), but we need
// ledcWrite to still work. In Core 3.x ledcWrite takes (pin, duty).
#if ESP_ARDUINO_VERSION_MAJOR >= 3
void setBrightness(uint8_t v) { ledcWrite(LCD_BL, (4095UL*v)/255); }
#else
void setBrightness(uint8_t v) { ledcWrite(BL_CH, (4095UL*v)/255); }
#endif

void begin() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(LCD_BL, BL_FREQ, BL_BITS);
#else
  ledcSetup(BL_CH, BL_FREQ, BL_BITS);
  ledcAttachPin(LCD_BL, BL_CH);
#endif
  setBrightness(0);

  bus   = new Arduino_ESP32QSPI(LCD_CS,LCD_SCK,LCD_D0,LCD_D1,LCD_D2,LCD_D3);
  panel = new Arduino_NV3041A(bus, GFX_NOT_DEFINED, 0, true);
  gfx   = panel;

  if (!panel->begin()) {
    while(true){setBrightness(200);delay(200);setBrightness(0);delay(200);}
  }
  panel->fillScreen(0x0000);
  setBrightness(200);

  // bb_captouch handles its own Wire/I2C init internally
  touch_init();
}

void lvglFlushCb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *buf) {
  uint32_t w = area->x2-area->x1+1, h = area->y2-area->y1+1;
  panel->startWrite();
  panel->setAddrWindow(area->x1, area->y1, w, h);
  panel->writePixels((uint16_t*)&buf->full, w*h);
  panel->endWrite();
  lv_disp_flush_ready(drv);
}

void lvglTouchReadCb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  if (touch_read()) {
    bool sleeping = Screensaver::isActive();
    Screensaver::onTouch();
    if (sleeping) { data->state = LV_INDEV_STATE_RELEASED; return; }
    data->point.x = touch_x();
    data->point.y = touch_y();
    data->state   = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void initLvgl() {
  // Allocate draw buffers in PSRAM — 480*64*2 bytes each = 61KB each, 122KB total
  // Using ps_malloc() puts them in the 8MB PSRAM instead of the 512KB internal DRAM
  static lv_disp_draw_buf_t db;
  const size_t bufPx = TFT_WIDTH * LVGL_BUF_LINES;
  lv_color_t *b1 = (lv_color_t*)ps_malloc(bufPx * sizeof(lv_color_t));
  lv_color_t *b2 = (lv_color_t*)ps_malloc(bufPx * sizeof(lv_color_t));
  if (!b1 || !b2) {
    // Fallback: smaller single buffer in heap if PSRAM somehow unavailable
    Serial.println(F("[LVGL] PSRAM alloc failed, using small fallback buffer"));
    static lv_color_t fb[TFT_WIDTH * 8];
    b1 = fb; b2 = nullptr;
    lv_disp_draw_buf_init(&db, b1, nullptr, TFT_WIDTH * 8);
  } else {
    lv_disp_draw_buf_init(&db, b1, b2, bufPx);
  }

  static lv_disp_drv_t dd;
  lv_disp_drv_init(&dd);
  dd.hor_res=TFT_WIDTH; dd.ver_res=TFT_HEIGHT;
  dd.flush_cb=lvglFlushCb; dd.draw_buf=&db;
  lv_disp_drv_register(&dd);

  static lv_indev_drv_t id;
  lv_indev_drv_init(&id);
  id.type=LV_INDEV_TYPE_POINTER; id.read_cb=lvglTouchReadCb;
  lv_indev_drv_register(&id);
}

} // namespace Display
