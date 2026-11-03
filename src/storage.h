#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "../config.h"

// ── Calendar identity icons ──────────────────────────────────────────────────
// Stored as an index, not a string: a saved value can then never drift to a
// different glyph when entries are appended to the list. The display maps the
// index to an LV_SYMBOL_* glyph, the settings page maps it to a name.
enum CalIcon : uint8_t {
  CALICON_HOME = 0, CALICON_KEYBOARD, CALICON_FOLDER,
  CALICON_MAIL, CALICON_PHONE, CALICON_DOT, CALICON_NONE,
  CALICON_COUNT
};

// Saved user configuration
struct UserConfig {
  bool    configured    = false;
  uint8_t theme         = THEME_DARK;
  uint32_t widgetMask   = WIDGETS_DEFAULT;
  char     finnhubKey[48] = {};   // Free API key from finnhub.io

  // Sports: bitmask of enabled leagues
  uint16_t sportsLeagues = 0b00001111;  // NFL, NBA, NHL, MLB by default

  // Stocks: comma-separated tickers stored as string
  char stocks[128]  = "SPY,AAPL,TSLA,NVDA,BTC-USD";

  // Crypto: comma-separated CoinGecko IDs
  char crypto[128]  = "bitcoin,ethereum,solana";

  // Primary weather location (kept for backwards compat)
  char   city[64]   = "New York";
  char   state[32]  = {};
  float  lat        = 0.0f;
  float  lon        = 0.0f;

  // Additional weather cities (up to 2 more)
  struct CitySlot {
    char  city[64]  = {};
    char  state[32] = {};
    float lat       = 0.0f;
    float lon       = 0.0f;
  };
  CitySlot extraCities[2]; // city2 and city3
  uint8_t  weatherCityCount = 1; // 1-3 active cities

  // Ticker speed 1–5 (maps to px/tick)
  uint8_t tickerSpeed = 2;
  uint8_t brightness  = 200;  // 0-255, default full

  // 24-hour clock
  bool clock24h = false;

  // Screensaver idle timeout in seconds (0 = never sleep)
  uint32_t saverSec = 600;

  // Local UTC offset in seconds (set from ip-api on boot, used to convert game times)
  int32_t tzOffsetSec = 0;

  // Timezone override. Empty = take the zone from the IP lookup. Set it when the
  // lookup is wrong, which is otherwise a silent way for the clock and every UTC
  // calendar entry to sit an hour out.
  char tzZone[40] = {};

  // iCal URL for calendar tab (Google/Outlook/Apple secret feed URL)
  char icalUrl[256] = {};

  // Optional second feed. Events from it are colour-coded and badged with their
  // own icon so a work entry never reads as a personal one at a glance.
  char    icalUrl2[256] = {};
  uint8_t calIcon1 = CALICON_HOME;      // badge for the first feed
  uint8_t calIcon2 = CALICON_KEYBOARD;  // badge for the second feed

  // Sports filtering — empty = show all teams for that league
  // teamFilter: comma-separated abbreviations e.g. "NYY,LAD,SEA,NYM"
  // Applied per-league: if a league's filter is empty, show all its games
  char teamFilter[192] = {};   // global team abbrev filter across all leagues

  // CFB/CBB conference filter — comma-separated short names e.g. "SEC,Big Ten,ACC"
  // Also supports "Top 25" as a special token
  char cfbConf[96] = {};

  // Tab visibility bitmask — which tabs to show
  // Bit 0=Sports, 1=Finance, 2=Weather, 3=Calendar
  uint8_t tabMask = 0b0111; // Sports+Finance+Weather on by default
};

namespace Storage {
  extern UserConfig cfg;

  void   begin();
  void   save();
  void   load();
  bool   isConfigured();
  void   setConfigured(bool v);
  void   reset();
}
