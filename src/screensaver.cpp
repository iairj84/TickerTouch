#include "screensaver.h"
#include "display.h"
#include "themes/themes.h"
#include "storage.h"
#include "ticker_engine.h"
#include "slideshow.h"
#include <math.h>

namespace Screensaver {

static lv_obj_t    *_screen        = nullptr;
static BrightnessFn _setBrightness = nullptr;
static bool         _active        = false;
static uint32_t     _idleTimeout   = SCREENSAVER_TIMEOUT_S * 1000UL;
static uint32_t     _lastTouchMs   = 0;
static uint32_t     _lastMoveMs    = 0;
static uint32_t     _lastClockMs   = 0;

static lv_obj_t    *_overlay       = nullptr;
static lv_obj_t    *_floatClock    = nullptr;
static lv_obj_t    *_floatDate     = nullptr;

// Ticker scroll state
static lv_obj_t    *_tickerCont    = nullptr;
static lv_obj_t    *_tickerLbl     = nullptr;
static lv_timer_t  *_scrollTimer   = nullptr;
static int32_t      _tickerX       = 0;

// Shared styles — initialized once, colors updated on theme change
static bool         _stylesInited  = false;
static lv_style_t   _stClock, _stDate, _stTickerTxt;

static void initStyles() {
  ThemePalette pal = Themes::get(Storage::cfg.theme);

  if (!_stylesInited) {
    _stylesInited = true;
    lv_style_init(&_stClock);
    lv_style_init(&_stDate);
    lv_style_init(&_stTickerTxt);
  }
  // Clock/date stay neutral purple-gray regardless of theme
  lv_style_set_text_color(&_stClock, lv_color_make(160, 160, 200));
  lv_style_set_text_font(&_stClock,  &lv_font_montserrat_48);
  lv_style_set_text_color(&_stDate,  lv_color_make(110, 110, 150));
  lv_style_set_text_font(&_stDate,   &lv_font_montserrat_24);
  // Ticker text uses theme's ticker text color
  lv_style_set_text_color(&_stTickerTxt, pal.tickerText);
  lv_style_set_text_font(&_stTickerTxt,  &lv_font_montserrat_14);
}

static int16_t _floatX = 100;
static int16_t _floatY = 60;
static int8_t  _driftX = 2;
static int8_t  _driftY = 1;

static void scrollCb(lv_timer_t *) {
  if (!_tickerLbl) return;
  _tickerX -= 2;
  lv_coord_t lblW = lv_obj_get_width(_tickerLbl);
  if (_tickerX < -(int32_t)lblW) _tickerX = TFT_WIDTH + 10;
  lv_obj_set_x(_tickerLbl, _tickerX);
}

// ── Photo slideshow ─────────────────────────────────────────────────────────
// One decoded frame lives in a single PSRAM buffer and is swapped while the
// image is off-screen, so a whole photo set costs 312KB no matter its length.
// The buffer is deliberately NOT freed when the overlay closes: the image
// objects are released with lv_obj_del_async() and must not outlive their data.
static lv_obj_t    *_imgCont  = nullptr;
static lv_obj_t    *_imgObj   = nullptr;
static lv_img_dsc_t _imgDsc;
static uint16_t    *_imgBuf   = nullptr;
static lv_timer_t  *_imgTimer = nullptr;
static int          _imgIdx   = -1;
static int          _imgCount = 0;
static uint32_t     _imgShown = 0;
static uint32_t     _panStart = 0;
static uint32_t     _phaseAt  = 0;
static uint8_t      _phase    = 0;   // 0 pan, 1 slide out, 2 swap, 3 slide in
static int16_t      _panY     = 0;

static const uint32_t SLIDE_MS = 550;
static const uint32_t DWELL_MS = 14000;
// Room the larger frame can drift within before an edge would show.
static const int16_t  PAN_SPAN_X = Slideshow::IMG_W - TFT_WIDTH;
static const int16_t  PAN_SPAN_Y = Slideshow::IMG_H - TFT_HEIGHT;

static bool loadImage(int idx) {
  const size_t bytes = (size_t)Slideshow::IMG_W * Slideshow::IMG_H * 2;
  if (!_imgBuf) {
    _imgBuf = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!_imgBuf) {
      Serial.println(F("[Saver] PSRAM image buffer unavailable"));
      return false;
    }
  }
  memset(_imgBuf, 0, bytes);
  int w = 0, h = 0;
  const uint32_t t0 = millis();
  if (!Slideshow::decode(idx, _imgBuf, &w, &h)) return false;
  Serial.printf("[Saver] image %d/%d %dx%d in %ums\n",
                idx + 1, _imgCount, w, h, (unsigned)(millis() - t0));

