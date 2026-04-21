#include "screen_manager.h"
#include "../ticker_engine.h"
#include "../screensaver.h"
#include "../themes/themes.h"
#include "../data/data_manager.h"
#include "../storage.h"
#include "../wifi_manager.h"
#include "../display.h"
#include "../../config.h"
#include <lvgl.h>
#include <time.h>

// ─────────────────────────────────────────────────────────────────────────────
// Screen objects (global to this file)
// ─────────────────────────────────────────────────────────────────────────────
static lv_obj_t *mainScreen    = nullptr;
static lv_obj_t *tabView       = nullptr;
static lv_obj_t *tickerCont    = nullptr;
static lv_obj_t *clockLabel    = nullptr;

// Persistent timers — stored so we can delete before recreating on dashboard rebuild
static lv_timer_t *clockTimer      = nullptr;
static lv_timer_t *dataUpdateTimer = nullptr;

// Tab content areas
static lv_obj_t *tabAll        = nullptr;
static lv_obj_t *tabSports     = nullptr;
static lv_obj_t *tabFin        = nullptr;
static lv_obj_t *tabWeather    = nullptr;
static lv_obj_t *tabCal        = nullptr;

static ThemePalette pal;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
// Shared static styles — initialized ONCE, reused forever. Never re-init.
// Using lv_obj_set_style_* inline on frequently-rebuilt widgets leaks style
// memory from LVGL's PSRAM heap. These shared styles have zero leak risk.
// ─────────────────────────────────────────────────────────────────────────────
static bool stylesInited = false;
static lv_style_t stTransparent, stNoBorder, stNoPad, stRadius8, stRadius12;
static lv_style_t stFont12, stFont14, stFont16, stFont22, stFont28;

static void initSharedStyles() {
  if (stylesInited) return;
  stylesInited = true;

  lv_style_init(&stTransparent);
  lv_style_set_bg_opa(&stTransparent, LV_OPA_TRANSP);

  lv_style_init(&stNoBorder);
  lv_style_set_border_width(&stNoBorder, 0);

  lv_style_init(&stNoPad);
  lv_style_set_pad_all(&stNoPad, 0);

  lv_style_init(&stRadius8);
  lv_style_set_radius(&stRadius8, 8);

  lv_style_init(&stRadius12);
  lv_style_set_radius(&stRadius12, 12);

  lv_style_init(&stFont12);
  lv_style_set_text_font(&stFont12, &lv_font_montserrat_12);

  lv_style_init(&stFont14);
  lv_style_set_text_font(&stFont14, &lv_font_montserrat_14);

  lv_style_init(&stFont16);
  lv_style_set_text_font(&stFont16, &lv_font_montserrat_16);

  lv_style_init(&stFont22);
  lv_style_set_text_font(&stFont22, &lv_font_montserrat_22);

  lv_style_init(&stFont28);
  lv_style_set_text_font(&stFont28, &lv_font_montserrat_28);
}



