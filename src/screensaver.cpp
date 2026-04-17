#include "screensaver.h"
#include "display.h"
#include "themes/themes.h"
#include "storage.h"

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

// Start near centre, drift outward
static int16_t _floatX = 160;
static int16_t _floatY = 100;
static int8_t  _driftX = 2;
static int8_t  _driftY = 1;

static void showOverlay() {
  if (_overlay) return;

  _overlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(_overlay, TFT_WIDTH, TFT_HEIGHT);
  lv_obj_set_pos(_overlay, 0, 0);
  lv_obj_set_style_bg_color(_overlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(_overlay, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(_overlay, 0, 0);
  lv_obj_set_style_radius(_overlay, 0, 0);
  lv_obj_clear_flag(_overlay, LV_OBJ_FLAG_SCROLLABLE);

  // Large clock — 48pt
  _floatClock = lv_label_create(_overlay);
  lv_label_set_text(_floatClock, "--:--");
  lv_obj_set_style_text_color(_floatClock, lv_color_make(160, 160, 200), 0);
  lv_obj_set_style_text_font(_floatClock, &lv_font_montserrat_48, 0);
  lv_obj_set_pos(_floatClock, _floatX, _floatY);

  // Date line below — 24pt
  _floatDate = lv_label_create(_overlay);
  lv_label_set_text(_floatDate, "");
  lv_obj_set_style_text_color(_floatDate, lv_color_make(110, 110, 150), 0);
  lv_obj_set_style_text_font(_floatDate, &lv_font_montserrat_24, 0);
  lv_obj_set_pos(_floatDate, _floatX, _floatY + 58);

  if (_setBrightness) _setBrightness(60); // less dim — was 20
}

static void hideOverlay() {
  if (!_overlay) return;
  lv_obj_del(_overlay);
  _overlay = _floatClock = _floatDate = nullptr;
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
      int h = t->tm_hour%12; if(!h) h=12;
      snprintf(tbuf, sizeof(tbuf), "%d:%02d %s", h, t->tm_min, t->tm_hour<12?"AM":"PM");
    }
    strftime(dbuf, sizeof(dbuf), "%A, %b %e", t);
  } else {
    strlcpy(tbuf, "--:--", sizeof(tbuf));
    dbuf[0] = 0;
  }
  lv_label_set_text(_floatClock, tbuf);
  if (_floatDate) lv_label_set_text(_floatDate, dbuf);
}

static void moveOverlay() {
  // Move 3px per step — visible bouncing around the screen
  _floatX += _driftX;
  _floatY += _driftY;
  // Clock is ~120px wide, 28px tall + 20px date line = ~80px total height
  // Clock at 48pt is ~200px wide, 58px tall; date at 24pt ~30px. Total height ~95px
  if (_floatX <  8)              { _floatX =  8;              _driftX = abs(_driftX); }
  if (_floatX > TFT_WIDTH - 210) { _floatX = TFT_WIDTH - 210; _driftX = -abs(_driftX); }
  if (_floatY <  8)              { _floatY =  8;              _driftY = abs(_driftY); }
  if (_floatY > TFT_HEIGHT - 100){ _floatY = TFT_HEIGHT - 100;_driftY = -abs(_driftY); }
  if (_floatClock) lv_obj_set_pos(_floatClock, _floatX, _floatY);
  if (_floatDate)  lv_obj_set_pos(_floatDate,  _floatX, _floatY + 58);
}

// ── Public ────────────────────────────────────────────────────────────────────
void begin(lv_obj_t *mainScreen, BrightnessFn cb) {
  _screen        = mainScreen;
  _setBrightness = cb;
  _active        = false;
  _lastTouchMs   = millis();
  _lastMoveMs    = millis();
  _lastClockMs   = millis();
  // Random initial drift direction
  _driftX = (random(2) ? 2 : -2);
  _driftY = (random(2) ? 1 : -1);
}

void tick() {
  if (_idleTimeout == 0) return;
  uint32_t now = millis();

  // Activate after idle
  if (!_active && (now - _lastTouchMs) >= _idleTimeout) {
    _active = true;
    _floatX = TFT_WIDTH/2 - 60;
    _floatY = TFT_HEIGHT/2 - 40;
    showOverlay();
    updateClock();
  }

  if (!_active) return;

  // Update clock every 10 seconds
  if (now - _lastClockMs >= 10000) {
    _lastClockMs = now;
    updateClock();
  }

  // Move every 3 seconds
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