  _imgDsc.header.always_zero = 0;
  _imgDsc.header.w  = Slideshow::IMG_W;
  _imgDsc.header.h  = Slideshow::IMG_H;
  _imgDsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  _imgDsc.data_size = bytes;
  _imgDsc.data      = (const uint8_t*)_imgBuf;
  lv_img_cache_invalidate_src(&_imgDsc);   // same descriptor, new pixels
  lv_img_set_src(_imgObj, &_imgDsc);
  _imgIdx = idx;
  return true;
}

static bool loadNextValid();   // defined below showImages()

static void imgTimerCb(lv_timer_t *) {
  if (!_imgObj) return;
  const uint32_t now = millis();
  switch (_phase) {
    case 0: {   // slow drift, so the frame is never quite still
      const float t = (now - _panStart) / 1000.0f;
      const int16_t x = (int16_t)((PAN_SPAN_X / 2.0f) * (1.0f - cosf(t * 0.18f)));
      const int16_t y = (int16_t)((PAN_SPAN_Y / 2.0f) * (1.0f - cosf(t * 0.13f + 1.7f)));
      lv_obj_set_pos(_imgObj, -x, -y);
      _panY = -y;
      if (now - _imgShown >= DWELL_MS) { _phase = 1; _phaseAt = now; }
      break;
    }
    case 1: {   // slide the current frame off to the left
      float p = (now - _phaseAt) / (float)SLIDE_MS;
      if (p > 1.0f) p = 1.0f;
      lv_obj_set_pos(_imgObj, -(int32_t)(p * (TFT_WIDTH + 40)), _panY);
      if (p >= 1.0f) _phase = 2;
      break;
    }
    case 2: {   // decode the next frame while the screen is clear
      if (!loadNextValid()) {          // nothing decodable left: end on this frame
        lv_obj_set_pos(_imgObj, 0, 0);
        _panY = 0;
        _phase = 0;
        _panStart = _imgShown = now;
        break;
      }
      _imgCount = Slideshow::count();
      lv_obj_set_pos(_imgObj, TFT_WIDTH + 40, 0);
      _panY = 0;
      _phase = 3;
      _phaseAt = now;
      break;
    }
    case 3: {   // slide the new frame in from the right
      float p = (now - _phaseAt) / (float)SLIDE_MS;
      if (p > 1.0f) p = 1.0f;
      lv_obj_set_pos(_imgObj, (int32_t)((1.0f - p) * (TFT_WIDTH + 40)), 0);
      if (p >= 1.0f) { _phase = 0; _panStart = now; _imgShown = now; }
      break;
    }
  }
}

// A reboot during an upload can leave a truncated JPEG in the partition. Treat
// such a file as debris: drop it and move on rather than refusing to show any
// photos at all.
static bool loadFirstValid() {
  int guard = Slideshow::count() + 1;
  while (Slideshow::count() > 0 && guard-- > 0) {
    if (loadImage(0)) return true;
    Serial.println(F("[Saver] image 1 undecodable - removing it"));
    if (!Slideshow::removeAt(0)) return false;
  }
  return false;
}

static bool loadNextValid() {
  int guard = Slideshow::count() + 1;
  while (guard-- > 0) {
    const int n = Slideshow::count();
    if (n <= 0) return false;
    const int next = (_imgIdx + 1) % n;
    if (loadImage(next)) return true;
    Serial.printf("[Saver] image %d undecodable - removing it\n", next + 1);
    if (!Slideshow::removeAt(next)) return false;
  }
  return false;
}

