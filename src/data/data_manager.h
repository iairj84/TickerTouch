#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// ── Shared data structures ────────────────────────────────────────────────────

struct ScoreEntry {
  char homeTeam[16];    // abbreviation e.g. "NYY"
  char awayTeam[16];
  char homeName[32];    // full name e.g. "New York Yankees"
  char awayName[32];
  char homeScore[8];
  char awayScore[8];
  char status[32];
  char league[8];
  uint32_t homeColor;
  uint32_t awayColor;
  char conference[32];
  uint8_t homeRank;
  uint8_t awayRank;
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

#define MAX_CAL_EVENTS 50   // enough for a week of events

struct CalendarEvent {
  char title[64];
  char timeStr[16];   // "9:00 AM" or "All Day"
  bool allDay;
  int  startMinute;   // minutes from midnight for sorting (0-1439), 9999=all-day
  int  dayOffset;     // 0=today, 1=tomorrow, 2=day after, etc.
};

extern ScoreEntry  scores[MAX_SCORES];
extern uint8_t     scoreCount;

extern StockEntry  stocks[MAX_STOCKS];
extern uint8_t     stockCount;

extern CryptoEntry cryptos[MAX_CRYPTO];
extern uint8_t     cryptoCount;

extern WeatherData weather;          // primary city alias = weatherCities[0]
extern WeatherData weatherCities[3]; // up to 3 cities
extern uint8_t     weatherCityCount;
extern bool        weatherReady;
extern CalendarEvent calEvents[MAX_CAL_EVENTS];
extern uint8_t       calCount;
extern bool          calReady;

// ── Calendar parse trace (shown on /cal) ─────────────────────────────────────
// The last few VEVENT blocks exactly as the feed sent them. A missing or phantom
// entry can only be diagnosed from this: the feed URL exists only on the device.
struct CalDebugEvent {
  char title[40];
  char dtstart[24];
  char recid[24];    // RECURRENCE-ID: the date an edited instance came from
  char status[12];   // CANCELLED, TENTATIVE, CONFIRMED
  bool hasRule;      // the block carries an RRULE
};

int calDebugCount();
const CalDebugEvent *calDebugAt(int i);

extern bool        scoresReady;
extern bool        stocksReady;
extern bool        cryptoReady;

// ── Fetch health ─────────────────────────────────────────────────────────────
// Each feed records when it was last attempted and when it last actually brought
// data back. Without this a feed that has been failing for an hour still shows
// yesterday's numbers on screen looking live; /diag turns that into an age.
enum FeedId { FEED_WEATHER, FEED_SCORES, FEED_STOCKS, FEED_CRYPTO, FEED_RACING, FEED_CAL, FEED_COUNT };

struct FeedHealth {
  uint32_t tryMs;    // millis() of the last attempt, 0 = never tried
  uint32_t okMs;     // millis() when data last arrived, 0 = never succeeded
  uint16_t fails;    // consecutive attempts that returned nothing
};

extern FeedHealth feedHealth[FEED_COUNT];

const char *feedName(uint8_t feed);
void noteTry(uint8_t feed);
void noteOk(uint8_t feed);
void noteFail(uint8_t feed);

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

// Motorsport + Golf entries — kept outside DataManager namespace like other structs
#define MAX_RACES 5

struct RaceEntry {
  char series[8];   // "NASCAR", "F1", "IRL", "PGA"
  char event[48];   // "Daytona 500", "Monaco GP", "Masters"
  char status[32];  // "Race Day", "Qualifying", "Round 2", "Final"
  char leader[32];  // leading driver or golfer name
  char detail[32];  // "Lap 45/200", "-12 thru 14", "P1"
};

namespace DataManager {
  extern RaceEntry  races[MAX_RACES];
  extern uint8_t    raceCount;
  extern bool       racesReady;
  void fetchRacing();
}
