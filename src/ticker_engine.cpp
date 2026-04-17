/**
 * ticker_engine.cpp
 * Fixed: segments now rebuild immediately when data first becomes available,
 * not just on a fixed interval. Uses a "was ready" flag to detect first arrival.
 */

#include "ticker_engine.h"
#include "storage.h"
#include "data/data_manager.h"
#include <time.h>

namespace TickerEngine {

TickerSegment     segments[SEG_COUNT];
TickerScrollState scrollState;

static bool   paused      = false;
static bool   dirty       = false;
static char   fullString[2048];
static uint32_t lastClockMin = 0xFFFF;

// Track previous ready states — rebuild immediately when data first arrives
static bool wasWeatherReady = false;
static bool wasSportsReady  = false;
static bool wasStocksReady  = false;
static bool wasCryptoReady  = false;

// Rate-limit re-rebuilds after first arrival
static uint32_t lastWeatherRebuild = 0;
static uint32_t lastSportsRebuild  = 0;
static uint32_t lastStocksRebuild  = 0;
static uint32_t lastCryptoRebuild  = 0;

// ── Scroll timer ──────────────────────────────────────────────────────────────
static uint8_t _tickCount = 0;
static void scrollTimerCb(lv_timer_t *) {
  if (paused || !scrollState.label) return;
  _tickCount++;
  // speed 1-5 mapped to pixels/tick at 32ms interval:
  // 1 = 1px every 4 ticks (~8px/s)   2 = 1px every 2 ticks (~16px/s)
  // 3 = 1px/tick (~30px/s)            4 = 2px/tick (~60px/s)
  // 5 = 3px/tick (~90px/s)
  uint8_t spd = scrollState.speed;
  int move = 0;
  if      (spd <= 1) { move = (_tickCount % 4 == 0) ? 1 : 0; }
  else if (spd == 2) { move = (_tickCount % 2 == 0) ? 1 : 0; }
  else if (spd == 3) { move = 1; }
  else if (spd == 4) { move = 2; }
  else               { move = 3; }
  if (move == 0) return;
  scrollState.xPos -= move;
  lv_coord_t labelW = lv_obj_get_width(scrollState.label);
  if (scrollState.xPos < -(labelW + 20)) {
    scrollState.xPos = TFT_WIDTH + 10;
    if (dirty) { refreshLabel(); dirty = false; }
  }
  lv_obj_set_x(scrollState.label, scrollState.xPos);
}

// ── Segment builders ──────────────────────────────────────────────────────────
static void buildClockSegment() {
  time_t now = time(nullptr);
  if (now < 100000) {
    // NTP not synced yet — don't show garbage time
    strlcpy(segments[SEG_CLOCK].text, "", sizeof(segments[SEG_CLOCK].text));
    return;
  }
  struct tm *t = localtime(&now);
  char buf[20];
  if (Storage::cfg.clock24h)
    snprintf(buf, sizeof(buf), "%02d:%02d    ", t->tm_hour, t->tm_min);
  else {
    int h = t->tm_hour % 12; if (!h) h = 12;
    snprintf(buf, sizeof(buf), "%d:%02d %s    ",
      h, t->tm_min, t->tm_hour < 12 ? "AM" : "PM");
  }
  segments[SEG_CLOCK].type    = SEG_CLOCK;
  segments[SEG_CLOCK].enabled = true;
  strlcpy(segments[SEG_CLOCK].text, buf, sizeof(segments[SEG_CLOCK].text));
}

static void buildWeatherSegment() {
  TickerSegment &s = segments[SEG_WEATHER];
  s.type    = SEG_WEATHER;
  s.enabled = (Storage::cfg.widgetMask & WIDGET_WEATHER) != 0;
  if (!s.enabled || !DataManager::weatherReady) {
    strlcpy(s.text, "", sizeof(s.text)); return;
  }
  auto &w = DataManager::weather;
  char buf[96];
  snprintf(buf, sizeof(buf), "%s %.0f\xB0""F %s    ",
    w.city, w.tempF, w.description);
  strlcpy(s.text, buf, sizeof(s.text));
}

static void buildSportsSegment() {
  TickerSegment &s = segments[SEG_SPORTS];
  s.type    = SEG_SPORTS;
  s.enabled = (Storage::cfg.widgetMask & WIDGET_SPORTS) != 0;
  if (!s.enabled || !DataManager::scoresReady) {
    strlcpy(s.text, "", sizeof(s.text)); return;
  }
  if (DataManager::scoreCount == 0) {
    strlcpy(s.text, "No games today    ", sizeof(s.text)); return;
  }
  s.text[0] = '\0';
  for (int i = 0; i < DataManager::scoreCount; i++) {
    auto &sc = DataManager::scores[i];
    bool isSched = !strstr(sc.status,"Final") &&
                   (strstr(sc.status,"PM") || strstr(sc.status,"AM"));
    char buf[80];
    if (isSched)
      snprintf(buf, sizeof(buf), "%s: %s vs %s %s    ",
        sc.league, sc.awayTeam, sc.homeTeam, sc.status);
    else
      snprintf(buf, sizeof(buf), "%s: %s %s-%s %s (%s)    ",
        sc.league, sc.awayTeam, sc.awayScore,
        sc.homeScore, sc.homeTeam, sc.status);
    strlcat(s.text, buf, sizeof(s.text));
  }
}

static void buildStocksSegment() {
  TickerSegment &s = segments[SEG_STOCKS];
  s.type    = SEG_STOCKS;
  s.enabled = (Storage::cfg.widgetMask & WIDGET_STOCKS) != 0;
  if (!s.enabled || !DataManager::stocksReady || DataManager::stockCount == 0) {
    strlcpy(s.text, "", sizeof(s.text)); return;
  }
  s.text[0] = '\0';
  for (int i = 0; i < DataManager::stockCount; i++) {
    auto &st = DataManager::stocks[i];
    char buf[48];
    snprintf(buf, sizeof(buf), "%s $%.2f %+.2f%%    ",
      st.symbol, st.price, st.changePercent);
    strlcat(s.text, buf, sizeof(s.text));
  }
}

static void buildCryptoSegment() {
  TickerSegment &s = segments[SEG_CRYPTO];
  s.type    = SEG_CRYPTO;
  s.enabled = (Storage::cfg.widgetMask & WIDGET_CRYPTO) != 0;
  if (!s.enabled || !DataManager::cryptoReady || DataManager::cryptoCount == 0) {
    strlcpy(s.text, "", sizeof(s.text)); return;
  }
  s.text[0] = '\0';
  for (int i = 0; i < DataManager::cryptoCount; i++) {
    auto &c = DataManager::cryptos[i];
    char buf[48];
    if (c.priceUSD >= 1000.0f)
      snprintf(buf, sizeof(buf), "%s $%.0f %+.2f%%    ", c.symbol, c.priceUSD, c.change24h);
    else
      snprintf(buf, sizeof(buf), "%s $%.3f %+.2f%%    ", c.symbol, c.priceUSD, c.change24h);
    strlcat(s.text, buf, sizeof(s.text));
  }
}

// ── Public ────────────────────────────────────────────────────────────────────
void rebuildSegment(TickerSegmentType type) {
  switch (type) {
    case SEG_CLOCK:   buildClockSegment();   break;
    case SEG_WEATHER: buildWeatherSegment(); break;
    case SEG_SPORTS:  buildSportsSegment();  break;
    case SEG_STOCKS:  buildStocksSegment();  break;
    case SEG_CRYPTO:  buildCryptoSegment();  break;
    default: break;
  }
  dirty = true;
}

void rebuildAll() {
  for (int i = 0; i < SEG_COUNT; i++)
    rebuildSegment((TickerSegmentType)i);
}

void refreshLabel() {
  fullString[0] = '\0';
  for (int i = 0; i < SEG_COUNT; i++) {
    if (segments[i].enabled && strlen(segments[i].text) > 0)
      strlcat(fullString, segments[i].text, sizeof(fullString));
  }
  if (strlen(fullString) == 0)
    strlcpy(fullString, "Connecting...    ", sizeof(fullString));
  if (scrollState.label)
    lv_label_set_text(scrollState.label, fullString);
}

void tick() {
  uint32_t now32 = millis();
  bool anyChange = false;

  // Clock: check every call but only rebuild when minute changes
  time_t nowT = time(nullptr);
  if (nowT > 100000) {
    struct tm *t = localtime(&nowT);
    uint32_t thisMin = (uint32_t)(t->tm_hour * 60 + t->tm_min);
    if (thisMin != lastClockMin) {
      lastClockMin = thisMin;
      buildClockSegment();
      anyChange = true;
    }
  }

  // Data segments: rebuild immediately on first arrival, then rate-limit
  #define CHECK(rdy, was, last, interval, fn) \
    if (DataManager::rdy) { \
      if (!was || (now32 - last >= (uint32_t)interval)) { \
        was=true; last=now32; fn(); anyChange=true; \
      } \
    }
  CHECK(weatherReady, wasWeatherReady, lastWeatherRebuild, REFRESH_WEATHER_MS, buildWeatherSegment)
  CHECK(scoresReady,  wasSportsReady,  lastSportsRebuild,  REFRESH_SPORTS_MS,  buildSportsSegment)
  CHECK(stocksReady,  wasStocksReady,  lastStocksRebuild,  REFRESH_STOCKS_MS,  buildStocksSegment)
  CHECK(cryptoReady,  wasCryptoReady,  lastCryptoRebuild,  REFRESH_CRYPTO_MS,  buildCryptoSegment)
  #undef CHECK

  if (anyChange) { dirty = true; refreshLabel(); }
}

void begin(lv_obj_t *container) {
  scrollState.container = container;
  scrollState.speed     = Storage::cfg.tickerSpeed;
  scrollState.xPos      = TFT_WIDTH + 10;

  lv_obj_set_style_clip_corner(container, true, 0);
  lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);

