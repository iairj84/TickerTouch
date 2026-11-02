#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "../config.h"

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

  // iCal URL for calendar tab (Google/Outlook/Apple secret feed URL)
  char icalUrl[256] = {};

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