static lv_obj_t* makeCard(lv_obj_t *parent, int32_t w, int32_t h) {
  lv_obj_t *c = lv_obj_create(parent);
  lv_obj_set_size(c, w, h);
  lv_obj_set_style_bg_color(c, pal.bg2, 0);
  lv_obj_set_style_border_color(c, pal.border, 0);
  lv_obj_set_style_border_width(c, 1, 0);
  lv_obj_add_style(c, &stRadius12, 0);
  lv_obj_set_style_pad_all(c, 10, 0);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

// ─────────────────────────────────────────────────────────────────────────────
// Clock update timer
// ─────────────────────────────────────────────────────────────────────────────
static void clockTimerCb(lv_timer_t *timer) {
  if (!clockLabel) return;
  time_t now = time(nullptr);
  struct tm *t = localtime(&now);
  char buf[16];
  if (Storage::cfg.clock24h)
    snprintf(buf, sizeof(buf), "%02d:%02d", t->tm_hour, t->tm_min);
  else {
    int h = t->tm_hour % 12;
    if (!h) h = 12;
    snprintf(buf, sizeof(buf), "%d:%02d %s", h, t->tm_min, t->tm_hour < 12 ? "AM" : "PM");
  }
  lv_label_set_text(clockLabel, buf);
}


// ─────────────────────────────────────────────────────────────────────────────
// All tab — summary of every category
// ─────────────────────────────────────────────────────────────────────────────
static void buildAllTab(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, pal.bg, 0);
  lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_gap(parent, 4, 0);

  auto addRow = [&](const char *label, const char *value, lv_color_t col) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, TFT_WIDTH - 16, 26);
    lv_obj_set_style_bg_color(row, pal.bg2, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, pal.textMuted, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_t *val = lv_label_create(row);
    lv_label_set_text(val, value);
    lv_obj_set_style_text_color(val, col, 0);
    lv_obj_align(val, LV_ALIGN_RIGHT_MID, -4, 0);
  };

  // Sports rows — only live and final games (not scheduled), filtered same as sports tab
  if (!DataManager::scoresReady) {
    addRow("Sports", Storage::cfg.sportsLeagues ? "Loading..." : "No leagues set", pal.textMuted);
  } else {
    int shown = 0;
    for (int i = 0; i < DataManager::scoreCount && shown < 12; i++) {
      auto &s = DataManager::scores[i];
      // Skip scheduled games on the All tab — only show active/final
      bool isSched = !strstr(s.status,"Final") &&
                     (strstr(s.status,"PM") || strstr(s.status,"AM") ||
                      strchr(s.status,'/') != nullptr ||
                      (s.status[0]=='\0' && strcmp(s.homeScore,"0")==0 && strcmp(s.awayScore,"0")==0));
      if (isSched) continue;

      // Apply same team filter as sports tab
      bool isCFB = (!strcmp(s.league,"CFB") || !strcmp(s.league,"CBB"));
      bool pass = true;
      if (isCFB && strlen(Storage::cfg.cfbConf) > 0) {
        pass = false;
        if (strstr(Storage::cfg.cfbConf,"Top 25") && (s.awayRank>0 || s.homeRank>0)) pass = true;
        if (!pass) {
          char cb[sizeof(Storage::cfg.cfbConf)];
          strlcpy(cb, Storage::cfg.cfbConf, sizeof(cb));
          char *tok = strtok(cb, ",");
          while (tok) { while(*tok==' ')tok++; if(*tok&&strstr(s.conference,tok)){pass=true;break;} tok=strtok(nullptr,","); }
        }
      } else if (!isCFB && strlen(Storage::cfg.teamFilter) > 0) {
        pass = false;
        char awayKey[24], homeKey[24];
        snprintf(awayKey,sizeof(awayKey),"%s:%s",s.league,s.awayTeam);
        snprintf(homeKey,sizeof(homeKey),"%s:%s",s.league,s.homeTeam);
        char fb[sizeof(Storage::cfg.teamFilter)];
        strlcpy(fb, Storage::cfg.teamFilter, sizeof(fb));
        char *tok = strtok(fb, ",");
        while (tok) { while(*tok==' ')tok++; if(strcasecmp(tok,awayKey)==0||strcasecmp(tok,homeKey)==0){pass=true;break;} tok=strtok(nullptr,","); }
      }
      if (!pass) continue;

      char val[48];
      snprintf(val, sizeof(val), "%s %s-%s %s %s",
        s.awayTeam, s.awayScore, s.homeScore, s.homeTeam, s.status);
      addRow(s.league, val, pal.text);
      shown++;
    }
    if (shown == 0 && DataManager::scoreCount > 0)
      addRow("Sports", "No live games", pal.textMuted);
    else if (DataManager::scoreCount == 0)
      addRow("Sports", "No games today", pal.textMuted);
  }

  // Racing — just a prompt to see Sports tab (too many drivers to list inline)
  if (DataManager::racesReady && DataManager::raceCount > 0) {
    // Build a compact summary e.g. "NASCAR  F1  PGA"
    char series[48] = {};
    for (int i = 0; i < DataManager::raceCount; i++) {
      if (i > 0) strlcat(series, "  ", sizeof(series));
      strlcat(series, DataManager::races[i].series, sizeof(series));
    }
    addRow(series, "See Sports tab", pal.textMuted);
  }

  // Stock rows — second
  if (DataManager::stocksReady && DataManager::stockCount > 0) {
    for (int i = 0; i < DataManager::stockCount; i++) {
      auto &s = DataManager::stocks[i];
      char val[24]; snprintf(val, sizeof(val), "$%.2f  %+.2f%%", s.price, s.changePercent);
      addRow(s.symbol, val, s.changePercent >= 0 ? pal.positive : pal.negative);
    }
  } else if (!DataManager::stocksReady && strlen(Storage::cfg.stocks)) {
    addRow("Finance", "Loading...", pal.textMuted);
  }

  // Crypto rows — third
  if (DataManager::cryptoReady && DataManager::cryptoCount > 0) {
    for (int i = 0; i < DataManager::cryptoCount; i++) {
      auto &c = DataManager::cryptos[i];
      char val[24];
      if (c.priceUSD >= 1000) snprintf(val, sizeof(val), "$%.0f  %+.2f%%", c.priceUSD, c.change24h);
      else                    snprintf(val, sizeof(val), "$%.3f  %+.2f%%", c.priceUSD, c.change24h);
      addRow(c.symbol, val, c.change24h >= 0 ? pal.positive : pal.negative);
    }
  }

  // Weather rows — one per city
  if (DataManager::weatherReady) {
    for (int i = 0; i < DataManager::weatherCityCount; i++) {
      auto &w = DataManager::weatherCities[i];
      char buf[48];
      snprintf(buf, sizeof(buf), "%.0fF  %s", w.tempF, w.description);
      addRow(w.city, buf, pal.text);
    }
  } else {
    addRow("Weather", "Loading...", pal.textMuted);
  }

  // Calendar rows — today only (dayOffset == 0)
  if (Storage::cfg.tabMask & 0x08) {
    if (DataManager::calReady) {
      int shown = 0;
      for (int i = 0; i < DataManager::calCount; i++) {
        auto &e = DataManager::calEvents[i];
        if (e.dayOffset != 0) continue;
        char timeLabel[20];
        if (e.allDay) strlcpy(timeLabel, "Today", sizeof(timeLabel));
        else snprintf(timeLabel, sizeof(timeLabel), "@ %s", e.timeStr);
        addRow(timeLabel, e.title, pal.accent);
        shown++;
      }
      if (shown == 0 && strlen(Storage::cfg.icalUrl))
        addRow("Calendar", "No events today", pal.textMuted);
    } else if (!DataManager::calReady && strlen(Storage::cfg.icalUrl)) {
      addRow("Calendar", "Loading...", pal.textMuted);
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Build sports tab — scoreboard list with team color badges, league sections
// ─────────────────────────────────────────────────────────────────────────────
static void buildSportsTab(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, pal.bg, 0);
  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

  if (!DataManager::scoresReady) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, Storage::cfg.sportsLeagues == 0
      ? "No sports leagues selected.\nOpen settings to enable leagues."
      : "Loading scores...");
    lv_obj_add_style(lbl, &stFont16, 0);
    lv_obj_set_style_text_color(lbl, pal.textMuted, 0);
    lv_obj_center(lbl);
    return;
  }
  if (DataManager::scoreCount == 0) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "No games today");
    lv_obj_add_style(lbl, &stFont16, 0);
    lv_obj_set_style_text_color(lbl, pal.textMuted, 0);
    lv_obj_center(lbl);
    return;
  }

  // Full-height scrollable column
  lv_obj_t *scroll = lv_obj_create(parent);
  lv_obj_set_size(scroll, TFT_WIDTH - 20, lv_obj_get_height(parent));
  lv_obj_set_pos(scroll, 0, 0);
  lv_obj_add_style(scroll, &stTransparent, 0);
  lv_obj_add_style(scroll, &stNoBorder, 0);
  lv_obj_add_style(scroll, &stNoPad, 0);
  lv_obj_set_layout(scroll, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(scroll, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_gap(scroll, 0, 0);
  lv_obj_add_flag(scroll, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_AUTO);

  // Colored badge — uses lv_obj_add_style for font/text (shared, no leak)
  // Only bg color is inline since it's unique per team
  auto makeBadge = [&](lv_obj_t *par, const char *abbr, uint32_t rgb) -> lv_obj_t* {
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >>  8) & 0xFF;
    uint8_t b =  rgb        & 0xFF;
    if ((r + g + b) < 120) { r = r/2+60; g = g/2+60; b = b/2+60; }
    lv_obj_t *badge = lv_obj_create(par);
    lv_obj_set_size(badge, 52, 34);
    lv_obj_set_style_bg_color(badge, lv_color_make(r, g, b), 0);
    lv_obj_add_style(badge, &stNoBorder, 0);
    lv_obj_add_style(badge, &stRadius8, 0);
    lv_obj_add_style(badge, &stNoPad, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *lbl = lv_label_create(badge);
    lv_label_set_text(lbl, abbr);
    lv_obj_add_style(lbl, &stFont14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);
    return badge;
  };

  const char *lastLeague = "";
  const int ROW_W = TFT_WIDTH - 20;

  // Filter helper — checks if a game should display given current filter settings.
  // teamFilter stores "LEAGUE:ABBR" pairs e.g. "MLB:PHI,NFL:PHI,NBA:LAL"
  // cfbConf stores conference IDs e.g. "SEC,Big Ten,Top 25"
  // Empty filter = show all games.
  auto gamePassesFilter = [](const ScoreEntry &sc) -> bool {
    bool isCFB = (!strcmp(sc.league,"CFB") || !strcmp(sc.league,"CBB"));

    if (isCFB) {
      if (strlen(Storage::cfg.cfbConf) == 0) return true; // no conf filter = show all
      if (strstr(Storage::cfg.cfbConf, "Top 25")) {
        if (sc.awayRank > 0 && sc.awayRank <= 25) return true;
        if (sc.homeRank > 0 && sc.homeRank <= 25) return true;
      }
      char confBuf[sizeof(Storage::cfg.cfbConf)];
      strlcpy(confBuf, Storage::cfg.cfbConf, sizeof(confBuf));
      char *tok = strtok(confBuf, ",");
      while (tok) {
        while (*tok == ' ') tok++;
        if (*tok && strstr(sc.conference, tok)) return true;
        tok = strtok(nullptr, ",");
      }
      return false;
    }

    // Pro leagues — empty filter means show all
    if (strlen(Storage::cfg.teamFilter) == 0) return true;

    // Build "LEAGUE:AWAY" and "LEAGUE:HOME" to match against stored tokens
    char awayKey[24], homeKey[24];
    snprintf(awayKey, sizeof(awayKey), "%s:%s", sc.league, sc.awayTeam);
    snprintf(homeKey, sizeof(homeKey), "%s:%s", sc.league, sc.homeTeam);

    char filterBuf[sizeof(Storage::cfg.teamFilter)];
    strlcpy(filterBuf, Storage::cfg.teamFilter, sizeof(filterBuf));
    char *tok = strtok(filterBuf, ",");
    while (tok) {
      while (*tok == ' ') tok++;
      if (strcasecmp(tok, awayKey) == 0) return true;
      if (strcasecmp(tok, homeKey) == 0) return true;
      tok = strtok(nullptr, ",");
    }
    return false;
  };

  // Pre-check: if filter is active and NO games pass, show a helpful message
  bool anyVisible = false;
  bool filterActive = strlen(Storage::cfg.teamFilter) > 0 || strlen(Storage::cfg.cfbConf) > 0;
  for (int i = 0; i < DataManager::scoreCount; i++)
    if (gamePassesFilter(DataManager::scores[i])) { anyVisible = true; break; }

  if (filterActive && !anyVisible) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "No games today\nfor your teams.\nEdit filter in Settings.");
    lv_obj_add_style(lbl, &stFont14, 0);
    lv_obj_set_style_text_color(lbl, pal.textMuted, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(lbl);
    return;
  }

  for (int i = 0; i < DataManager::scoreCount; i++) {
    auto &s = DataManager::scores[i];
    if (!gamePassesFilter(s)) continue;

    bool isFinal = strstr(s.status, "Final") != nullptr;
    bool isSched = !isFinal && (
      strstr(s.status, "PM") || strstr(s.status, "AM") ||
      (strchr(s.status, '/') != nullptr) ||
      (s.status[0] == '\0' && strcmp(s.homeScore,"0")==0 && strcmp(s.awayScore,"0")==0)
    );
    bool isLive  = !isFinal && !isSched;

    // League section header — emitted once per league group
    if (strcmp(s.league, lastLeague) != 0) {
      lastLeague = s.league;

      // Full league name lookup
      const char *fullName = s.league;
      if      (!strcmp(s.league,"NFL")) fullName = "NFL - National Football League";
      else if (!strcmp(s.league,"NBA")) fullName = "NBA - National Basketball Assoc.";
      else if (!strcmp(s.league,"NHL")) fullName = "NHL - National Hockey League";
      else if (!strcmp(s.league,"MLB")) fullName = "MLB - Major League Baseball";
      else if (!strcmp(s.league,"CFB")) fullName = "CFB - College Football";
      else if (!strcmp(s.league,"CBB")) fullName = "CBB - College Basketball";
      else if (!strcmp(s.league,"MLS")) fullName = "MLS - Major League Soccer";
      else if (!strcmp(s.league,"EPL")) fullName = "EPL - English Premier League";

      lv_obj_t *hdr = lv_obj_create(scroll);
      lv_obj_set_size(hdr, ROW_W, 26);
      lv_obj_set_style_bg_color(hdr, pal.accent, 0);
      lv_obj_set_style_bg_opa(hdr, LV_OPA_20, 0);
      lv_obj_add_style(hdr, &stNoBorder, 0);
      lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_LEFT, 0);
      lv_obj_set_style_border_color(hdr, pal.accent, 0);
      lv_obj_set_style_border_width(hdr, 3, LV_STATE_DEFAULT);
      lv_obj_set_style_radius(hdr, 0, 0);
      lv_obj_set_style_pad_left(hdr, 10, 0);
      lv_obj_set_style_pad_right(hdr, 0, 0);
      lv_obj_set_style_pad_ver(hdr, 0, 0);
      lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_t *hdrLbl = lv_label_create(hdr);
      lv_label_set_text(hdrLbl, fullName);
      lv_obj_add_style(hdrLbl, &stFont12, 0);
      lv_obj_set_style_text_color(hdrLbl, pal.accent, 0);
      lv_obj_align(hdrLbl, LV_ALIGN_LEFT_MID, 0, 0);
    }

    // Game row
    lv_obj_t *row = lv_obj_create(scroll);
    lv_obj_set_size(row, ROW_W, 56);
    lv_obj_set_style_bg_color(row, pal.bg2, 0);
    lv_obj_add_style(row, &stNoBorder, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, pal.border, 0);
    lv_obj_set_style_border_width(row, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_add_style(row, &stNoPad, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    // Away badge — left side
    lv_obj_t *awayBadge = makeBadge(row, s.awayTeam, s.awayColor);
    lv_obj_set_pos(awayBadge, 8, 11);

    // Away rank (CFB/CBB only)
    if (s.awayRank > 0) {
      char rankBuf[6]; snprintf(rankBuf, sizeof(rankBuf), "#%d", s.awayRank);
      lv_obj_t *rnk = lv_label_create(row);
      lv_label_set_text(rnk, rankBuf);
      lv_obj_add_style(rnk, &stFont12, 0);
      lv_obj_set_style_text_color(rnk, pal.accentAlt, 0);
      lv_obj_set_pos(rnk, 8, 1);
    }

    // Away score
    lv_obj_t *awayScLbl = lv_label_create(row);
    lv_label_set_text(awayScLbl, isSched ? "  " : s.awayScore);
    lv_obj_add_style(awayScLbl, &stFont22, 0);
    lv_obj_set_style_text_color(awayScLbl, pal.text, 0);
    lv_obj_set_pos(awayScLbl, 68, 14);

    // Center status
    lv_obj_t *midLbl = lv_label_create(row);
    lv_label_set_text(midLbl, isSched ? s.status : (isFinal ? "Final" : s.status));
    lv_obj_add_style(midLbl, &stFont12, 0);
    lv_obj_set_style_text_color(midLbl,
      isFinal ? pal.textMuted : (isLive ? pal.positive : pal.accent), 0);
    lv_obj_align(midLbl, LV_ALIGN_CENTER, 0, 0);

    // Home score
    lv_obj_t *homeScLbl = lv_label_create(row);
    lv_label_set_text(homeScLbl, isSched ? "  " : s.homeScore);
    lv_obj_add_style(homeScLbl, &stFont22, 0);
    lv_obj_set_style_text_color(homeScLbl, pal.text, 0);
    lv_obj_set_pos(homeScLbl, ROW_W - 68 - 30, 14);

    // Home badge — right side
    lv_obj_t *homeBadge = makeBadge(row, s.homeTeam, s.homeColor);
    lv_obj_set_pos(homeBadge, ROW_W - 60, 11);

    // Home rank (CFB/CBB only)
    if (s.homeRank > 0) {
      char rankBuf[6]; snprintf(rankBuf, sizeof(rankBuf), "#%d", s.homeRank);
      lv_obj_t *rnk = lv_label_create(row);
      lv_label_set_text(rnk, rankBuf);
      lv_obj_add_style(rnk, &stFont12, 0);
      lv_obj_set_style_text_color(rnk, pal.accentAlt, 0);
      lv_obj_set_pos(rnk, ROW_W - 60, 1);
    }

    // Green live dot
    if (isLive) {
      lv_obj_t *dot = lv_obj_create(row);
      lv_obj_set_size(dot, 6, 6);
      lv_obj_set_style_bg_color(dot, pal.positive, 0);
      lv_obj_set_style_radius(dot, 3, 0);
      lv_obj_add_style(dot, &stNoBorder, 0);
      lv_obj_set_pos(dot, 2, 2);
    }
  }

  // ── Motorsports + Golf rows ───────────────────────────────────────────────
  if (DataManager::racesReady && DataManager::raceCount > 0) {
    for (int i = 0; i < DataManager::raceCount; i++) {
      auto &r = DataManager::races[i];
      bool offSeason = (strstr(r.event, "No active") != nullptr);

      // Section header
      lv_obj_t *hdr = lv_obj_create(scroll);
      lv_obj_set_size(hdr, ROW_W, 24);
      lv_obj_set_style_bg_color(hdr, pal.accent, 0);
      lv_obj_set_style_bg_opa(hdr, LV_OPA_20, 0);
      lv_obj_add_style(hdr, &stNoBorder, 0);
      lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_LEFT, 0);
      lv_obj_set_style_border_color(hdr, pal.accent, 0);
      lv_obj_set_style_border_width(hdr, 3, LV_STATE_DEFAULT);
      lv_obj_set_style_radius(hdr, 0, 0);
      lv_obj_set_style_pad_left(hdr, 10, 0);
      lv_obj_set_style_pad_ver(hdr, 0, 0);
      lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_t *hdrLbl = lv_label_create(hdr);
      lv_label_set_text(hdrLbl, r.series);
      lv_obj_add_style(hdrLbl, &stFont12, 0);
      lv_obj_set_style_text_color(hdrLbl, pal.accent, 0);
      lv_obj_align(hdrLbl, LV_ALIGN_LEFT_MID, 0, 0);

      // Event row — compact: event name left, status right
      lv_obj_t *row = lv_obj_create(scroll);
      lv_obj_set_size(row, ROW_W, offSeason ? 32 : 48);
      lv_obj_set_style_bg_color(row, pal.bg2, 0);
      lv_obj_add_style(row, &stNoBorder, 0);
      lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
      lv_obj_set_style_border_color(row, pal.border, 0);
      lv_obj_set_style_border_width(row, 1, LV_STATE_DEFAULT);
      lv_obj_set_style_radius(row, 0, 0);
      lv_obj_set_style_pad_left(row, 10, 0);
      lv_obj_set_style_pad_right(row, 8, 0);
      lv_obj_set_style_pad_ver(row, 0, 0);
      lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

      if (offSeason) {
        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, "Off season");
        lv_obj_add_style(lbl, &stFont12, 0);
        lv_obj_set_style_text_color(lbl, pal.textMuted, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
      } else {
        lv_obj_t *evtLbl = lv_label_create(row);
        lv_label_set_text(evtLbl, r.event);
        lv_obj_add_style(evtLbl, &stFont14, 0);
        lv_obj_set_style_text_color(evtLbl, pal.text, 0);
        lv_obj_set_width(evtLbl, ROW_W - 110);
        lv_label_set_long_mode(evtLbl, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(evtLbl, 0, 6);

        lv_obj_t *stLbl = lv_label_create(row);
        lv_label_set_text(stLbl, r.status);
        lv_obj_add_style(stLbl, &stFont12, 0);
        lv_obj_set_style_text_color(stLbl, pal.accent, 0);
        lv_obj_set_pos(stLbl, 0, 26);

        if (r.leader[0]) {
          lv_obj_t *ldLbl = lv_label_create(row);
          char ldBuf[48]; snprintf(ldBuf, sizeof(ldBuf), "%s %s", r.leader, r.detail);
          lv_label_set_text(ldLbl, ldBuf);
          lv_obj_add_style(ldLbl, &stFont12, 0);
          lv_obj_set_style_text_color(ldLbl, pal.textMuted, 0);
          lv_obj_set_pos(ldLbl, ROW_W - 108, 10);
          lv_obj_set_width(ldLbl, 100);
          lv_obj_set_style_text_align(ldLbl, LV_TEXT_ALIGN_RIGHT, 0);
          lv_label_set_long_mode(ldLbl, LV_LABEL_LONG_DOT);
        }
      }
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Build stocks/crypto tab
// ─────────────────────────────────────────────────────────────────────────────
static void buildFinanceTab(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, pal.bg, 0);
  lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(parent, 6, 0);
  // Allow vertical scroll if user has more than 6 items
  lv_obj_set_scroll_dir(parent, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_AUTO);

  bool hasAnything = false;

  if (DataManager::stocksReady && DataManager::stockCount > 0) {
    hasAnything = true;

    // 3 per row, 2 rows = 6 cards.
    // Usable height = 168px tab area - 16px pad = 152px. 2 rows + 6px gap = 152 → card_h = 73px, use 70.
    for (int i = 0; i < DataManager::stockCount; i++) {
      auto &s = DataManager::stocks[i];
      lv_obj_t *card = makeCard(parent, 140, 70);

      lv_obj_t *sym = lv_label_create(card);
      lv_label_set_text(sym, s.symbol);
      lv_obj_add_style(sym, &stFont14, 0);
      lv_obj_set_style_text_color(sym, s.marketClosed ? pal.textMuted : pal.accent, 0);
      lv_obj_set_pos(sym, 0, 0);

      char price[20];
      if (s.marketClosed) snprintf(price, sizeof(price), "$%.2f*", s.price);
      else snprintf(price, sizeof(price), "$%.2f", s.price);
      lv_obj_t *priceLbl = lv_label_create(card);
      lv_label_set_text(priceLbl, price);
      lv_obj_set_style_text_font(priceLbl, &lv_font_montserrat_16, 0);
      lv_obj_set_style_text_color(priceLbl, pal.text, 0);
      lv_obj_set_pos(priceLbl, 0, 17);

      char chg[20]; snprintf(chg, sizeof(chg), "%+.1f%%", s.changePercent);
      lv_obj_t *chgLbl = lv_label_create(card);
      lv_label_set_text(chgLbl, chg);
      lv_obj_add_style(chgLbl, &stFont12, 0);
      lv_obj_set_style_text_color(chgLbl,
        s.changePercent > 0 ? pal.positive : (s.changePercent < 0 ? pal.negative : pal.textMuted), 0);
      lv_obj_set_pos(chgLbl, 0, 38);
    }
  } else if (!DataManager::stocksReady && strlen(Storage::cfg.stocks)) {
    hasAnything = true;
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "Loading stocks...");
    lv_obj_set_style_text_color(lbl, pal.textMuted, 0);
  }

  // ── Crypto ────────────────────────────────────────────────────────────
  if (DataManager::cryptoReady && DataManager::cryptoCount > 0) {
    hasAnything = true;
    for (int i = 0; i < DataManager::cryptoCount; i++) {
      auto &c = DataManager::cryptos[i];
      lv_obj_t *card = makeCard(parent, 140, 70);

      lv_obj_t *sym = lv_label_create(card);
      lv_label_set_text(sym, c.symbol);
      lv_obj_add_style(sym, &stFont14, 0);
      lv_obj_set_style_text_color(sym, pal.accent, 0);
      lv_obj_set_pos(sym, 0, 0);

      char price[20];
      if (c.priceUSD >= 1000.0f) snprintf(price, sizeof(price), "$%.0f", c.priceUSD);
      else snprintf(price, sizeof(price), "$%.3f", c.priceUSD);
      lv_obj_t *priceLbl = lv_label_create(card);
      lv_label_set_text(priceLbl, price);
      lv_obj_set_style_text_font(priceLbl, &lv_font_montserrat_16, 0);
      lv_obj_set_style_text_color(priceLbl, pal.text, 0);
      lv_obj_set_pos(priceLbl, 0, 17);

      char chg[20]; snprintf(chg, sizeof(chg), "%+.1f%%", c.change24h);
      lv_obj_t *chgLbl = lv_label_create(card);
      lv_label_set_text(chgLbl, chg);
      lv_obj_add_style(chgLbl, &stFont12, 0);
      lv_obj_set_style_text_color(chgLbl,
        c.change24h > 0 ? pal.positive : (c.change24h < 0 ? pal.negative : pal.textMuted), 0);
      lv_obj_set_pos(chgLbl, 0, 38);
    }
  }

  if (!hasAnything) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, strlen(Storage::cfg.stocks)
      ? "Market closed or no data"
      : "No stocks configured.\nOpen settings to add tickers.");
    lv_obj_add_style(lbl, &stFont16, 0);
    lv_obj_set_style_text_color(lbl, pal.textMuted, 0);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Build weather tab
// ─────────────────────────────────────────────────────────────────────────────
static const char* wmoDescShort(int c) {
  if(c==0) return "Clear"; if(c<=2) return "P.Cloudy"; if(c==3) return "Cloudy";
  if(c<=49) return "Foggy"; if(c<=59) return "Drizzle"; if(c<=69) return "Rain";
  if(c<=79) return "Snow";  if(c<=82) return "Showers"; if(c<=99) return "T-Storm";
  return "";
}

static void buildCalendarTab(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, pal.bg, 0);

  if (!DataManager::calReady) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, !strlen(Storage::cfg.icalUrl)
      ? "No calendar URL set.\nAdd your iCal URL in settings."
      : "Loading calendar...");
    lv_obj_set_style_text_color(lbl, pal.textMuted, 0);
    lv_obj_add_style(lbl, &stFont14, 0);
    lv_obj_center(lbl);
    return;
  }

  if (DataManager::calCount == 0) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "No events this week");
    lv_obj_add_style(lbl, &stFont14, 0);
    lv_obj_set_style_text_color(lbl, pal.textMuted, 0);
    lv_obj_center(lbl);
    return;
  }

  static const char *dayNames[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  static const char *months[]   = {"Jan","Feb","Mar","Apr","May","Jun",
                                    "Jul","Aug","Sep","Oct","Nov","Dec"};
  time_t now = time(nullptr);

  // Full-height scrollable container
  lv_obj_t *scroll = lv_obj_create(parent);
  lv_obj_set_size(scroll, TFT_WIDTH - 20, lv_obj_get_height(parent));
  lv_obj_set_pos(scroll, 0, 0);
  lv_obj_add_style(scroll, &stTransparent, 0);
  lv_obj_add_style(scroll, &stNoBorder, 0);
  lv_obj_add_style(scroll, &stNoPad, 0);
  lv_obj_set_layout(scroll, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(scroll, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_gap(scroll, 3, 0);
  lv_obj_add_flag(scroll, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_AUTO);

  int lastDay = -1;

  for (int i = 0; i < DataManager::calCount; i++) {
    auto &e = DataManager::calEvents[i];

    // Day section header when day changes
    if (e.dayOffset != lastDay) {
      lastDay = e.dayOffset;
      time_t dayT = now + (time_t)e.dayOffset * 86400;
      struct tm *lt = localtime(&dayT);
      char dayHdr[32];
      if (e.dayOffset == 0)
        snprintf(dayHdr, sizeof(dayHdr), "Today - %s %d",
          months[lt->tm_mon], lt->tm_mday);
      else if (e.dayOffset == 1)
        snprintf(dayHdr, sizeof(dayHdr), "Tomorrow - %s %d",
          months[lt->tm_mon], lt->tm_mday);
      else
        snprintf(dayHdr, sizeof(dayHdr), "%s  %s %d",
          dayNames[lt->tm_wday], months[lt->tm_mon], lt->tm_mday);

      lv_obj_t *hdr = lv_obj_create(scroll);
      lv_obj_set_size(hdr, TFT_WIDTH - 20, 22);
      lv_obj_set_style_bg_color(hdr, pal.accent, 0);
      lv_obj_set_style_bg_opa(hdr, LV_OPA_20, 0);
      lv_obj_add_style(hdr, &stNoBorder, 0);
      lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_LEFT, 0);
      lv_obj_set_style_border_color(hdr, pal.accent, 0);
      lv_obj_set_style_border_width(hdr, 3, LV_STATE_DEFAULT);
      lv_obj_set_style_radius(hdr, 0, 0);
      lv_obj_set_style_pad_left(hdr, 8, 0);
      lv_obj_set_style_pad_ver(hdr, 0, 0);
      lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_t *hdrLbl = lv_label_create(hdr);
      lv_label_set_text(hdrLbl, dayHdr);
      lv_obj_add_style(hdrLbl, &stFont12, 0);
      lv_obj_set_style_text_color(hdrLbl, pal.accent, 0);
      lv_obj_align(hdrLbl, LV_ALIGN_LEFT_MID, 0, 0);
    }

    // Event row
    lv_obj_t *row = lv_obj_create(scroll);
    lv_obj_set_size(row, TFT_WIDTH - 20, 46);
    lv_obj_set_style_bg_color(row, pal.bg2, 0);
    lv_obj_add_style(row, &stNoBorder, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_color(row, e.dayOffset == 0 ? pal.accent : pal.textMuted, 0);
    lv_obj_set_style_border_width(row, 3, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(row, 4, 0);
    lv_obj_set_style_pad_left(row, 10, 0);
    lv_obj_set_style_pad_right(row, 6, 0);
    lv_obj_set_style_pad_ver(row, 4, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *timeLbl = lv_label_create(row);
    lv_label_set_text(timeLbl, e.timeStr);
    lv_obj_add_style(timeLbl, &stFont12, 0);
    lv_obj_set_style_text_color(timeLbl, e.dayOffset == 0 ? pal.accent : pal.textMuted, 0);
    lv_obj_set_pos(timeLbl, 0, 0);

    lv_obj_t *titleLbl = lv_label_create(row);
    lv_label_set_text(titleLbl, e.title);
    lv_obj_add_style(titleLbl, &stFont14, 0);
    lv_obj_set_style_text_color(titleLbl, pal.text, 0);
    lv_obj_set_width(titleLbl, TFT_WIDTH - 40);
    lv_label_set_long_mode(titleLbl, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(titleLbl, 0, 15);
  }
}

static void buildWeatherPanel(lv_obj_t *parent, const WeatherData &w, bool compact, int panelW) {
  const int RW = panelW - (compact ? 12 : 16); // usable width after padding
  int y = compact ? 0 : 2;

  static const char *wdayNames[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  time_t now = time(nullptr);

  lv_obj_t *city = lv_label_create(parent);
  lv_label_set_text(city, w.city);
  lv_obj_set_style_text_color(city, pal.accent, 0);
  lv_obj_add_style(city, &stFont16, 0);
  lv_obj_set_pos(city, 0, y);

  lv_obj_t *condLbl = lv_label_create(parent);
  lv_label_set_text(condLbl, w.description);
  lv_obj_set_style_text_color(condLbl, pal.textMuted, 0);
  lv_obj_add_style(condLbl, &stFont14, 0);
  lv_obj_set_width(condLbl, 180);
  lv_obj_set_style_text_align(condLbl, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(condLbl, RW - 180, y + 2);
  y += 22;

  char temp[12]; snprintf(temp, sizeof(temp), "%.0f\xB0""F", w.tempF);
  lv_obj_t *tempLbl = lv_label_create(parent);
  lv_label_set_text(tempLbl, temp);
  lv_obj_add_style(tempLbl, &stFont28, 0);
  lv_obj_set_style_text_color(tempLbl, pal.text, 0);
  lv_obj_set_pos(tempLbl, 0, y);

  char feels[20]; snprintf(feels, sizeof(feels), "Feels %.0f\xB0""F", w.feelsLike);
  lv_obj_t *feelsLbl = lv_label_create(parent);
  lv_label_set_text(feelsLbl, feels);
  lv_obj_set_style_text_color(feelsLbl, pal.textMuted, 0);
  lv_obj_add_style(feelsLbl, &stFont14, 0);
  lv_obj_set_pos(feelsLbl, RW - 100, y);

  char hl[20]; snprintf(hl, sizeof(hl), "H:%.0f\xB0  L:%.0f\xB0", w.highF, w.lowF);
  lv_obj_t *hlLbl = lv_label_create(parent);
  lv_label_set_text(hlLbl, hl);
  lv_obj_set_style_text_color(hlLbl, pal.text, 0);
  lv_obj_add_style(hlLbl, &stFont14, 0);
  lv_obj_set_pos(hlLbl, RW - 100, y + 18);
  y += 36;

  char hw[48]; snprintf(hw, sizeof(hw), "Humidity %d%%   Wind %.0f mph", w.humidity, w.windMph);
  lv_obj_t *hwLbl = lv_label_create(parent);
  lv_label_set_text(hwLbl, hw);
  lv_obj_set_style_text_color(hwLbl, pal.textMuted, 0);
  lv_obj_add_style(hwLbl, &stFont12, 0);
  lv_obj_set_pos(hwLbl, 0, y); y += 18;

  // 3-day forecast cards
  int cardW = compact ? 90 : 105;
  int cardH = compact ? 58 : 66;
  int gap   = compact ? 6  : 8;
  int cx = (RW - (3 * cardW + 2 * gap)) / 2;
  for (int i = 0; i < 3; i++) {
    time_t future = now + (i + 1) * 86400;
    struct tm *ft = localtime(&future);
    lv_obj_t *card = makeCard(parent, cardW, cardH);
    lv_obj_set_pos(card, cx, y);
    cx += cardW + gap;

    lv_obj_t *dayLbl = lv_label_create(card);
    lv_label_set_text(dayLbl, wdayNames[ft->tm_wday]);
    lv_obj_add_style(dayLbl, &stFont12, 0);
    lv_obj_set_style_text_color(dayLbl, pal.accent, 0);
    lv_obj_set_pos(dayLbl, 4, 2);

    char fc[20]; snprintf(fc, sizeof(fc), "%.0f / %.0f", w.forecast[i].highF, w.forecast[i].lowF);
    lv_obj_t *fcLbl = lv_label_create(card);
    lv_label_set_text(fcLbl, fc);
    lv_obj_add_style(fcLbl, &stFont14, 0);
    lv_obj_set_style_text_color(fcLbl, pal.text, 0);
    lv_obj_set_pos(fcLbl, 4, compact ? 16 : 18);

    lv_obj_t *fcCond = lv_label_create(card);
    lv_label_set_text(fcCond, wmoDescShort(w.forecast[i].code));
    lv_obj_add_style(fcCond, &stFont12, 0);
    lv_obj_set_style_text_color(fcCond, pal.textMuted, 0);
    lv_obj_set_pos(fcCond, 4, compact ? 32 : 38);
    lv_obj_set_width(fcCond, cardW - 8);
    lv_label_set_long_mode(fcCond, LV_LABEL_LONG_CLIP);
  }
}

static void buildWeatherTab(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, pal.bg, 0);
  lv_obj_set_scroll_dir(parent, LV_DIR_NONE);
  lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);

  if (!DataManager::weatherReady) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, Storage::cfg.lat == 0.0f
      ? "No location set.\nOpen settings to enter your city."
      : "Loading weather...");
    lv_obj_set_style_text_color(lbl, pal.textMuted, 0);
    lv_obj_add_style(lbl, &stFont16, 0);
    lv_obj_center(lbl);
    return;
  }

  uint8_t n = DataManager::weatherCityCount;
  if (n == 0) n = 1; // fallback

  if (n == 1) {
    buildWeatherPanel(parent, DataManager::weatherCities[0], false, TFT_WIDTH);
  } else {
    // Multiple cities — scrollable list of compact panels separated by dividers
    lv_obj_set_scroll_dir(parent, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_AUTO);

    const int PW = TFT_WIDTH - 16; // panel width = tab usable width
    const int PANEL_H = 148;       // height per city panel (compact)
    int py = 0;

    for (int i = 0; i < n; i++) {
      // Panel container for each city
      lv_obj_t *panel = lv_obj_create(parent);
      lv_obj_set_size(panel, PW, PANEL_H);
      lv_obj_set_pos(panel, 0, py);
      lv_obj_set_style_bg_color(panel, i == 0 ? pal.bg : pal.bg2, 0);
      lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(panel, 0, 0);
      if (i > 0) {
        lv_obj_set_style_border_side(panel, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_color(panel, pal.border, 0);
        lv_obj_set_style_border_width(panel, 1, LV_STATE_DEFAULT);
      }
      lv_obj_set_style_pad_all(panel, 6, 0);
      lv_obj_set_style_radius(panel, 0, 0);
      lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

      buildWeatherPanel(panel, DataManager::weatherCities[i], true, PW);
      py += PANEL_H;
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Settings overlay (gear cog tap)
// ─────────────────────────────────────────────────────────────────────────────
// Track original theme when settings opens — used to detect changes in closeSettings
static uint8_t sSettingsOrigTheme = 0xFF;

static void closeSettings(lv_event_t *e) {
  lv_obj_t *overlay = (lv_obj_t*)lv_event_get_user_data(e);
  TickerEngine::setPaused(false);
  bool themeChanged = (Storage::cfg.theme != sSettingsOrigTheme);
  lv_obj_del(overlay);
  Storage::save();
  if (themeChanged) {
    lv_timer_create([](lv_timer_t *t){ lv_timer_del(t); ESP.restart(); }, 200, nullptr);
  } else {
    lv_timer_create([](lv_timer_t *t){
      lv_timer_del(t);
      ScreenManager::showDashboard();
    }, 50, nullptr);
  }
}

void ScreenManager::showSettings() {
  sSettingsOrigTheme = Storage::cfg.theme;
  TickerEngine::setPaused(true);

  lv_obj_t *overlay = lv_obj_create(lv_scr_act());
  lv_obj_set_size(overlay, TFT_WIDTH, TFT_HEIGHT);
  lv_obj_set_pos(overlay, 0, 0);
  lv_obj_set_style_bg_color(overlay, lv_color_make(0,0,0), 0);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_80, 0);
  lv_obj_set_style_border_width(overlay, 0, 0);
  lv_obj_set_style_radius(overlay, 0, 0);
  lv_obj_set_style_pad_all(overlay, 0, 0);

  lv_obj_t *panel = lv_obj_create(overlay);
  lv_obj_set_size(panel, TFT_WIDTH - 20, TFT_HEIGHT - 16);
  lv_obj_center(panel);
  lv_obj_set_style_bg_color(panel, pal.bg2, 0);
  lv_obj_set_style_border_color(panel, pal.border, 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_radius(panel, 12, 0);
  lv_obj_set_style_pad_all(panel, 12, 0);
  lv_obj_set_style_pad_bottom(panel, 8, 0);
  // Make scrollable so all content is accessible
  lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(panel, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_AUTO);

  const int PW = TFT_WIDTH - 20 - 24; // panel width minus padding
  int y = 0;

  // ── Title + Done ─────────────────────────────────────────────────────────
  lv_obj_t *title = lv_label_create(panel);
  lv_label_set_text(title, "Settings");
  lv_obj_set_style_text_color(title, pal.text, 0);
  lv_obj_set_pos(title, 0, y);

  lv_obj_t *closeBtn = lv_btn_create(panel);
  lv_obj_set_size(closeBtn, 60, 22);
  lv_obj_set_pos(closeBtn, PW - 60, y);
  lv_obj_set_style_bg_color(closeBtn, pal.accent, 0);
  lv_obj_set_style_radius(closeBtn, 6, 0);
  lv_obj_t *closeLbl = lv_label_create(closeBtn);
  lv_label_set_text(closeLbl, "Done");
  lv_obj_set_style_text_color(closeLbl, lv_color_white(), 0);
  lv_obj_center(closeLbl);
  lv_obj_add_event_cb(closeBtn, closeSettings, LV_EVENT_CLICKED, overlay);
  y += 30;

  // ── IP address ───────────────────────────────────────────────────────────
  lv_obj_t *ipLbl = lv_label_create(panel);
  char ipbuf[48];
  snprintf(ipbuf, sizeof(ipbuf), "http://%s", WiFiManager::getIP());
  lv_label_set_text(ipLbl, ipbuf);
  lv_label_set_long_mode(ipLbl, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(ipLbl, PW);
  lv_obj_set_style_text_color(ipLbl, pal.accent, 0);
  lv_obj_set_style_text_font(ipLbl, &lv_font_montserrat_12, 0);
  lv_obj_set_pos(ipLbl, 0, y);
  y += 18;

  if (!strlen(Storage::cfg.finnhubKey)) {
    // Finnhub key is optional — stocks use Yahoo Finance now
    // Only show warning if user has explicitly entered a key (legacy) that seems wrong
  }
  y += 6;

  // ── Theme ─────────────────────────────────────────────────────────────────
  lv_obj_t *themeLbl = lv_label_create(panel);
  lv_label_set_text(themeLbl, "Theme:");
  lv_obj_set_style_text_color(themeLbl, pal.textMuted, 0);
  lv_obj_set_pos(themeLbl, 0, y + 8);

  lv_obj_t *roller = lv_roller_create(panel);
  lv_roller_set_options(roller, "Dark\nRetro\nNeon\nClean\nSports", LV_ROLLER_MODE_NORMAL);
  lv_roller_set_selected(roller, Storage::cfg.theme, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(roller, pal.bg, 0);
  lv_obj_set_style_text_color(roller, pal.text, 0);
  lv_obj_set_style_bg_color(roller, pal.accent, LV_PART_SELECTED);
  lv_obj_set_style_text_color(roller, lv_color_white(), LV_PART_SELECTED);
  lv_obj_set_pos(roller, 70, y);
  lv_obj_set_size(roller, 130, 66);
  lv_obj_add_event_cb(roller, [](lv_event_t *e){
    Storage::cfg.theme = lv_roller_get_selected(lv_event_get_target(e));
  }, LV_EVENT_VALUE_CHANGED, nullptr);
  y += 74;

  // ── Ticker Speed ─────────────────────────────────────────────────────────
  lv_obj_t *speedLbl = lv_label_create(panel);
  lv_label_set_text(speedLbl, "Speed:");
  lv_obj_set_style_text_color(speedLbl, pal.textMuted, 0);
  lv_obj_set_pos(speedLbl, 0, y + 6);

  lv_obj_t *slider = lv_slider_create(panel);
  lv_slider_set_range(slider, 1, 5);
  lv_slider_set_value(slider, Storage::cfg.tickerSpeed, LV_ANIM_OFF);
  lv_obj_set_pos(slider, 70, y + 6);
  lv_obj_set_width(slider, PW - 70);
  lv_obj_set_style_bg_color(slider, pal.accent, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, pal.accent, LV_PART_KNOB);
  lv_obj_add_event_cb(slider, [](lv_event_t *e){
    TickerEngine::setSpeed((uint8_t)lv_slider_get_value(lv_event_get_target(e)));
  }, LV_EVENT_VALUE_CHANGED, nullptr);
  y += 28;

  // ── Brightness ───────────────────────────────────────────────────────────
  lv_obj_t *brtLbl = lv_label_create(panel);
  lv_label_set_text(brtLbl, "Bright:");
  lv_obj_set_style_text_color(brtLbl, pal.textMuted, 0);
  lv_obj_set_pos(brtLbl, 0, y + 6);

  lv_obj_t *brtSlider = lv_slider_create(panel);
  lv_slider_set_range(brtSlider, 40, 255);
  lv_slider_set_value(brtSlider, Storage::cfg.brightness, LV_ANIM_OFF);
  lv_obj_set_pos(brtSlider, 70, y + 6);
  lv_obj_set_width(brtSlider, PW - 70);
  lv_obj_set_style_bg_color(brtSlider, pal.accent, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(brtSlider, pal.accent, LV_PART_KNOB);
  lv_obj_add_event_cb(brtSlider, [](lv_event_t *e){
    uint8_t b = (uint8_t)lv_slider_get_value(lv_event_get_target(e));
    Storage::cfg.brightness = b;
    Display::setBrightness(b);
  }, LV_EVENT_VALUE_CHANGED, nullptr);
  y += 28;

  // ── Sleep ────────────────────────────────────────────────────────────────
  lv_obj_t *ssLbl = lv_label_create(panel);
  lv_label_set_text(ssLbl, "Sleep:");
  lv_obj_set_style_text_color(ssLbl, pal.textMuted, 0);
  lv_obj_set_pos(ssLbl, 0, y + 8);

  lv_obj_t *ssRoller = lv_roller_create(panel);
  lv_roller_set_options(ssRoller, "5 min\n10 min\n30 min\nOff", LV_ROLLER_MODE_NORMAL);
  lv_roller_set_selected(ssRoller, 1, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(ssRoller, pal.bg, 0);
  lv_obj_set_style_text_color(ssRoller, pal.text, 0);
  lv_obj_set_style_bg_color(ssRoller, pal.accent, LV_PART_SELECTED);
  lv_obj_set_style_text_color(ssRoller, lv_color_white(), LV_PART_SELECTED);
  lv_obj_set_pos(ssRoller, 70, y);
  lv_obj_set_size(ssRoller, 100, 56);
  lv_obj_add_event_cb(ssRoller, [](lv_event_t *e){
    static const uint32_t timeouts[] = {300, 600, 1800, 0};
    Screensaver::setIdleTimeout(timeouts[lv_roller_get_selected(lv_event_get_target(e))]);
  }, LV_EVENT_VALUE_CHANGED, nullptr);
  y += 64;

  // ── Restart Device button ────────────────────────────────────────────────
  lv_obj_t *restartBtn = lv_btn_create(panel);
  lv_obj_set_size(restartBtn, PW, 28);
  lv_obj_set_pos(restartBtn, 0, y);
  lv_obj_set_style_bg_color(restartBtn, lv_color_make(55, 65, 81), 0);
  lv_obj_set_style_radius(restartBtn, 6, 0);
  lv_obj_t *restartLbl = lv_label_create(restartBtn);
  lv_label_set_text(restartLbl, "Restart Device");
  lv_obj_set_style_text_color(restartLbl, lv_color_make(200, 200, 200), 0);
  lv_obj_set_style_text_font(restartLbl, &lv_font_montserrat_12, 0);
  lv_obj_center(restartLbl);
  lv_obj_add_event_cb(restartBtn, [](lv_event_t*){
    lv_timer_create([](lv_timer_t *t){ lv_timer_del(t); ESP.restart(); }, 300, nullptr);
  }, LV_EVENT_CLICKED, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Captive portal / QR screen
// ─────────────────────────────────────────────────────────────────────────────
void ScreenManager::showCaptivePortalScreen() {
  pal = Themes::get(THEME_DARK);
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, pal.bg, 0);
  lv_obj_clean(scr);

  // Title
  lv_obj_t *title = lv_label_create(scr);
  lv_label_set_text(title, "TickerTouch Setup");
  lv_obj_set_style_text_color(title, pal.accent, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

  // QR code — encodes WiFi join + portal URL
  // WIFI: URI connects phone to the AP; captive portal detection opens browser
  const char *qrData = "WIFI:T:nopass;S:TickerTouch-Setup;P:;;";
  lv_obj_t *qr = lv_qrcode_create(scr, 120, lv_color_black(), lv_color_white());
  lv_qrcode_update(qr, qrData, strlen(qrData));
  lv_obj_align(qr, LV_ALIGN_CENTER, -65, 10);
  lv_obj_set_style_border_color(qr, lv_color_white(), 0);
  lv_obj_set_style_border_width(qr, 4, 0);

  // Instructions
  lv_obj_t *inst = lv_label_create(scr);
  lv_label_set_text(inst,
    "Scan QR to\nconnect & setup\n\n"
    "Or join WiFi:\nTickerTouch-Setup\n\n"
    "Then visit:\n192.168.4.1");
  lv_obj_set_style_text_color(inst, pal.text, 0);
  lv_obj_set_style_text_font(inst, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(inst, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(inst, LV_ALIGN_CENTER, 55, 10);

  // Spinner
  lv_obj_t *spinner = lv_spinner_create(scr, 1000, 60);
  lv_obj_set_size(spinner, 24, 24);
  lv_obj_align(spinner, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_style_arc_color(spinner, pal.accent, LV_PART_INDICATOR);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main dashboard
// ─────────────────────────────────────────────────────────────────────────────
void ScreenManager::showDashboard() {
  initSharedStyles();
  pal = Themes::get(Storage::cfg.theme);

  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, pal.bg, 0);
  lv_obj_clean(scr);

  // Null all child object pointers immediately after clean — prevents dangling
  // refs if a timer fires between clean and pointer reassignment below
  clockLabel = nullptr;
  tabView = tabAll = tabSports = tabFin = tabWeather = tabCal = nullptr;
  // Null ticker label so scrollTimerCb doesn't fire on freed object
  TickerEngine::scrollState.label = nullptr;

  // ── Header bar (top 40px) ────────────────────────────────────────────────
  lv_obj_t *header = lv_obj_create(scr);
  lv_obj_set_size(header, TFT_WIDTH, 40);
  lv_obj_set_pos(header, 0, 0);
  lv_obj_set_style_bg_color(header, pal.bg2, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(header, pal.border, 0);
  lv_obj_set_style_border_width(header, 1, LV_STATE_DEFAULT);
  lv_obj_set_style_radius(header, 0, 0);
  lv_obj_set_style_pad_hor(header, 12, 0);
  lv_obj_set_style_pad_ver(header, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  // App name — left side, accent color, 16px
  lv_obj_t *appName = lv_label_create(header);
  lv_label_set_text(appName, "TickerTouch");
  lv_obj_set_style_text_color(appName, pal.accent, 0);
  lv_obj_set_style_text_font(appName, &lv_font_montserrat_16, 0);
  lv_obj_align(appName, LV_ALIGN_LEFT_MID, 0, 0);

  // Clock — center, large and prominent
  clockLabel = lv_label_create(header);
  lv_label_set_text(clockLabel, "--:--");
  lv_obj_set_style_text_color(clockLabel, pal.text, 0);
  lv_obj_set_style_text_font(clockLabel, &lv_font_montserrat_20, 0);
  lv_obj_align(clockLabel, LV_ALIGN_CENTER, 0, 0);

  // Gear button — right side
  lv_obj_t *gearBtn = lv_btn_create(header);
  lv_obj_set_size(gearBtn, 40, 40);
  lv_obj_align(gearBtn, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_opa(gearBtn, LV_OPA_0, 0);
  lv_obj_set_style_border_width(gearBtn, 0, 0);
  lv_obj_set_style_shadow_width(gearBtn, 0, 0);
  lv_obj_set_style_pad_all(gearBtn, 0, 0);
  lv_obj_t *gearLbl = lv_label_create(gearBtn);
  lv_label_set_text(gearLbl, LV_SYMBOL_SETTINGS);
  lv_obj_set_style_text_color(gearLbl, pal.accent, 0);
  lv_obj_set_style_text_font(gearLbl, &lv_font_montserrat_20, 0);
  lv_obj_center(gearLbl);
  lv_obj_add_event_cb(gearBtn, [](lv_event_t*){ ScreenManager::showSettings(); },
    LV_EVENT_CLICKED, nullptr);

  // ── Scrolling ticker bar (bottom 36px) ──────────────────────────────────
  tickerCont = lv_obj_create(scr);
  lv_obj_set_size(tickerCont, TFT_WIDTH, TICKER_BAR_H);
  lv_obj_set_pos(tickerCont, 0, TFT_HEIGHT - TICKER_BAR_H);
  lv_obj_set_style_bg_color(tickerCont, pal.tickerBg, 0);
  lv_obj_set_style_border_width(tickerCont, 0, 0);
  lv_obj_set_style_radius(tickerCont, 0, 0);
  lv_obj_set_style_pad_all(tickerCont, 0, 0);

  static bool tickerStarted = false;
  if (!tickerStarted) {
    TickerEngine::begin(tickerCont);
    tickerStarted = true;
  } else {
    TickerEngine::reattach(tickerCont);
  }
  if (TickerEngine::scrollState.label)
    lv_obj_set_style_text_color(TickerEngine::scrollState.label, pal.tickerText, 0);

  // ── Tab view (between header and ticker) ────────────────────────────────
  int tabAreaH = TFT_HEIGHT - 40 - TICKER_BAR_H;
  tabView = lv_tabview_create(scr, LV_DIR_TOP, 28); // 28px to fit 5 tabs
  lv_obj_set_pos(tabView, 0, 40);
  lv_obj_set_size(tabView, TFT_WIDTH, tabAreaH);
  lv_obj_set_style_bg_color(tabView, pal.bg, 0);

  lv_obj_t *tabBtns = lv_tabview_get_tab_btns(tabView);
  lv_obj_set_style_bg_color(tabBtns, pal.bg2, 0);
  lv_obj_set_style_text_color(tabBtns, pal.textMuted, 0);
  lv_obj_set_style_text_font(tabBtns, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(tabBtns, pal.accent, LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_border_side(tabBtns, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_border_width(tabBtns, 2, LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_border_color(tabBtns, pal.accent, LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_bg_opa(tabBtns, LV_OPA_0, LV_PART_ITEMS | LV_STATE_CHECKED);

  uint8_t mask = Storage::cfg.tabMask;
  // Short labels that fit at 12pt on a 480px-wide 5-tab bar (~96px each)
  // LV_SYMBOL_HOME, PLAY, REFRESH, BELL all exist in LVGL 8.3
  // Tab labels: Home icon for All, short text for others to be clear at small size
  // LV_SYMBOL_IMAGE = picture icon (weather), LV_SYMBOL_BELL = calendar alert
  tabAll     = lv_tabview_add_tab(tabView, LV_SYMBOL_HOME);
  tabSports  = (mask & 0x01) ? lv_tabview_add_tab(tabView, "Sports")    : nullptr;
  tabFin     = (mask & 0x02) ? lv_tabview_add_tab(tabView, "Finance")    : nullptr;
  tabWeather = (mask & 0x04) ? lv_tabview_add_tab(tabView, "Weather")   : nullptr;
  tabCal     = (mask & 0x08) ? lv_tabview_add_tab(tabView, "Calendar")  : nullptr;

  for (lv_obj_t *t : {tabAll, tabSports, tabFin, tabWeather, tabCal}) {
    if (!t) continue;
    lv_obj_set_style_bg_color(t, pal.bg, 0);
    lv_obj_set_style_pad_all(t, 8, 0);
    lv_obj_set_style_pad_gap(t, 6, 0);
  }

  buildAllTab(tabAll);
  if (tabSports)  buildSportsTab(tabSports);
  if (tabFin)     buildFinanceTab(tabFin);
  if (tabWeather) buildWeatherTab(tabWeather);
  if (tabCal)     buildCalendarTab(tabCal);

  // Delete existing repeating timers before recreating — prevents duplicates
  // accumulating across theme changes / settings closes
  if (clockTimer)      { lv_timer_del(clockTimer);      clockTimer      = nullptr; }
  if (dataUpdateTimer) { lv_timer_del(dataUpdateTimer); dataUpdateTimer = nullptr; }

  clockTimer = lv_timer_create(clockTimerCb, 10000, nullptr);
  clockTimerCb(nullptr);

  // Track data fingerprints — rebuild tabs when underlying data actually changes,
  // not just on first-seen. Uses a simple hash of key fields.
  static uint32_t lastSportsHash = 0, lastWeatherHash = 0;
  static uint32_t lastStocksHash = 0, lastCalHash = 0xFFFFFFFF, lastCryptoHash = 0;

  dataUpdateTimer = lv_timer_create([](lv_timer_t*){

    // Sports: hash league+score+status of first few games
    uint32_t sportsHash = 0;
    for (int i = 0; i < min((int)DataManager::scoreCount, 8); i++) {
      auto &s = DataManager::scores[i];
      for (const char *p = s.awayScore; *p; p++) sportsHash = sportsHash*31 + *p;
      for (const char *p = s.homeScore; *p; p++) sportsHash = sportsHash*31 + *p;
      for (const char *p = s.status;    *p; p++) sportsHash = sportsHash*31 + *p;
    }
    sportsHash ^= (uint32_t)DataManager::scoreCount << 16;

    // Weather: hash all city temps + count
    uint32_t weatherHash = DataManager::weatherCityCount;
    for (int i = 0; i < DataManager::weatherCityCount; i++)
      weatherHash = weatherHash * 31 + (uint32_t)(DataManager::weatherCities[i].tempF * 10);

    // Stocks: hash first stock price
    uint32_t stocksHash = 0;
    if (DataManager::stockCount > 0)
      stocksHash = (uint32_t)(DataManager::stocks[0].price * 100);

    // Crypto: hash first crypto price
    uint32_t cryptoHash = 0;
    if (DataManager::cryptoCount > 0)
      cryptoHash = (uint32_t)(DataManager::cryptos[0].priceUSD);

    // Calendar: hash ready state + count + first event title
    // Adding 0x80000000 when ready ensures ready+0events != not-ready
    uint32_t calHash = (DataManager::calReady ? 0x80000000u : 0u) | DataManager::calCount;
    if (DataManager::calCount > 0)
      for (const char *p = DataManager::calEvents[0].title; *p; p++)
        calHash = calHash*31 + *p;

    bool sportsDone  = DataManager::scoresReady  && (sportsHash  != lastSportsHash);
    bool weatherDone = DataManager::weatherReady  && (weatherHash != lastWeatherHash);
    bool stocksDone  = DataManager::stocksReady   && (stocksHash  != lastStocksHash);
    bool cryptoDone  = DataManager::cryptoReady   && (cryptoHash  != lastCryptoHash) && DataManager::cryptoCount > 0;
    bool calDone     = DataManager::calReady      && (calHash     != lastCalHash);

    // When ready flags go false (forceRefresh), reset hashes to sentinel so the
    // tab always rebuilds when data returns — even if the data is identical
    if (!DataManager::scoresReady)  lastSportsHash  = 0xFFFFFFFF;
    if (!DataManager::weatherReady) lastWeatherHash = 0xFFFFFFFF;
    if (!DataManager::stocksReady)  lastStocksHash  = 0xFFFFFFFF;
    if (!DataManager::cryptoReady)  lastCryptoHash  = 0xFFFFFFFF;
    if (!DataManager::calReady)     lastCalHash     = 0xFFFFFFFE; // different from init sentinel

    if (DataManager::scoresReady)  lastSportsHash  = sportsHash;
    if (DataManager::weatherReady) lastWeatherHash = weatherHash;
    if (DataManager::stocksReady)  lastStocksHash  = stocksHash;
    if (DataManager::cryptoReady && DataManager::cryptoCount > 0) lastCryptoHash = cryptoHash;
    if (DataManager::calReady)     lastCalHash     = calHash;

    if (weatherDone) {
      if (tabAll)    { lv_obj_clean(tabAll);    buildAllTab(tabAll); }
      if (tabWeather){ lv_obj_clean(tabWeather);buildWeatherTab(tabWeather); }
    }
    if (sportsDone) {
      if (tabAll)    { lv_obj_clean(tabAll);    buildAllTab(tabAll); }
      if (tabSports) { lv_obj_clean(tabSports); buildSportsTab(tabSports); }
    }
    if (stocksDone || cryptoDone) {
      if (tabAll)    { lv_obj_clean(tabAll);    buildAllTab(tabAll); }
      if (tabFin)    { lv_obj_clean(tabFin);    buildFinanceTab(tabFin); }
    }
    if (calDone) {
      if (tabAll) { lv_obj_clean(tabAll); buildAllTab(tabAll); }
      if (tabCal) { lv_obj_clean(tabCal); buildCalendarTab(tabCal); }
    }
    if (weatherDone || sportsDone || stocksDone || calDone || cryptoDone) {
      TickerEngine::rebuildAll();
      TickerEngine::refreshLabel();
    }
  }, 2000, nullptr);
}

void ScreenManager::applyTheme(uint8_t themeId) {
  Storage::cfg.theme = themeId;
  Storage::save();
  // Restart cleanest way to apply theme — shared styles need full reinit
  lv_timer_create([](lv_timer_t *){
    ESP.restart();
  }, 200, nullptr);
}

void ScreenManager::updateTickerText(const char *text) {
  TickerEngine::refreshLabel();
}

void ScreenManager::refreshAllWidgets() {
  if (tabAll)    { lv_obj_clean(tabAll);    buildAllTab(tabAll);       }
  if (tabSports) { lv_obj_clean(tabSports); buildSportsTab(tabSports); }
  if (tabFin)    { lv_obj_clean(tabFin);    buildFinanceTab(tabFin);   }
  if (tabWeather){ lv_obj_clean(tabWeather);buildWeatherTab(tabWeather);}
  if (tabCal)    { lv_obj_clean(tabCal);    buildCalendarTab(tabCal);  }
  TickerEngine::rebuildAll();
  TickerEngine::refreshLabel();
}

void ScreenManager::showSplash() {
  pal = Themes::get(THEME_DARK);
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, pal.bg, 0);

  // App name — big, centered high
  lv_obj_t *lbl = lv_label_create(scr);
  lv_label_set_text(lbl, "TickerTouch");
  lv_obj_set_style_text_color(lbl, pal.accent, 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_48, 0);
  lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -40);

  // Tagline
  lv_obj_t *tag = lv_label_create(scr);
  lv_label_set_text(tag, "Your desk. Your data.");
  lv_obj_set_style_text_color(tag, pal.textMuted, 0);
  lv_obj_set_style_text_font(tag, &lv_font_montserrat_16, 0);
  lv_obj_align(tag, LV_ALIGN_CENTER, 0, 12);

  // Spinner below tagline
  lv_obj_t *spinner = lv_spinner_create(scr, 1000, 60);
  lv_obj_set_size(spinner, 32, 32);
  lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 52);
  lv_obj_set_style_arc_color(spinner, pal.accent, LV_PART_INDICATOR);

  // Status below spinner
  lv_obj_t *status = lv_label_create(scr);
  lv_label_set_text(status, "Connecting...");
  lv_obj_set_style_text_color(status, pal.textMuted, 0);
  lv_obj_set_style_text_font(status, &lv_font_montserrat_12, 0);
  lv_obj_align(status, LV_ALIGN_CENTER, 0, 82);
}