  scrollState.label = lv_label_create(container);
  lv_obj_set_style_text_color(scrollState.label, lv_color_make(210, 210, 230), 0);
  lv_obj_set_pos(scrollState.label, TFT_WIDTH + 10, (TICKER_BAR_H - 16) / 2);
  lv_label_set_long_mode(scrollState.label, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(scrollState.label, LV_SIZE_CONTENT);
  lv_label_set_text(scrollState.label, "Loading...");

  wasWeatherReady = wasSportsReady = wasStocksReady = wasCryptoReady = false;
  lastWeatherRebuild = lastSportsRebuild = lastStocksRebuild = lastCryptoRebuild = 0;
  lastClockMin = 0xFFFF;
  dirty = false;

  // Scroll animation driven by lv_timer (runs inside lv_timer_handler on Core 1)
  lv_timer_create(scrollTimerCb, 32, nullptr);
}

void reattach(lv_obj_t *newContainer) {
  scrollState.container = newContainer;
  scrollState.xPos      = TFT_WIDTH + 10;
  scrollState.speed     = Storage::cfg.tickerSpeed;
  lv_obj_set_style_clip_corner(newContainer, true, 0);
  lv_obj_clear_flag(newContainer, LV_OBJ_FLAG_SCROLLABLE);
  scrollState.label = lv_label_create(newContainer);
  lv_obj_set_pos(scrollState.label, TFT_WIDTH + 10, (TICKER_BAR_H - 16) / 2);
  lv_label_set_long_mode(scrollState.label, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(scrollState.label, LV_SIZE_CONTENT);
  refreshLabel();
}

void setSpeed(uint8_t speed) {
  if (speed < 1) speed = 1;
  if (speed > 5) speed = 5;
  scrollState.speed = speed;
  Storage::cfg.tickerSpeed = speed;
  Storage::save();
}

void setPaused(bool p) { paused = p; }
const char* getCurrentString() { return fullString; }

} // namespace TickerEngine
