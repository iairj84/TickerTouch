#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// ── Shared data structures ────────────────────────────────────────────────────

struct ScoreEntry {
  char homeTeam[16];
  char awayTeam[16];
  char homeScore[8];
  char awayScore[8];
  char status[32];       // "Q4 2:34", "Final", "4/16 - 10:00 PM EDT"
  char league[8];        // "NFL", "NBA"
  uint32_t homeColor;    // team brand color as 0xRRGGBB
  uint32_t awayColor;
};

struct StockEntry {
  char  symbol[12];
  float price;
  float changePercent;
  bool  marketClosed;  // true = showing last close price, not live
};

struct CryptoEntry {
  char  id[24];
  char  symbol[8];
  float priceUSD;
  float change24h;
};

struct ForecastDay {
  float highF;
  float lowF;
  int   code;
  int   precipPct;
};

struct WeatherData {
  float tempF;
  float feelsLike;
  float highF;
  float lowF;
  float windMph;
  int   humidity;
  int   weatherCode;
  char  city[64];
  char  description[32];
  ForecastDay forecast[3]; // next 3 days
};

// ── Feed storage (simple fixed arrays) ────────────────────────────────────────
#define MAX_SCORES  32
#define MAX_STOCKS  20
#define MAX_CRYPTO  10

namespace DataManager {

#define MAX_CAL_EVENTS 20

struct CalendarEvent {
  char title[64];
  char timeStr[16];   // "9:00 AM" or "All Day"
  bool allDay;
  int  startMinute;   // minutes from midnight for sorting (0-1439), 9999=all-day
};

extern ScoreEntry  scores[MAX_SCORES];
extern uint8_t     scoreCount;

extern StockEntry  stocks[MAX_STOCKS];
extern uint8_t     stockCount;

extern CryptoEntry cryptos[MAX_CRYPTO];
extern uint8_t     cryptoCount;

extern WeatherData weather;

extern CalendarEvent calEvents[MAX_CAL_EVENTS];
extern uint8_t       calCount;
extern bool          calReady;

extern bool        weatherReady;
extern bool        scoresReady;
extern bool        stocksReady;
extern bool        cryptoReady;

void begin();
void tick();
void fetchSports();
void fetchStocks();
void fetchCrypto();
void fetchWeather();
void fetchCalendar();
void fetchTimezone();
void geocodeCity();
void forceRefresh();
const char* buildTickerString();

} // namespace DataManager
