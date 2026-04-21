#include "screen_manager.h"
#include "../ticker_engine.h"
#include "../screensaver.h"
#include "../themes/themes.h"
#include "../data/data_manager.h"
#include "../storage.h"
#include "../wifi_manager.h"
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
static void styleLabel(lv_obj_t *obj, lv_color_t col) {
  static lv_style_t s;
  lv_style_init(&s);
  lv_style_set_text_color(&s, col);
  lv_obj_add_style(obj, &s, 0);
}

static lv_obj_t* makeCard(lv_obj_t *parent, int32_t w, int32_t h) {
  lv_obj_t *c = lv_obj_create(parent);
  lv_obj_set_size(c, w, h);
  lv_obj_set_style_bg_color(c, pal.bg2, 0);
  lv_obj_set_style_border_color(c, pal.border, 0);
  lv_obj_set_style_border_width(c, 1, 0);
  lv_obj_set_style_radius(c, 12, 0);
  lv_obj_set_style_pad_all(c, 10, 0);
  lv_obj_set_style_shadow_width(c, 8, 0);
  lv_obj_set_style_shadow_color(c, lv_color_make(0,0,0), 0);
  lv_obj_set_style_shadow_opa(c, LV_OPA_30, 0);
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

  // Sports rows — only live and final games (not scheduled)
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
      char key[12], val[36];
      snprintf(key, sizeof(key), "%s", s.league);
      snprintf(val, sizeof(val), "%s %s-%s %s", s.awayTeam, s.awayScore, s.homeScore, s.homeTeam);
      // Append period/inning status
      char full[48];
      snprintf(full, sizeof(full), "%s %s", val, s.status);
      addRow(key, full, pal.text);
      shown++;
    }
    if (shown == 0 && DataManager::scoreCount > 0)
      addRow("Sports", "No live games", pal.textMuted);
    else if (DataManager::scoreCount == 0)
      addRow("Sports", "No games today", pal.textMuted);
  }

  // Stock rows — second
  if (DataManager::stocksReady && DataManager::stockCount > 0) {
    for (int i = 0; i < DataManager::stockCount; i++) {
      auto &s = DataManager::stocks[i];
      char val[24]; snprintf(val, sizeof(val), "$%.2f  %+.2f%%", s.price, s.changePercent);
      addRow(s.symbol, val, s.changePercent >= 0 ? pal.positive : pal.negative);
    }
  } else if (!DataManager::stocksReady && strlen(Storage::cfg.stocks)) {
    addRow("Stocks", "Loading...", pal.textMuted);
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

  // Weather row — last
  if (DataManager::weatherReady) {
    char buf[48];
    snprintf(buf, sizeof(buf), "%.0fF  %s", DataManager::weather.tempF, DataManager::weather.description);
    addRow(DataManager::weather.city, buf, pal.text);
  } else {
    addRow("Weather", "Loading...", pal.textMuted);
  }

  // Calendar rows — today's events
  if (Storage::cfg.tabMask & 0x08) {
    if (DataManager::calReady && DataManager::calCount > 0) {
      for (int i = 0; i < DataManager::calCount; i++) {
        auto &e = DataManager::calEvents[i];
        addRow(e.timeStr, e.title, pal.accent);
      }
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
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, pal.textMuted, 0);
    lv_obj_center(lbl);
    return;
  }
  if (DataManager::scoreCount == 0) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "No games today");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, pal.textMuted, 0);
    lv_obj_center(lbl);
    return;
  }

  // Full-height scrollable column
  lv_obj_t *scroll = lv_obj_create(parent);
  lv_obj_set_size(scroll, TFT_WIDTH - 20, lv_obj_get_height(parent));
  lv_obj_set_pos(scroll, 0, 0);
  lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(scroll, 0, 0);
  lv_obj_set_style_pad_all(scroll, 0, 0);
  lv_obj_set_layout(scroll, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(scroll, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_gap(scroll, 0, 0);
  lv_obj_add_flag(scroll, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_AUTO);

  // Colored badge helper: rounded rect with team color + white abbreviation
  auto makeBadge = [&](lv_obj_t *par, const char *abbr, uint32_t rgb) -> lv_obj_t* {
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >>  8) & 0xFF;
    uint8_t b =  rgb        & 0xFF;
    // Boost very dark colors so they're visible
    if ((r + g + b) < 120) { r = r/2+60; g = g/2+60; b = b/2+60; }
    lv_obj_t *badge = lv_obj_create(par);
    lv_obj_set_size(badge, 52, 34);
    lv_obj_set_style_bg_color(badge, lv_color_make(r, g, b), 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_set_style_radius(badge, 8, 0);
    lv_obj_set_style_pad_all(badge, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *lbl = lv_label_create(badge);
    lv_label_set_text(lbl, abbr);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);
    return badge;
  };

  const char *lastLeague = "";
  const int ROW_W = TFT_WIDTH - 20;

  for (int i = 0; i < DataManager::scoreCount; i++) {
    auto &s = DataManager::scores[i];
    bool isFinal = strstr(s.status, "Final") != nullptr;
    // Scheduled: has time (PM/AM), or is a bare date like "4/17", or empty with 0-0 scores
    bool isSched = !isFinal && (
      strstr(s.status, "PM") || strstr(s.status, "AM") ||
      (strchr(s.status, '/') != nullptr) || // date like "4/17"
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
      lv_obj_set_style_border_width(hdr, 0, 0);
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
      lv_obj_set_style_text_font(hdrLbl, &lv_font_montserrat_12, 0);
      lv_obj_set_style_text_color(hdrLbl, pal.accent, 0);
      lv_obj_align(hdrLbl, LV_ALIGN_LEFT_MID, 0, 0);
    }

    // Game row
    lv_obj_t *row = lv_obj_create(scroll);
    lv_obj_set_size(row, ROW_W, 56);
    lv_obj_set_style_bg_color(row, pal.bg2, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, pal.border, 0);
    lv_obj_set_style_border_width(row, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    // Away badge — left side
    lv_obj_t *awayBadge = makeBadge(row, s.awayTeam, s.awayColor);
    lv_obj_set_pos(awayBadge, 8, 11);

    // Away score
    lv_obj_t *awayScLbl = lv_label_create(row);
    lv_label_set_text(awayScLbl, isSched ? "  " : s.awayScore);
    lv_obj_set_style_text_font(awayScLbl, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(awayScLbl, pal.text, 0);
    lv_obj_set_pos(awayScLbl, 68, 14);

    // Center status — time/period/Final
    lv_obj_t *midLbl = lv_label_create(row);
    lv_label_set_text(midLbl, isSched ? s.status : (isFinal ? "Final" : s.status));
    lv_obj_set_style_text_font(midLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(midLbl,
      isFinal ? pal.textMuted : (isLive ? pal.positive : pal.accent), 0);
    lv_obj_align(midLbl, LV_ALIGN_CENTER, 0, 0);

    // Home score — right of center
    lv_obj_t *homeScLbl = lv_label_create(row);
    lv_label_set_text(homeScLbl, isSched ? "  " : s.homeScore);
    lv_obj_set_style_text_font(homeScLbl, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(homeScLbl, pal.text, 0);
    lv_obj_set_pos(homeScLbl, ROW_W - 68 - 30, 14);

    // Home badge — right side
    lv_obj_t *homeBadge = makeBadge(row, s.homeTeam, s.homeColor);
    lv_obj_set_pos(homeBadge, ROW_W - 60, 11);

    // Green live dot
    if (isLive) {
      lv_obj_t *dot = lv_obj_create(row);
      lv_obj_set_size(dot, 6, 6);
      lv_obj_set_style_bg_color(dot, pal.positive, 0);
      lv_obj_set_style_radius(dot, 3, 0);
      lv_obj_set_style_border_width(dot, 0, 0);
      lv_obj_set_pos(dot, 2, 2);
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

  bool hasAnything = false;

  if (DataManager::stocksReady && DataManager::stockCount > 0) {
    hasAnything = true;
    bool anyClosed = false;
    for (int i = 0; i < DataManager::stockCount; i++)
      if (DataManager::stocks[i].marketClosed) { anyClosed = true; break; }

    if (anyClosed) {
      lv_obj_t *closedLbl = lv_label_create(parent);
      lv_label_set_text(closedLbl, "Market Closed - Last Close Prices");
      lv_obj_set_style_text_color(closedLbl, pal.textMuted, 0);
      lv_obj_set_style_text_font(closedLbl, &lv_font_montserrat_12, 0);
      lv_obj_set_width(closedLbl, TFT_WIDTH - 20);
    }

    // 3 per row: (480 - 20 pad - 2×8 gap) / 3 = ~148, use 140 to give breathing room
    for (int i = 0; i < DataManager::stockCount; i++) {
      auto &s = DataManager::stocks[i];
      lv_obj_t *card = makeCard(parent, 140, 76);  // slightly taller for 3 lines

      lv_obj_t *sym = lv_label_create(card);
      lv_label_set_text(sym, s.symbol);
      lv_obj_set_style_text_font(sym, &lv_font_montserrat_14, 0);
      lv_obj_set_style_text_color(sym, s.marketClosed ? pal.textMuted : pal.accent, 0);
      lv_obj_set_pos(sym, 0, 0);

      char price[20]; snprintf(price, sizeof(price), "$%.2f", s.price);
      lv_obj_t *priceLbl = lv_label_create(card);
      lv_label_set_text(priceLbl, price);
      lv_obj_set_style_text_font(priceLbl, &lv_font_montserrat_18, 0);
      lv_obj_set_style_text_color(priceLbl, pal.text, 0);
      lv_obj_set_pos(priceLbl, 0, 18);

      char chg[20]; snprintf(chg, sizeof(chg), "%+.1f%%", s.changePercent);
      lv_obj_t *chgLbl = lv_label_create(card);
      lv_label_set_text(chgLbl, chg);
      lv_obj_set_style_text_font(chgLbl, &lv_font_montserrat_14, 0);
      lv_obj_set_style_text_color(chgLbl,
        s.changePercent > 0 ? pal.positive : (s.changePercent < 0 ? pal.negative : pal.textMuted), 0);
      lv_obj_set_pos(chgLbl, 0, 46);
    }
  } else if (!DataManager::stocksReady && strlen(Storage::cfg.stocks)) {
    hasAnything = true;
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "Loading stocks...");
    lv_obj_set_style_text_color(lbl, pal.textMuted, 0);
  }

  if (!hasAnything) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, strlen(Storage::cfg.stocks)
      ? "Market closed or no data"
      : "No stocks configured.\nOpen settings to add tickers.");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
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
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl);
    return;
  }

  // Date header
  time_t now = time(nullptr);
  struct tm *lt = localtime(&now);
  static const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                  "Jul","Aug","Sep","Oct","Nov","Dec"};
  static const char *days[]   = {"Sunday","Monday","Tuesday","Wednesday",
                                  "Thursday","Friday","Saturday"};
  char dateHdr[40];
  snprintf(dateHdr, sizeof(dateHdr), "%s, %s %d", days[lt->tm_wday], months[lt->tm_mon], lt->tm_mday);

  lv_obj_t *dateLbl = lv_label_create(parent);
  lv_label_set_text(dateLbl, dateHdr);
  lv_obj_set_style_text_font(dateLbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(dateLbl, pal.accent, 0);
  lv_obj_set_pos(dateLbl, 0, 0);

  if (DataManager::calCount == 0) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "No events today");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, pal.textMuted, 0);
    lv_obj_set_pos(lbl, 0, 26);
    return;
  }

  // Scrollable event list
  lv_obj_t *scroll = lv_obj_create(parent);
  lv_obj_set_size(scroll, TFT_WIDTH - 20, lv_obj_get_height(parent) - 26);
  lv_obj_set_pos(scroll, 0, 24);
  lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(scroll, 0, 0);
  lv_obj_set_style_pad_all(scroll, 0, 0);
  lv_obj_set_layout(scroll, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(scroll, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_gap(scroll, 4, 0);
  lv_obj_add_flag(scroll, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_AUTO);

  for (int i = 0; i < DataManager::calCount; i++) {
    auto &e = DataManager::calEvents[i];

    lv_obj_t *row = lv_obj_create(scroll);
    lv_obj_set_size(row, TFT_WIDTH - 20, 48);
    lv_obj_set_style_bg_color(row, pal.bg2, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_color(row, pal.accent, 0);
    lv_obj_set_style_border_width(row, 3, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(row, 4, 0);
    lv_obj_set_style_pad_left(row, 10, 0);
    lv_obj_set_style_pad_right(row, 6, 0);
    lv_obj_set_style_pad_ver(row, 4, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    // Time — top left
    lv_obj_t *timeLbl = lv_label_create(row);
    lv_label_set_text(timeLbl, e.timeStr);
    lv_obj_set_style_text_font(timeLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(timeLbl, pal.accent, 0);
    lv_obj_set_pos(timeLbl, 0, 0);

    // Title — main line
    lv_obj_t *titleLbl = lv_label_create(row);
    lv_label_set_text(titleLbl, e.title);
    lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(titleLbl, pal.text, 0);
    lv_obj_set_width(titleLbl, TFT_WIDTH - 40);
    lv_label_set_long_mode(titleLbl, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(titleLbl, 0, 16);
  }
}

static void buildWeatherTab(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, pal.bg, 0);
  // Disable horizontal scrolling — everything must fit within tab width
  lv_obj_set_scroll_dir(parent, LV_DIR_NONE);
  lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);

  if (!DataManager::weatherReady) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, Storage::cfg.lat == 0.0f
      ? "No location set.\nOpen settings to enter your city."
      : "Loading weather...");
    lv_obj_set_style_text_color(lbl, pal.textMuted, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl);
    return;
  }

  auto &w = DataManager::weather;
  // Tab has pad_all=8 so usable width = TFT_WIDTH - 16 = 464px
  // Right-align labels relative to usable width using negative x offset
  const int RW = TFT_WIDTH - 16; // 464
  int y = 2;

  // ── Top row: city (left) + condition (right) ──────────────────────────
  lv_obj_t *city = lv_label_create(parent);
  lv_label_set_text(city, w.city);
  lv_obj_set_style_text_color(city, pal.accent, 0);
  lv_obj_set_style_text_font(city, &lv_font_montserrat_16, 0);
  lv_obj_set_pos(city, 0, y);

  lv_obj_t *condLbl = lv_label_create(parent);
  lv_label_set_text(condLbl, w.description);
  lv_obj_set_style_text_color(condLbl, pal.textMuted, 0);
  lv_obj_set_style_text_font(condLbl, &lv_font_montserrat_14, 0);
  lv_obj_set_width(condLbl, 180);
  lv_obj_set_style_text_align(condLbl, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(condLbl, RW - 180, y + 2);
  y += 22;

  // ── Big temperature (left) + feels/H/L (right) ────────────────────────
  char temp[12]; snprintf(temp, sizeof(temp), "%.0f\xB0""F", w.tempF);
  lv_obj_t *tempLbl = lv_label_create(parent);
  lv_label_set_text(tempLbl, temp);
  lv_obj_set_style_text_font(tempLbl, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(tempLbl, pal.text, 0);
  lv_obj_set_pos(tempLbl, 0, y);

  char feels[20]; snprintf(feels, sizeof(feels), "Feels %.0f\xB0""F", w.feelsLike);
  lv_obj_t *feelsLbl = lv_label_create(parent);
  lv_label_set_text(feelsLbl, feels);
  lv_obj_set_style_text_color(feelsLbl, pal.textMuted, 0);
  lv_obj_set_style_text_font(feelsLbl, &lv_font_montserrat_14, 0);
  lv_obj_set_pos(feelsLbl, RW - 100, y);

  char hl[20]; snprintf(hl, sizeof(hl), "H:%.0f\xB0  L:%.0f\xB0", w.highF, w.lowF);
  lv_obj_t *hlLbl = lv_label_create(parent);
  lv_label_set_text(hlLbl, hl);
  lv_obj_set_style_text_color(hlLbl, pal.text, 0);
  lv_obj_set_style_text_font(hlLbl, &lv_font_montserrat_14, 0);
  lv_obj_set_pos(hlLbl, RW - 100, y + 18);
  y += 36;

  // ── Humidity + Wind ────────────────────────────────────────────────────
  char hw[48]; snprintf(hw, sizeof(hw), "Humidity %d%%   Wind %.0f mph", w.humidity, w.windMph);
  lv_obj_t *hwLbl = lv_label_create(parent);
  lv_label_set_text(hwLbl, hw);
  lv_obj_set_style_text_color(hwLbl, pal.textMuted, 0);
  lv_obj_set_style_text_font(hwLbl, &lv_font_montserrat_12, 0);
  lv_obj_set_pos(hwLbl, 0, y); y += 18;

  // ── 3-day forecast cards — 105px wide (30% smaller), 8px gap ──────────
  static const char *wdayNames[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  char dayNames[3][8];
  time_t now = time(nullptr);
  for (int i = 0; i < 3; i++) {
    time_t future = now + (i + 1) * 86400;
    struct tm *ft = localtime(&future);
    strlcpy(dayNames[i], wdayNames[ft->tm_wday], sizeof(dayNames[i]));
  }
  // 3 cards × 105 + 2 gaps × 8 = 315 + 16 = 331px — fits fine in 464px
  // Centre the group: start_x = (464 - 331) / 2 = 66
  int cx = (RW - (3 * 105 + 2 * 8)) / 2;
  for (int i = 0; i < 3; i++) {
    lv_obj_t *card = makeCard(parent, 105, 66);
    lv_obj_set_pos(card, cx, y);
    cx += 105 + 8;

    lv_obj_t *dayLbl = lv_label_create(card);
    lv_label_set_text(dayLbl, dayNames[i]);
    lv_obj_set_style_text_font(dayLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(dayLbl, pal.accent, 0);
    lv_obj_set_pos(dayLbl, 4, 2);

    char fc[20]; snprintf(fc, sizeof(fc), "%.0f / %.0f", w.forecast[i].highF, w.forecast[i].lowF);
    lv_obj_t *fcLbl = lv_label_create(card);
    lv_label_set_text(fcLbl, fc);
    lv_obj_set_style_text_font(fcLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(fcLbl, pal.text, 0);
    lv_obj_set_pos(fcLbl, 4, 18);

    lv_obj_t *fcCond = lv_label_create(card);
    lv_label_set_text(fcCond, wmoDescShort(w.forecast[i].code));
    lv_obj_set_style_text_font(fcCond, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(fcCond, pal.textMuted, 0);
    lv_obj_set_pos(fcCond, 4, 38);
    lv_obj_set_width(fcCond, 97);
    lv_label_set_long_mode(fcCond, LV_LABEL_LONG_CLIP);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Settings overlay (gear cog tap)
// ─────────────────────────────────────────────────────────────────────────────
static void closeSettings(lv_event_t *e) {
  lv_obj_t *overlay = (lv_obj_t*)lv_event_get_user_data(e);
  TickerEngine::setPaused(false);
  lv_obj_del(overlay);
  // Apply theme now — deferred until Done so overlay isn't destroyed mid-interaction
  uint8_t t = Storage::cfg.theme;
  Storage::save();
  ScreenManager::applyTheme(t);
}

void ScreenManager::showSettings() {
  TickerEngine::setPaused(true);
  // Full-screen overlay
  lv_obj_t *overlay = lv_obj_create(lv_scr_act());
  lv_obj_set_size(overlay, TFT_WIDTH, TFT_HEIGHT);
  lv_obj_set_pos(overlay, 0, 0);
  lv_obj_set_style_bg_color(overlay, lv_color_make(0,0,0), 0);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_80, 0);
  lv_obj_set_style_border_width(overlay, 0, 0);
  lv_obj_set_style_radius(overlay, 0, 0);
  lv_obj_set_style_pad_all(overlay, 0, 0);

  // Settings panel — taller to fit all elements
  lv_obj_t *panel = lv_obj_create(overlay);
  lv_obj_set_size(panel, TFT_WIDTH - 20, TFT_HEIGHT - 16);
  lv_obj_center(panel);
  lv_obj_set_style_bg_color(panel, pal.bg2, 0);
  lv_obj_set_style_border_color(panel, pal.border, 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_radius(panel, 12, 0);
  lv_obj_set_style_pad_all(panel, 12, 0);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

  // Row 0 — Title (left) + Done button (right)
  lv_obj_t *title = lv_label_create(panel);
  lv_label_set_text(title, "Settings");
  lv_obj_set_style_text_color(title, pal.text, 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *closeBtn = lv_btn_create(panel);
  lv_obj_set_size(closeBtn, 60, 22);
  lv_obj_align(closeBtn, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_set_style_bg_color(closeBtn, pal.accent, 0);
  lv_obj_set_style_radius(closeBtn, 6, 0);
  lv_obj_t *closeLbl = lv_label_create(closeBtn);
  lv_label_set_text(closeLbl, "Done");
  lv_obj_set_style_text_color(closeLbl, lv_color_white(), 0);
  lv_obj_center(closeLbl);
  lv_obj_add_event_cb(closeBtn, closeSettings, LV_EVENT_CLICKED, overlay);

  // Bottom info strip — IP address + optional Finnhub warning
  lv_obj_t *ipLbl = lv_label_create(panel);
  char ipbuf[48];
  snprintf(ipbuf, sizeof(ipbuf), "Settings: http://%s", WiFiManager::getIP());
  lv_label_set_text(ipLbl, ipbuf);
  lv_label_set_long_mode(ipLbl, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(ipLbl, TFT_WIDTH - 44);
  lv_obj_set_style_text_color(ipLbl, pal.accent, 0);
  lv_obj_set_style_text_font(ipLbl, &lv_font_montserrat_12, 0);

  if (!strlen(Storage::cfg.finnhubKey)) {
    // Two lines — warning above, IP below
    lv_obj_t *warnLbl = lv_label_create(panel);
    lv_label_set_text(warnLbl, "! No Finnhub key - stocks disabled");
    lv_obj_set_style_text_color(warnLbl, lv_color_make(245,158,11), 0);
    lv_obj_set_style_text_font(warnLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(warnLbl, LV_ALIGN_BOTTOM_LEFT, 0, -14);
    lv_obj_align(ipLbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  } else {
    lv_obj_align(ipLbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  }

  // Row 2 — Theme (y=52)
  lv_obj_t *themeLbl = lv_label_create(panel);
  lv_label_set_text(themeLbl, "Theme:");
  lv_obj_set_style_text_color(themeLbl, pal.textMuted, 0);
  lv_obj_align(themeLbl, LV_ALIGN_TOP_LEFT, 0, 56);

  static const char *themeNames[] = {"Dark", "Retro", "Neon", "Clean", "Sports", ""};
  lv_obj_t *roller = lv_roller_create(panel);
  lv_roller_set_options(roller, "Dark\nRetro\nNeon\nClean\nSports", LV_ROLLER_MODE_NORMAL);
  lv_roller_set_selected(roller, Storage::cfg.theme, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(roller, pal.bg, 0);
  lv_obj_set_style_text_color(roller, pal.text, 0);
  lv_obj_set_style_bg_color(roller, pal.accent, LV_PART_SELECTED);
  lv_obj_set_style_text_color(roller, lv_color_white(), LV_PART_SELECTED);
  lv_obj_align(roller, LV_ALIGN_TOP_LEFT, 70, 48);
  lv_obj_set_size(roller, 130, 66);

  lv_obj_add_event_cb(roller, [](lv_event_t *e){
    // Just save the selection — apply on Done button, not immediately
    // (immediate apply destroys the overlay which is jarring)
    uint16_t sel = lv_roller_get_selected(lv_event_get_target(e));
    Storage::cfg.theme = sel;
  }, LV_EVENT_VALUE_CHANGED, nullptr);

  // Row 3 — Speed (y=122)
  lv_obj_t *speedLbl = lv_label_create(panel);
  lv_label_set_text(speedLbl, "Speed:");
  lv_obj_set_style_text_color(speedLbl, pal.textMuted, 0);
  lv_obj_align(speedLbl, LV_ALIGN_TOP_LEFT, 0, 128);

  lv_obj_t *slider = lv_slider_create(panel);
  lv_slider_set_range(slider, 1, 5);
  lv_slider_set_value(slider, Storage::cfg.tickerSpeed, LV_ANIM_OFF);
  lv_obj_align(slider, LV_ALIGN_TOP_LEFT, 70, 128);
  lv_obj_set_width(slider, TFT_WIDTH - 110);
  lv_obj_set_style_bg_color(slider, pal.accent, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, pal.accent, LV_PART_KNOB);

  lv_obj_add_event_cb(slider, [](lv_event_t *e){
    lv_obj_t *s = lv_event_get_target(e);
    TickerEngine::setSpeed((uint8_t)lv_slider_get_value(s));
  }, LV_EVENT_VALUE_CHANGED, nullptr);

  // Row 4 — Sleep (y=152)
  lv_obj_t *ssLbl = lv_label_create(panel);
  lv_label_set_text(ssLbl, "Sleep:");
  lv_obj_set_style_text_color(ssLbl, pal.textMuted, 0);
  lv_obj_align(ssLbl, LV_ALIGN_TOP_LEFT, 0, 160);

  static const char *ssOpts = "5 min\n10 min\n30 min\nOff";
  lv_obj_t *ssRoller = lv_roller_create(panel);
  lv_roller_set_options(ssRoller, ssOpts, LV_ROLLER_MODE_NORMAL);
  lv_roller_set_selected(ssRoller, 1, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(ssRoller, pal.bg, 0);
  lv_obj_set_style_text_color(ssRoller, pal.text, 0);
  lv_obj_set_style_bg_color(ssRoller, pal.accent, LV_PART_SELECTED);
  lv_obj_set_style_text_color(ssRoller, lv_color_white(), LV_PART_SELECTED);
  lv_obj_align(ssRoller, LV_ALIGN_TOP_LEFT, 70, 152);
  lv_obj_set_size(ssRoller, 100, 56);

  lv_obj_add_event_cb(ssRoller, [](lv_event_t *e){
    static const uint32_t timeouts[] = {300, 600, 1800, 0};
    uint16_t sel = lv_roller_get_selected(lv_event_get_target(e));
    Screensaver::setIdleTimeout(timeouts[sel]);
  }, LV_EVENT_VALUE_CHANGED, nullptr);
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
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

  // Instruction
  lv_obj_t *inst = lv_label_create(scr);
  lv_label_set_text(inst,
    "Connect to WiFi:\n"
    "\"TickerTouch-Setup\"\n\n"
    "Then open:\n"
    "http://192.168.4.1");
  lv_obj_set_style_text_color(inst, pal.text, 0);
  lv_obj_set_style_text_align(inst, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(inst, LV_ALIGN_CENTER, 60, 0);

  // QR code box — text fallback (lv_lib_qrcode not installed)
  lv_obj_t *qrBox = lv_obj_create(scr);
  lv_obj_set_size(qrBox, 120, 120);
  lv_obj_align(qrBox, LV_ALIGN_CENTER, -80, 0);
  lv_obj_set_style_bg_color(qrBox, lv_color_white(), 0);
  lv_obj_set_style_border_width(qrBox, 0, 0);
  lv_obj_set_style_radius(qrBox, 4, 0);
  lv_obj_clear_flag(qrBox, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *qrLbl = lv_label_create(qrBox);
  lv_label_set_text(qrLbl, "192.168.4.1");
  lv_obj_set_style_text_color(qrLbl, lv_color_black(), 0);
  lv_obj_set_style_text_align(qrLbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(qrLbl);

  // Spinner
  lv_obj_t *spinner = lv_spinner_create(scr, 1000, 60);
  lv_obj_set_size(spinner, 28, 28);
  lv_obj_align(spinner, LV_ALIGN_BOTTOM_MID, 0, -12);
  lv_obj_set_style_arc_color(spinner, pal.accent, LV_PART_INDICATOR);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main dashboard
// ─────────────────────────────────────────────────────────────────────────────
void ScreenManager::showDashboard() {
  pal = Themes::get(Storage::cfg.theme);

  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, pal.bg, 0);
  lv_obj_clean(scr);

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
  tabFin     = (mask & 0x02) ? lv_tabview_add_tab(tabView, "Stocks")    : nullptr;
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

  lv_timer_create(clockTimerCb, 10000, nullptr);
  clockTimerCb(nullptr);

  static bool lastWeatherSeen=false, lastSportsSeen=false, lastStocksSeen=false, lastCalSeen=false;
  lastWeatherSeen=false; lastSportsSeen=false; lastStocksSeen=false; lastCalSeen=false;

  lv_timer_create([](lv_timer_t*){
    bool weatherDone = DataManager::weatherReady && !lastWeatherSeen;
    bool sportsDone  = DataManager::scoresReady  && !lastSportsSeen;
    bool stocksDone  = DataManager::stocksReady  && !lastStocksSeen;
    bool calDone     = DataManager::calReady     && !lastCalSeen;
    lastWeatherSeen = DataManager::weatherReady;
    lastSportsSeen  = DataManager::scoresReady;
    lastStocksSeen  = DataManager::stocksReady;
    lastCalSeen     = DataManager::calReady;

    if (weatherDone) {
      if (tabAll)    { lv_obj_clean(tabAll);    buildAllTab(tabAll); }
      if (tabWeather){ lv_obj_clean(tabWeather);buildWeatherTab(tabWeather); }
    }
    if (sportsDone) {
      if (tabAll)    { lv_obj_clean(tabAll);    buildAllTab(tabAll); }
      if (tabSports) { lv_obj_clean(tabSports); buildSportsTab(tabSports); }
    }
    if (stocksDone) {
      if (tabAll)    { lv_obj_clean(tabAll);    buildAllTab(tabAll); }
      if (tabFin)    { lv_obj_clean(tabFin);    buildFinanceTab(tabFin); }
    }
    if (calDone) {
      if (tabAll) { lv_obj_clean(tabAll); buildAllTab(tabAll); }
      if (tabCal) { lv_obj_clean(tabCal); buildCalendarTab(tabCal); }
    }
    if (weatherDone || sportsDone || stocksDone || calDone) {
      TickerEngine::rebuildAll();
      TickerEngine::refreshLabel();
    }
  }, 2000, nullptr);
}

void ScreenManager::applyTheme(uint8_t themeId) {
  Storage::cfg.theme = themeId;
  Storage::save();
  // Schedule dashboard rebuild on next LVGL timer tick instead of inline
  lv_timer_create([](lv_timer_t *t){
    lv_timer_del(t);
    ScreenManager::showDashboard();
  }, 50, nullptr);
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

  lv_obj_t *lbl = lv_label_create(scr);
  lv_label_set_text(lbl, "TickerTouch");
  lv_obj_set_style_text_color(lbl, pal.accent, 0);
  lv_obj_center(lbl);

  lv_obj_t *spinner = lv_spinner_create(scr, 1000, 60);
  lv_obj_set_size(spinner, 40, 40);
  lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 40);
  lv_obj_set_style_arc_color(spinner, pal.accent, LV_PART_INDICATOR);
}
