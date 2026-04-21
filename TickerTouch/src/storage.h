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

  // Weather location
  char   city[64]   = "New York";
  char   state[32]  = {};       // state/region for geocoding disambiguation
  float  lat        = 0.0f;     // 0 = not geocoded yet
  float  lon        = 0.0f;

  // Ticker speed 1–5 (maps to px/tick)
  uint8_t tickerSpeed = 2;

  // 24-hour clock
  bool clock24h = false;

  // Local UTC offset in seconds (set from ip-api on boot, used to convert game times)
  int32_t tzOffsetSec = 0;

  // iCal URL for calendar tab (Google/Outlook/Apple secret feed URL)
  char icalUrl[256] = {};

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
