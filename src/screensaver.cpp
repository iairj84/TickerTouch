#include "screensaver.h"
#include "display.h"
#include "themes/themes.h"
#include "storage.h"
#include "ticker_engine.h"
#include "slideshow.h"

namespace Screensaver {

static lv_obj_t    *_screen        = nullptr;
static BrightnessFn _setBrightness = nullptr;
static bool         _active        = false;
static uint32_t     _idleTimeout   = SCREENSAVER_TIMEOUT_S * 1000UL;
static uint32_t     _lastTouchMs   = 0;
static uint32_t     _lastClockMs   = 0;

static lv_obj_t    *_overlay       = nullptr;

// Info bar: clock on the left, ticker scrolling to its right. Docking the clock
// here instead of floating it over the photo keeps the whole image unobstructed
// and gives the text a solid background, so it stays readable on any picture.
// The photo is full-bleed; the bar is a translucent scrim over its bottom edge.
// Reserving 56px for a solid bar made the visible window 2.2:1, which forced blur
// strips even on 16:9 photos. At full height they fill the panel almost exactly.
static const lv_coord_t BAR_H      = 48;
static const lv_coord_t PHOTO_H    = TFT_HEIGHT;
static const lv_coord_t CLOCK_W    = 118;

static lv_obj_t    *_bar           = nullptr;
static lv_obj_t    *_clockLbl      = nullptr;
static lv_obj_t    *_dateLbl       = nullptr;
static lv_obj_t    *_tickerClip    = nullptr;

// Ticker scroll state
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
  // The bar is dark in every theme, so the clock does not follow the theme.
  lv_style_set_text_color(&_stClock, lv_color_white());
  lv_style_set_text_font(&_stClock,  &lv_font_montserrat_24);
  lv_style_set_text_color(&_stDate,  lv_color_make(150, 150, 185));
  lv_style_set_text_font(&_stDate,   &lv_font_montserrat_14);
  // Ticker text uses theme's ticker text color
  lv_style_set_text_color(&_stTickerTxt, pal.tickerText);
  lv_style_set_text_font(&_stTickerTxt,  &lv_font_montserrat_14);
}

static void scrollCb(lv_timer_t *) {
  if (!_tickerLbl) return;
  _tickerX -= 2;
  const int32_t clipW = _tickerClip ? lv_obj_get_width(_tickerClip) : TFT_WIDTH;
  if (_tickerX < -(int32_t)lv_obj_get_width(_tickerLbl)) _tickerX = clipW + 10;
  lv_obj_set_x(_tickerLbl, _tickerX);
}

// ── Photo slideshow ─────────────────────────────────────────────────────────
// Two decoded frames live in PSRAM. The incoming frame is faded in on top of the
// current one: at the ~12fps this panel's blocking QSPI flushes allow, a dissolve
// reads as smooth where a sliding window becomes a series of 50px jumps.
// The buffers are deliberately NOT freed when the overlay closes: the image
// objects are released with lv_obj_del_async() and must not outlive their data.
static lv_obj_t    *_imgCont  = nullptr;
static lv_obj_t    *_imgObj   = nullptr;   // frame on screen
static lv_obj_t    *_imgObjNext = nullptr; // frame being faded in
static lv_img_dsc_t _dsc[2];
static uint16_t    *_buf[2]   = { nullptr, nullptr };
static uint8_t      _cur      = 0;         // buffer index shown in _imgObj
static lv_timer_t  *_imgTimer = nullptr;
static int          _imgIdx   = -1;
static int          _imgCount = 0;
static uint32_t     _imgShown = 0;
static uint32_t     _phaseAt  = 0;
static uint8_t      _phase    = 0;   // 0 pan/hold, 1 decode next, 2 crossfade
static const size_t IMG_BYTES = (size_t)Slideshow::IMG_W * Slideshow::IMG_H * 2;

static const uint32_t DWELL_MS = 14000;
static const uint32_t FADE_MS  = 900;
// Uploads are composed on a 520x300 canvas with the whole photo centred in the
// 480x216 window; these offsets line that window up exactly with the panel.
static const int16_t  IMG_OFF_X = -(Slideshow::IMG_W - TFT_WIDTH) / 2;
static const int16_t  IMG_OFF_Y = -(Slideshow::IMG_H - PHOTO_H) / 2;

static bool ensureBuf(uint8_t slot) {
  if (_buf[slot]) return true;
  _buf[slot] = (uint16_t*)heap_caps_malloc(IMG_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!_buf[slot]) {
    Serial.printf("[Saver] PSRAM frame %u unavailable\n", (unsigned)slot);
    return false;
  }
  return true;
}

