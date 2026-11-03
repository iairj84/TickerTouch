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
static char   fullString[8192];   // all segments concatenated; sports alone can be 2 KB
static uint32_t lastClockMin = 0xFFFF;

// Track previous ready states — rebuild immediately when data first arrives
static bool wasWeatherReady = false;
static bool wasSportsReady  = false;
static bool wasStocksReady  = false;
static bool wasCryptoReady  = false;
static bool wasCalReady     = false;

// Rate-limit re-rebuilds after first arrival
static uint32_t lastWeatherRebuild = 0;
static uint32_t lastSportsRebuild  = 0;
static uint32_t lastStocksRebuild  = 0;
static uint32_t lastCryptoRebuild  = 0;
static uint32_t lastCalRebuild     = 0;

// ── Scroll timer ──────────────────────────────────────────────────────────────
static lv_timer_t *_scrollTimer = nullptr;

static void scrollTimerCb(lv_timer_t *) {
  if (paused || !scrollState.label) return;
  // Always move 1px per tick — smoothness comes from interval, not jump size
  scrollState.xPos -= 1;
  lv_coord_t labelW = lv_obj_get_width(scrollState.label);
  if (scrollState.xPos < -(labelW + 20)) {
    scrollState.xPos = TFT_WIDTH + 10;
    if (dirty) { refreshLabel(); dirty = false; }
  }
  lv_obj_set_x(scrollState.label, scrollState.xPos);
}

// Map speed 1-5 to timer interval in ms (lower = faster scroll)
// 1 = ~40px/s,  2 = ~60px/s,  3 = ~80px/s,  4 = ~100px/s,  5 = ~140px/s
static uint32_t speedToInterval(uint8_t spd) {
  switch (spd) {
    case 1:  return 25;
    case 2:  return 17;
    case 3:  return 12;
    case 4:  return 10;
    default: return 7;
  }
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
  s.text[0] = '\0';
  if (!s.enabled || !DataManager::weatherReady) return;
  // Show all fetched cities separated by spaces
  for (int i = 0; i < DataManager::weatherCityCount; i++) {
    auto &w = DataManager::weatherCities[i];
    char buf[128];
    snprintf(buf, sizeof(buf), "%s %.0fF %s  H:%.0f L:%.0f    ",
      w.city, w.tempF, w.description, w.highF, w.lowF);
    strlcat(s.text, buf, sizeof(s.text));
  }
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
  bool anyShown = false;
  for (int i = 0; i < DataManager::scoreCount; i++) {
    auto &sc = DataManager::scores[i];

    // Apply same team/conf filter as the UI tab
    bool pass = true;
    bool isCFB = (!strcmp(sc.league,"CFB") || !strcmp(sc.league,"CBB"));
    if (isCFB && strlen(Storage::cfg.cfbConf) > 0) {
      pass = false;
      if (strstr(Storage::cfg.cfbConf, "Top 25")) {
        if (sc.awayRank > 0 || sc.homeRank > 0) pass = true;
      }
      if (!pass) {
        char cb[sizeof(Storage::cfg.cfbConf)];
        strlcpy(cb, Storage::cfg.cfbConf, sizeof(cb));
        char *tok = strtok(cb, ",");
        while (tok) {
          while (*tok==' ') tok++;
          if (strlen(tok) > 0 && strstr(sc.conference, tok)) { pass = true; break; }
          tok = strtok(nullptr, ",");
        }
      }
    } else if (!isCFB && strlen(Storage::cfg.teamFilter) > 0) {
      pass = false;
      char awayKey[24], homeKey[24];
      snprintf(awayKey, sizeof(awayKey), "%s:%s", sc.league, sc.awayTeam);
      snprintf(homeKey, sizeof(homeKey), "%s:%s", sc.league, sc.homeTeam);
      char fb[sizeof(Storage::cfg.teamFilter)];
      strlcpy(fb, Storage::cfg.teamFilter, sizeof(fb));
      char *tok = strtok(fb, ",");
      while (tok) {
        while (*tok==' ') tok++;
        if (strcasecmp(tok, awayKey)==0 || strcasecmp(tok, homeKey)==0) { pass=true; break; }
        tok = strtok(nullptr, ",");
      }
    }
    if (!pass) continue;

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
    anyShown = true;
  }
  if (!anyShown) strlcpy(s.text, "", sizeof(s.text));

  // Append motorsports/golf to the sports segment
  if (DataManager::racesReady && DataManager::raceCount > 0) {
    for (int i = 0; i < DataManager::raceCount; i++) {
      auto &r = DataManager::races[i];
      char buf[96];
      if (r.leader[0])
        snprintf(buf, sizeof(buf), "%s: %s - %s - Leader: %s %s    ",
          r.series, r.event, r.status, r.leader, r.detail);
      else
        snprintf(buf, sizeof(buf), "%s: %s - %s    ",
          r.series, r.event, r.status);
      strlcat(s.text, buf, sizeof(s.text));
    }
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

static void buildCalSegment() {
  TickerSegment &s = segments[SEG_CAL];
  s.type    = SEG_CAL;
  s.enabled = (Storage::cfg.tabMask & 0x08) != 0;
  s.text[0] = '\0';
  if (!s.enabled || !DataManager::calReady || DataManager::calCount == 0) return;
  for (int i = 0; i < DataManager::calCount; i++) {
    auto &e = DataManager::calEvents[i];
    if (e.dayOffset != 0) continue;  // ticker shows today only
    char buf[96];
    if (e.allDay)
      snprintf(buf, sizeof(buf), "Today: %s    ", e.title);
    else
      snprintf(buf, sizeof(buf), "Today %s: %s    ", e.timeStr, e.title);
    strlcat(s.text, buf, sizeof(s.text));
  }
}
void rebuildSegment(TickerSegmentType type) {
  switch (type) {
    case SEG_CLOCK:   buildClockSegment();   break;
    case SEG_WEATHER: buildWeatherSegment(); break;
    case SEG_SPORTS:  buildSportsSegment();  break;
    case SEG_STOCKS:  buildStocksSegment();  break;
    case SEG_CRYPTO:  buildCryptoSegment();  break;
    case SEG_CAL:     buildCalSegment();     break;
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
  bool first = true;
  for (int i = 0; i < SEG_COUNT; i++) {
    if (segments[i].enabled && strlen(segments[i].text) > 0) {
      if (!first) strlcat(fullString, "  -  ", sizeof(fullString));
      strlcat(fullString, segments[i].text, sizeof(fullString));
      first = false;
    }
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
  CHECK(calReady,     wasCalReady,     lastCalRebuild,     900000,             buildCalSegment)
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

  // Scroll animation — 1px per tick, interval controls speed
  if (_scrollTimer) { lv_timer_del(_scrollTimer); _scrollTimer = nullptr; }
  _scrollTimer = lv_timer_create(scrollTimerCb, speedToInterval(scrollState.speed), nullptr);
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
  // Adjust timer interval for new speed — no need to recreate timer
  if (_scrollTimer) lv_timer_set_period(_scrollTimer, speedToInterval(speed));
}

void setPaused(bool p) { paused = p; }
const char* getFullString() { return fullString; }
const char* getCurrentString() { return fullString; }

} // namespace TickerEngine