static void showImages() {
  _imgCount = Slideshow::count();
  if (_imgCount <= 0) return;

  _imgCont = lv_obj_create(_overlay);
  lv_obj_set_size(_imgCont, TFT_WIDTH, TFT_HEIGHT);
  lv_obj_set_pos(_imgCont, 0, 0);
  lv_obj_set_style_bg_color(_imgCont, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(_imgCont, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(_imgCont, 0, 0);
  lv_obj_set_style_radius(_imgCont, 0, 0);
  lv_obj_set_style_pad_all(_imgCont, 0, 0);
  lv_obj_clear_flag(_imgCont, LV_OBJ_FLAG_SCROLLABLE);

  _imgObj = lv_img_create(_imgCont);
  lv_obj_set_pos(_imgObj, 0, 0);

  if (!loadFirstValid()) {
    lv_obj_del(_imgCont);
    _imgCont = nullptr;
    _imgObj  = nullptr;
    return;
  }
  _imgCount = Slideshow::count();
  _phase    = 0;
  _panStart = millis();
  _imgShown = millis();
  _imgTimer = lv_timer_create(imgTimerCb, 50, nullptr);
}

static void hideImages() {
  if (_imgTimer) { lv_timer_del(_imgTimer); _imgTimer = nullptr; }
  // Objects go away with the overlay (deleted async); the buffer stays owned
  // here so nothing can be freed underneath a pending redraw.
  _imgCont = nullptr;
  _imgObj  = nullptr;
  _imgIdx  = -1;
  _phase   = 0;
}

static void showOverlay() {
  if (_overlay) return;
  initStyles();

  _overlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(_overlay, TFT_WIDTH, TFT_HEIGHT);
  lv_obj_set_pos(_overlay, 0, 0);
  lv_obj_set_style_bg_color(_overlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(_overlay, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(_overlay, 0, 0);
  lv_obj_set_style_radius(_overlay, 0, 0);
  lv_obj_set_style_pad_all(_overlay, 0, 0);
  lv_obj_clear_flag(_overlay, LV_OBJ_FLAG_SCROLLABLE);

  // Photos first, so the clock and ticker draw on top of them.
  showImages();

  _floatClock = lv_label_create(_overlay);
  lv_label_set_text(_floatClock, "--:--");
  lv_obj_add_style(_floatClock, &_stClock, 0);
  lv_obj_set_pos(_floatClock, _floatX, _floatY);

  _floatDate = lv_label_create(_overlay);
  lv_label_set_text(_floatDate, "");
  lv_obj_add_style(_floatDate, &_stDate, 0);
  lv_obj_set_pos(_floatDate, _floatX, _floatY + 58);

  // Ticker bar — 24px tall, 4px from bottom edge
  const lv_coord_t TICKER_Y = TFT_HEIGHT - 28;
  ThemePalette pal = Themes::get(Storage::cfg.theme);
  _tickerCont = lv_obj_create(_overlay);
  lv_obj_set_size(_tickerCont, TFT_WIDTH, 24);
  lv_obj_set_pos(_tickerCont, 0, TICKER_Y);
  lv_obj_set_style_bg_color(_tickerCont, pal.tickerBg, 0);
  lv_obj_set_style_bg_opa(_tickerCont, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(_tickerCont, 0, 0);
  lv_obj_set_style_radius(_tickerCont, 0, 0);
  lv_obj_set_style_pad_all(_tickerCont, 0, 0);
  lv_obj_clear_flag(_tickerCont, LV_OBJ_FLAG_SCROLLABLE);

  _tickerLbl = lv_label_create(_tickerCont);
  lv_obj_add_style(_tickerLbl, &_stTickerTxt, 0);
  lv_label_set_long_mode(_tickerLbl, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_pad_all(_tickerLbl, 0, 0);
  lv_obj_set_pos(_tickerLbl, TFT_WIDTH + 10, 4);
  _tickerX = TFT_WIDTH + 10;

  const char *full = TickerEngine::getFullString();
  if (full && full[0]) {
    lv_label_set_text(_tickerLbl, full);
    lv_obj_set_width(_tickerLbl, LV_SIZE_CONTENT);
  }

  _scrollTimer = lv_timer_create(scrollCb, 32, nullptr);

  if (_setBrightness) _setBrightness(60);
}

static void hideOverlay() {
  if (!_overlay) return;
  if (_scrollTimer) { lv_timer_del(_scrollTimer); _scrollTimer = nullptr; }
  hideImages();
  // hideOverlay() runs from the indev read callback (wake-on-touch). Deleting the
  // object LVGL is still holding as the active touch target frees memory LVGL goes
  // on to dereference -> LoadProhibited. Defer the free to the next lv_timer_handler.
  lv_obj_del_async(_overlay);
  _overlay = _floatClock = _floatDate = _tickerCont = _tickerLbl = nullptr;
  if (_setBrightness) _setBrightness(200);
}

static void updateClock() {
  if (!_floatClock) return;
  time_t now = time(nullptr);
  char tbuf[16], dbuf[24];
  if (now > 100000) {
    struct tm *t = localtime(&now);
    if (Storage::cfg.clock24h)
      snprintf(tbuf, sizeof(tbuf), "%02d:%02d", t->tm_hour, t->tm_min);
    else {
      int h = t->tm_hour % 12; if (!h) h = 12;
      snprintf(tbuf, sizeof(tbuf), "%d:%02d %s", h, t->tm_min,
               t->tm_hour < 12 ? "AM" : "PM");
    }
    strftime(dbuf, sizeof(dbuf), "%A, %b %e", t);
  } else {
    strlcpy(tbuf, "--:--", sizeof(tbuf));
    dbuf[0] = '\0';
  }
  lv_label_set_text(_floatClock, tbuf);
  if (_floatDate) lv_label_set_text(_floatDate, dbuf);

  // Refresh ticker text periodically so new data appears
  if (_tickerLbl) {
    const char *full = TickerEngine::getFullString();
    if (full && full[0]) {
      lv_label_set_text(_tickerLbl, full);
      lv_obj_set_width(_tickerLbl, LV_SIZE_CONTENT);
    }
  }
}

static void moveOverlay() {
  _floatX += _driftX;
  _floatY += _driftY;
  // Keep clock block (90px tall) above ticker bar (28px from bottom)
  if (_floatX <  8)              { _floatX =  8;              _driftX = abs(_driftX); }
  if (_floatX > TFT_WIDTH - 210) { _floatX = TFT_WIDTH - 210; _driftX = -abs(_driftX); }
  if (_floatY <  8)              { _floatY =  8;              _driftY = abs(_driftY); }
  if (_floatY > TFT_HEIGHT - 128){ _floatY = TFT_HEIGHT - 128;_driftY = -abs(_driftY); }
  if (_floatClock) lv_obj_set_pos(_floatClock, _floatX, _floatY);
  if (_floatDate)  lv_obj_set_pos(_floatDate,  _floatX, _floatY + 58);
}

// ── Public ──────────────────────────────────────────────────────────────────
void begin(lv_obj_t *mainScreen, BrightnessFn cb) {
  _screen        = mainScreen;
  _setBrightness = cb;
  _active        = false;
  _lastTouchMs   = millis();
  _lastMoveMs    = millis();
  _lastClockMs   = millis();
  _driftX = (random(2) ? 2 : -2);
  _driftY = (random(2) ? 1 : -1);
}

void tick() {
  if (_idleTimeout == 0) return;
  uint32_t now = millis();

  if (!_active && (now - _lastTouchMs) >= _idleTimeout) {
    _active = true;
    _floatX = TFT_WIDTH / 2 - 60;
    _floatY = TFT_HEIGHT / 2 - 50;
    showOverlay();
    updateClock();
  }

  if (!_active) return;

  if (now - _lastClockMs >= 10000) {
    _lastClockMs = now;
    updateClock();
  }

  if (now - _lastMoveMs >= 3000) {
    _lastMoveMs = now;
    moveOverlay();
  }
}

void onTouch() {
  _lastTouchMs = millis();
  if (_active) {
    _active = false;
    hideOverlay();
    if (_screen) lv_obj_set_pos(_screen, 0, 0);
  }
}

bool isActive()                       { return _active; }
void setIdleTimeout(uint32_t seconds) { _idleTimeout = seconds * 1000UL; }

} // namespace Screensaver