static bool loadImage(int idx, uint8_t slot) {
  if (!ensureBuf(slot)) return false;
  memset(_buf[slot], 0, IMG_BYTES);
  int w = 0, h = 0;
  const uint32_t t0 = millis();
  if (!Slideshow::decode(idx, _buf[slot], &w, &h)) return false;
  Serial.printf("[Saver] image %d/%d %dx%d in %ums (slot %u)\n",
                idx + 1, _imgCount, w, h, (unsigned)(millis() - t0), (unsigned)slot);

  lv_img_dsc_t *d = &_dsc[slot];
  d->header.always_zero = 0;
  d->header.w  = Slideshow::IMG_W;
  d->header.h  = Slideshow::IMG_H;
  d->header.cf = LV_IMG_CF_TRUE_COLOR;
  d->data_size = IMG_BYTES;
  d->data      = (const uint8_t*)_buf[slot];
  _imgIdx = idx;
  return true;
}

// Same descriptor, new pixels every cycle — drop any cached entry first.
static void setFrame(lv_obj_t *obj, uint8_t slot) {
  lv_img_cache_invalidate_src(&_dsc[slot]);
  lv_img_set_src(obj, &_dsc[slot]);
}

static bool loadNextValid();   // defined below showImages()

static void imgTimerCb(lv_timer_t *) {
  if (!_imgObj) return;
  const uint32_t now = millis();
  switch (_phase) {
    case 0: {   // the photo is composed to fit, so the frame holds still
      if (now - _imgShown >= DWELL_MS) { _phase = 1; _phaseAt = now; }
      break;
    }
    case 1: {   // decode the replacement while the current frame still shows
      if (!loadNextValid()) {          // nothing decodable left: hold this frame
        _imgShown = now;
        _phase = 0;
        break;
      }
      _imgCount = Slideshow::count();
      if (_imgObjNext) {
        setFrame(_imgObjNext, _cur ^ 1);
        lv_obj_set_pos(_imgObjNext, IMG_OFF_X, IMG_OFF_Y);
        lv_obj_set_style_img_opa(_imgObjNext, LV_OPA_TRANSP, 0);
        _phase = 2;
      } else {
        // No second buffer: fall back to a hard cut rather than a broken fade.
        _cur ^= 1;
        setFrame(_imgObj, _cur);
        lv_obj_set_pos(_imgObj, IMG_OFF_X, IMG_OFF_Y);
        _imgShown = now;
        _phase = 0;
      }
      _phaseAt = now;
      break;
    }
    case 2: {   // dissolve
      float p = (now - _phaseAt) / (float)FADE_MS;
      if (p > 1.0f) p = 1.0f;
      lv_obj_set_style_img_opa(_imgObjNext, (lv_opa_t)(uint16_t)(p * 255.0f), 0);
      if (p >= 1.0f) {
        _cur ^= 1;
        setFrame(_imgObj, _cur);
        lv_obj_set_style_img_opa(_imgObj, LV_OPA_COVER, 0);
        lv_obj_set_style_img_opa(_imgObjNext, LV_OPA_TRANSP, 0);
        lv_obj_set_pos(_imgObj, IMG_OFF_X, IMG_OFF_Y);
        lv_obj_set_pos(_imgObjNext, IMG_OFF_X, IMG_OFF_Y);
        _imgShown = now;
        _phase = 0;
      }
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
    if (loadImage(0, _cur)) return true;
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
    if (loadImage(next, _cur ^ 1)) return true;
    Serial.printf("[Saver] image %d undecodable - removing it\n", next + 1);
    if (!Slideshow::removeAt(next)) return false;
  }
  return false;
}

static void showImages() {
  _imgCount = Slideshow::count();
  if (_imgCount <= 0) return;
  _cur = 0;
  if (!ensureBuf(0) || !loadFirstValid()) return;

  _imgCont = lv_obj_create(_overlay);
  lv_obj_set_size(_imgCont, TFT_WIDTH, PHOTO_H);
  lv_obj_set_pos(_imgCont, 0, 0);
  lv_obj_set_style_bg_color(_imgCont, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(_imgCont, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(_imgCont, 0, 0);
  lv_obj_set_style_radius(_imgCont, 0, 0);
  lv_obj_set_style_pad_all(_imgCont, 0, 0);
  lv_obj_clear_flag(_imgCont, LV_OBJ_FLAG_SCROLLABLE);

  _imgObj = lv_img_create(_imgCont);
  lv_obj_set_pos(_imgObj, IMG_OFF_X, IMG_OFF_Y);
  setFrame(_imgObj, _cur);

  // Second frame buffer only if PSRAM allows; without it we cut instead of fade.
  if (ensureBuf(1)) {
    _imgObjNext = lv_img_create(_imgCont);
    lv_obj_set_pos(_imgObjNext, IMG_OFF_X, IMG_OFF_Y);
    lv_obj_set_style_img_opa(_imgObjNext, LV_OPA_TRANSP, 0);
  } else {
    _imgObjNext = nullptr;
  }

  _imgCount = Slideshow::count();
  _phase    = 0;
  _imgShown = millis();
  _imgTimer = lv_timer_create(imgTimerCb, 30, nullptr);
}

static void hideImages() {
  if (_imgTimer) { lv_timer_del(_imgTimer); _imgTimer = nullptr; }
  // Objects go away with the overlay (deleted async); the buffers stay owned
  // here so nothing can be freed underneath a pending redraw.
  _imgCont = nullptr;
  _imgObj  = nullptr;
  _imgObjNext = nullptr;
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

  // Photos fill everything above the bar, so the bar never covers the picture.
  showImages();

  ThemePalette pal = Themes::get(Storage::cfg.theme);
  _bar = lv_obj_create(_overlay);
  lv_obj_set_size(_bar, TFT_WIDTH, BAR_H);
  lv_obj_set_pos(_bar, 0, TFT_HEIGHT - BAR_H);
  lv_obj_set_style_bg_color(_bar, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(_bar, 190, 0);   // ~75%: readable, photo still shows through
  lv_obj_set_style_border_width(_bar, 0, 0);
  lv_obj_set_style_radius(_bar, 0, 0);
  lv_obj_set_style_pad_all(_bar, 0, 0);
  lv_obj_clear_flag(_bar, LV_OBJ_FLAG_SCROLLABLE);

  _clockLbl = lv_label_create(_bar);
  lv_label_set_text(_clockLbl, "--:--");
  lv_obj_add_style(_clockLbl, &_stClock, 0);
  lv_obj_set_pos(_clockLbl, 10, 1);

  _dateLbl = lv_label_create(_bar);
  lv_label_set_text(_dateLbl, "");
  lv_obj_add_style(_dateLbl, &_stDate, 0);
  lv_obj_set_pos(_dateLbl, 10, 28);

  // Clip window for the ticker, right of the clock.
  _tickerClip = lv_obj_create(_bar);
  lv_obj_set_size(_tickerClip, TFT_WIDTH - CLOCK_W, BAR_H);
  lv_obj_set_pos(_tickerClip, CLOCK_W, 0);
  lv_obj_set_style_bg_opa(_tickerClip, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(_tickerClip, 0, 0);
  lv_obj_set_style_radius(_tickerClip, 0, 0);
  lv_obj_set_style_pad_all(_tickerClip, 0, 0);
  lv_obj_clear_flag(_tickerClip, LV_OBJ_FLAG_SCROLLABLE);

  _tickerLbl = lv_label_create(_tickerClip);
  lv_obj_add_style(_tickerLbl, &_stTickerTxt, 0);
  lv_label_set_long_mode(_tickerLbl, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_pad_all(_tickerLbl, 0, 0);
  lv_obj_set_pos(_tickerLbl, TFT_WIDTH, 17);
  _tickerX = TFT_WIDTH;

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
  _overlay = _clockLbl = _dateLbl = _bar = _tickerClip = _tickerLbl = nullptr;
  if (_setBrightness) _setBrightness(200);
}

static void updateClock() {
  if (!_clockLbl) return;
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
    strftime(dbuf, sizeof(dbuf), "%a, %b %e", t);
  } else {
    strlcpy(tbuf, "--:--", sizeof(tbuf));
    dbuf[0] = '\0';
  }
  lv_label_set_text(_clockLbl, tbuf);
  if (_dateLbl) lv_label_set_text(_dateLbl, dbuf);

  // Refresh ticker text periodically so new data appears
  if (_tickerLbl) {
    const char *full = TickerEngine::getFullString();
    if (full && full[0]) {
      lv_label_set_text(_tickerLbl, full);
      lv_obj_set_width(_tickerLbl, LV_SIZE_CONTENT);
    }
  }
}

// ── Public ──────────────────────────────────────────────────────────────────
void begin(lv_obj_t *mainScreen, BrightnessFn cb) {
  _screen        = mainScreen;
  _setBrightness = cb;
  _active        = false;
  _lastTouchMs   = millis();
  _lastClockMs   = millis();
}

void tick() {
  if (_idleTimeout == 0) return;
  uint32_t now = millis();

  if (!_active && (now - _lastTouchMs) >= _idleTimeout) {
    _active = true;
    showOverlay();
    updateClock();
  }

  if (!_active) return;

  if (now - _lastClockMs >= 10000) {
    _lastClockMs = now;
    updateClock();
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
