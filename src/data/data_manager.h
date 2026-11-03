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
#define MAX_SCORES  64        // a busy multi-league day runs 30-40 games
#define MAX_STOCKS  20
#define MAX_CRYPTO  10

namespace DataManager {

#define MAX_CAL_EVENTS 80   // two feeds share this: a week of events each

// A row with no known end — an all-day entry, or one that runs past midnight —
// is never treated as finished. Only a timed event carrying DTEND or DURATION
// can be retired while its day is still running.
constexpr int CAL_NO_END = 9999;

struct CalendarEvent {
  char title[64];
  char timeStr[16];   // "9:00 AM" or "All Day"
  bool allDay;
  uint8_t calIdx;     // 0 = first feed, 1 = second (sits in the struct's padding)
  int  startMinute;   // minutes from midnight for sorting (0-1439), 9999=all-day
  int  endMinute;     // minutes from midnight when it is over, CAL_NO_END = unknown
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

// How many of today's rows have already been retired. Once they are pruned an
// empty calendar no longer means "nothing on today" — it may mean "nothing
// left today", and the screens have to say which.
extern uint8_t       calFinishedToday;

// Drop the rows that have already finished. Safe to call at any time: it does
// nothing before the clock is set, and never removes all-day entries.
void pruneEndedCalEvents();

// ── Calendar parse trace (shown on /cal) ─────────────────────────────────────
// The last few VEVENT blocks exactly as the feed sent them. A missing or phantom
// entry can only be diagnosed from this: the feed URL exists only on the device.
struct CalDebugEvent {
  char title[40];
  char dtstart[24];
  char recid[24];    // RECURRENCE-ID: the date an edited instance came from
  char status[12];   // CANCELLED, TENTATIVE, CONFIRMED
  char tzid[40];     // the zone DTSTART was written in, if the feed named one
  bool hasRule;      // the block carries an RRULE
  uint8_t feed;      // 0 = first calendar, 1 = second
};

int calDebugCount();
const CalDebugEvent *calDebugAt(int i);

// Every block that carried a repeat rule, with the rule text. A monthly series
// whose master sits months back never appears in the last-N trace, and the rule
// is what decides whether an occurrence lands on a given day.
struct CalSeriesTrace {
  char title[40];
  char dtstart[24];
  char rule[72];
  char exdate[72];   // dates the series explicitly skips
  uint8_t feed;      // 0 = first calendar, 1 = second
};

int calSeriesCount();
const CalSeriesTrace *calSeriesAt(int i);

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

// What the clock is actually running on. Surfaced on /diag: "an hour off" is
// almost always a zone that is not the one you think, and the page has to be
// able to say which zone it picked.
const char *tzZoneName();
int32_t     tzOffsetNow();
void geocodeCity();
void forceRefresh();

// ── On-demand refresh ────────────────────────────────────────────────────────
// The settings page's "Fetch now" buttons only queue a bit; the fetch itself
// runs on the data task, because an HTTPS request needs far more stack than the
// 12KB web task has. feedReqDoneMs() is the millis() the queued fetch returned,
// which is how the page knows its answer has arrived and is fresh.
bool     requestFeed(uint8_t feed);
uint32_t feedReqDoneMs(uint8_t feed);

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
