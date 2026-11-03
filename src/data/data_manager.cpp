/**
 * data_manager.cpp
 */
// config.h defines ARDUINOJSON_DEFAULT_NESTING_LIMIT 50 — MUST come before ArduinoJson
#include "../../config.h"
#include "data_manager.h"
#include <strings.h>
#include "../storage.h"
#include "../wifi_manager.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>   // heap_caps_malloc/free for DRAM-forced allocation
#include <esp_task_wdt.h>    // esp_task_wdt_reset()

namespace DataManager {

ScoreEntry  scores[MAX_SCORES]; uint8_t scoreCount  = 0;
StockEntry  stocks[MAX_STOCKS];  uint8_t stockCount  = 0;
CryptoEntry cryptos[MAX_CRYPTO]; uint8_t cryptoCount = 0;
WeatherData weather{};
WeatherData weatherCities[3]{};
uint8_t     weatherCityCount = 1;
RaceEntry   races[MAX_RACES]{};
uint8_t     raceCount = 0;
bool        racesReady = false;
CalendarEvent calEvents[MAX_CAL_EVENTS]; uint8_t calCount = 0;
bool weatherReady=false, scoresReady=false, stocksReady=false, cryptoReady=false, calReady=false;

FeedHealth feedHealth[FEED_COUNT] = {};

const char *feedName(uint8_t feed) {
  static const char *names[FEED_COUNT] = {
    "weather", "scores", "stocks", "crypto", "racing", "calendar"
  };
  return feed < FEED_COUNT ? names[feed] : "?";
}
void noteTry(uint8_t feed) {
  if (feed < FEED_COUNT) feedHealth[feed].tryMs = millis();
}
void noteOk(uint8_t feed) {
  if (feed < FEED_COUNT) { feedHealth[feed].okMs = millis(); feedHealth[feed].fails = 0; }
}
void noteFail(uint8_t feed) {
  if (feed < FEED_COUNT && feedHealth[feed].fails < 0xFFFF) feedHealth[feed].fails++;
}

static uint32_t lastSports=0, lastStocks=0, lastWeather=0, lastCrypto=0, lastCalendar=0, lastRacing=0;
#define REFRESH_RACING_MS   300000  // 5 min — races don't change that fast
#define REFRESH_CALENDAR_MS   900000  // 15 min

// ── HTTP GET ──────────────────────────────────────────────────────────────────
// Reads response into caller-supplied DRAM buffer.
// CRITICAL: Never touches PSRAM. Uses write-to-stream approach to avoid
// getString() which returns a PSRAM String pointer that crashes on memcpy.
static int httpGET_buf(const char *url, char *buf, int bufSize, int ms=10000) {
  if (!WiFiManager::isConnected()) return 0;
  HTTPClient http;
  WiFiClient plain;
  WiFiClientSecure *tls = nullptr;
  bool ok = false;

  if (strncmp(url, "https", 5) == 0) {
    tls = new (std::nothrow) WiFiClientSecure();
    if (!tls) { Serial.println(F("[HTTP] TLS alloc fail")); return 0; }
    tls->setInsecure();
    ok = http.begin(*tls, url);
  } else {
    ok = http.begin(plain, url);
  }
  if (!ok) { delete tls; return 0; }
  http.setTimeout(ms);
  http.setReuse(false);
  http.useHTTP10(true);  // disables chunked transfer — body is clean bytes
  http.addHeader("User-Agent", "TickerTouch/1.0");
  esp_task_wdt_reset();   // GET() can block for the full timeout
  int code = http.GET();
  int len = 0;
  if (code == 200) {
    // read() returns int (safe — no PSRAM pointer dereference).
    // Tight inner loop reads up to 128 bytes before yielding.
    WiFiClient *stream = http.getStreamPtr();
    uint32_t deadline = millis() + ms;
    while (len < bufSize - 1 && millis() < deadline) {
      // Slow servers can dribble bytes for the whole timeout budget. Without
      // this the subscribed main task trips the 60s TWDT and panics.
      esp_task_wdt_reset();
      int got = 0;
      while (len < bufSize - 1 && got < 128) {
        int c = stream->read();
        if (c < 0) break;
        buf[len++] = (char)c;
        got++;
      }
      if (got == 0) {
        if (!stream->connected()) break;
        vTaskDelay(1);
      }
    }
    buf[len] = '\0';
  } else {
    Serial.printf("[HTTP] %d <- %.60s\n", code, url);
    buf[0] = '\0';
  }
  http.end();
  if (tls) tls->stop();
  else plain.stop();
  delete tls;
  return len;
}


static const char* wmoDesc(int c) {
  if(c==0)  return "Clear";      if(c<=2)  return "Partly Cloudy";
  if(c==3)  return "Overcast";   if(c<=49) return "Foggy";
  if(c<=59) return "Drizzle";    if(c<=69) return "Rain";
  if(c<=79) return "Snow";       if(c<=82) return "Showers";
  if(c<=99) return "Thunderstorm"; return "";
}

// ── Timezone + NTP ─────────────────────────────────────────────────────────────
void fetchTimezone() {
  char tzBuf[128] = {};
  int len = httpGET_buf("http://ip-api.com/json/?fields=offset", tzBuf, sizeof(tzBuf), 5000);
  int32_t offset = 0;
  if (len > 0) {
    StaticJsonDocument<128> doc;
    if (!deserializeJson(doc, tzBuf)) {
      offset = doc["offset"]|0;
      Storage::cfg.tzOffsetSec = offset; // save for game time conversion
    }
  }
  Serial.printf("[TZ] offset=%d sec\n", offset);
  configTime(offset, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print(F("[NTP]"));
  for (int i=0; i<40 && time(nullptr)<100000; i++) { delay(250); Serial.print('.'); }
  time_t now=time(nullptr);
  if (now>100000) { char b[24]; strftime(b,sizeof(b),"%H:%M:%S",localtime(&now)); Serial.printf(" OK: %s\n",b); }
  else Serial.println(F(" FAILED"));
}

// ── Geocode ─────────────────────────────────────────────────────────────────────
// ── Geocode ─────────────────────────────────────────────────────────────────────
void geocodeCity() {
  if (!strlen(Storage::cfg.city)) return;
  if (Storage::cfg.lat != 0.0f && Storage::cfg.lon != 0.0f) {
    Serial.printf("[Geo] saved: %s %.4f, %.4f\n",
      Storage::cfg.city, Storage::cfg.lat, Storage::cfg.lon);
    return;
  }

  // Strip trailing ", OR" state suffix if present in city field
  char cityOnly[64];
  strlcpy(cityOnly, Storage::cfg.city, sizeof(cityOnly));
  char *comma = strchr(cityOnly, ',');
  if (comma) *comma = '\0';

  // Build query: "Tigard Oregon" or just "Tigard"
  char query[128] = {};
  if (strlen(Storage::cfg.state))
    snprintf(query, sizeof(query), "%s %s", cityOnly, Storage::cfg.state);
  else
    strlcpy(query, cityOnly, sizeof(query));

  // URL-encode spaces
  char enc[256] = {};
  int j = 0;
  for (int i = 0; query[i] && j < 250; i++) {
    if (query[i] == ' ') enc[j++] = '+';
    else enc[j++] = query[i];
  }
  enc[j] = '\0';

  Serial.printf("[Geo] searching '%s'\n", query);

  // ── Try 1: Open-Meteo plain HTTP ─────────────────────────────────────────
  {
    char url[280];
    snprintf(url, sizeof(url),
      "http://geocoding-api.open-meteo.com/v1/search"
      "?name=%s&count=1&language=en&format=json", enc);
    char geoBuf[1024];
    int geoLen = httpGET_buf(url, geoBuf, sizeof(geoBuf), 8000);
    if (geoLen > 0) {
      StaticJsonDocument<2048> doc;
      if (deserializeJson(doc, geoBuf) == DeserializationError::Ok) {
        JsonVariant r = doc["results"][0];
        if (!r.isNull()) {
          float lat = r["latitude"]  | 0.0f;
          float lon = r["longitude"] | 0.0f;
          if (lat != 0.0f || lon != 0.0f) {
            const char *n = r["name"] | cityOnly;
            const char *a = r["admin1"] | (const char*)"";
            char city[64];
            if (a && strlen(a))
              snprintf(city, sizeof(city), "%s, %s", n, a);
            else
              strlcpy(city, n, sizeof(city));
            Storage::cfg.lat = lat;
            Storage::cfg.lon = lon;
            strlcpy(Storage::cfg.city, city, sizeof(Storage::cfg.city));
            Storage::save();
            Serial.printf("[Geo] OpenMeteo OK: %s %.4f, %.4f\n", city, lat, lon);
            return;
          }
        }
      }
    }
  }

  // ── Try 2: Nominatim HTTPS (requires working TLS) ────────────────────────
  {
    char url[280];
    snprintf(url, sizeof(url),
      "https://nominatim.openstreetmap.org/search"
      "?q=%s&format=json&limit=1&countrycodes=us,ca,gb,au,nz", enc);
    char geoBuf[1024];
    int geoLen = httpGET_buf(url, geoBuf, sizeof(geoBuf), 10000);
    if (geoLen > 0 && geoBuf[0] == '[') {
      StaticJsonDocument<2048> doc;
      if (deserializeJson(doc, geoBuf) == DeserializationError::Ok && doc.size() > 0) {
        float lat = atof(doc[0]["lat"] | "0");
        float lon = atof(doc[0]["lon"] | "0");
        if (lat != 0.0f || lon != 0.0f) {
          const char *dn = doc[0]["display_name"] | cityOnly;
          char city[64];
          strlcpy(city, dn, sizeof(city));
          char *cm = strchr(city, ',');
          if (cm) *cm = '\0';
          Storage::cfg.lat = lat;
          Storage::cfg.lon = lon;
          strlcpy(Storage::cfg.city, city, sizeof(Storage::cfg.city));
          Storage::save();
          Serial.printf("[Geo] Nominatim OK: %s %.4f, %.4f\n", city, lat, lon);
          return;
        }
      }
    }
  }
  Serial.printf("[Geo] FAILED for '%s'\n", query);
}

// ── Sports helpers ─────────────────────────────────────────────────────────────
// Copy chars from src into dst until a closing quote, end of string, or dstLen-1
static void copyUntilQuote(char *dst, int dstLen, const char *src) {
  if (!src || !dst || dstLen <= 0) return;
  int i = 0;
  while (i < dstLen - 1 && src[i] && src[i] != '"') {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}


// Convert a game time string like "6:45 PM EDT" to local time.
// EDT = UTC-4, EST = UTC-5. We use the device's known UTC offset.
static void convertGameTime(char *status, int statusLen) {
  // Only convert if it looks like "H:MM PM EDT" or "H:MM AM EDT/EST/PDT/PST etc"
  const char *pmAm = strstr(status, " PM ");
  if (!pmAm) pmAm = strstr(status, " AM ");
  if (!pmAm) return; // not a time string

  // Parse hours and minutes
  int h = 0, m = 0;
  if (sscanf(status, "%d:%d", &h, &m) != 2) return;
  bool isPM = (strstr(status, " PM ") != nullptr);
  if (isPM && h != 12) h += 12;
  if (!isPM && h == 12) h = 0;

  // Source timezone — detect from suffix
  int srcOffset = -4 * 3600; // default EDT
  if (strstr(status, "EST")) srcOffset = -5 * 3600;
  else if (strstr(status, "CDT")) srcOffset = -5 * 3600;
  else if (strstr(status, "CST")) srcOffset = -6 * 3600;
  else if (strstr(status, "MDT")) srcOffset = -6 * 3600;
  else if (strstr(status, "MST")) srcOffset = -7 * 3600;
  else if (strstr(status, "PDT")) srcOffset = -7 * 3600;
  else if (strstr(status, "PST")) srcOffset = -8 * 3600;

  int localOffset = Storage::cfg.tzOffsetSec;
  if (localOffset == 0) return; // not yet fetched

  // Convert: UTC time = game_time - srcOffset; local = UTC + localOffset
  int totalMins = h * 60 + m;
  int utcMins = totalMins - (srcOffset / 60);
  int localMins = utcMins + (localOffset / 60);
  // Normalize to 0-1439
  localMins = ((localMins % 1440) + 1440) % 1440;

  int lh = localMins / 60, lm = localMins % 60;
  bool lPM = (lh >= 12);
  if (lh > 12) lh -= 12;
  if (lh == 0) lh = 12;
  snprintf(status, statusLen, "%d:%02d %s", lh, lm, lPM ? "PM" : "AM");
}


struct LG { const char *path; const char *abbr; uint16_t bit; };

static const LG LGS[] = {
  {"football/nfl",                        "NFL",  1<<0},
  {"baseball/mlb",                        "MLB",  1<<3},
  {"basketball/nba",                      "NBA",  1<<1},
  {"football/college-football",           "CFB",  1<<4},
  {"basketball/mens-college-basketball",  "CBB",  1<<7},
  {"hockey/nhl",                          "NHL",  1<<2},
  {"soccer/usa.1",                        "MLS",  1<<5},
  {"soccer/eng.1",                        "EPL",  1<<6},
  {"basketball/wnba",                     "WNBA", 1<<8},
};

// Forward declaration for sports stream fill
static int sportsFill(WiFiClient *stream, char *buf, int &bLen, int BSIZ, uint32_t deadline) {
  if (bLen > BSIZ * 3/4) {
    int keep = BSIZ/2;
    memmove(buf, buf + bLen - keep, keep + 1);
    bLen = keep;
    return 1;
  }
  // Read up to 512 bytes per call using safe single-byte read() calls.
  // read() returns int (not a pointer), so no PSRAM memcpy involved.
  int added = 0;
  uint32_t t = millis() + 150;
  while (millis() < t && millis() < deadline && added < 512) {
    int c = stream->read();
    if (c >= 0) {
      if (bLen < BSIZ - 1) { buf[bLen++] = (char)c; buf[bLen] = 0; }
      added++;
    } else if (!stream->connected()) {
      return added > 0 ? 1 : 0;
    } else {
      if (added > 0) break; // got some data, return it
      vTaskDelay(1);
    }
  }
  if (added > 0) { vTaskDelay(1); return 1; } // yield after bulk read
  return 0;
}

void fetchSports() {
  noteTry(FEED_SCORES);
  // Use a staging counter so display sees complete old data until new fetch fully done
  uint8_t stagingCount = 0;
  uint16_t mask = Storage::cfg.sportsLeagues;
  Serial.printf("[Sports] mask=0x%04X\n", mask);
  if (!mask) { scoreCount = 0; noteOk(FEED_SCORES); scoresReady = true; return; }

  const int BSIZ = 16384;
  char *buf = (char*)heap_caps_malloc(BSIZ, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!buf) {
    Serial.println(F("[Sports] alloc fail"));
    noteFail(FEED_SCORES);
    scoresReady = true; return;
  }
  // Sanity check: DRAM addresses are 0x3fc00000-0x3fffffff on ESP32-S3
  if ((uint32_t)buf > 0x40000000) {
    Serial.printf("[Sports] BUF IN PSRAM: %p — aborting\n", buf);
    heap_caps_free(buf);
    noteFail(FEED_SCORES);
    scoresReady = true; return;
  }

  for (auto &lg : LGS) {
    if (!(mask & lg.bit)) continue;

    esp_task_wdt_reset(); // reset watchdog — each league can take several seconds

    time_t now_t = time(nullptr);
    struct tm *lt = localtime(&now_t);
    char url[200];
    snprintf(url, sizeof(url),
      "http://site.api.espn.com/apis/site/v2/sports/%s/scoreboard"
      "?dates=%04d%02d%02d&limit=25",
      lg.path, lt->tm_year+1900, lt->tm_mon+1, lt->tm_mday);

    Serial.printf("[Sports] %s\n", lg.abbr);
    if (!WiFiManager::isConnected()) continue;

    HTTPClient http;
    WiFiClient client;
    // NOTE: do NOT free buf here — it is reused by the next league and freed once
    // after the loop. Freeing it here was a use-after-free + double-free.
    if (!http.begin(client, url)) { Serial.printf("[Sports] begin failed %s\n", lg.abbr); continue; }
    http.setTimeout(8000);  // reduced from 10000
    http.useHTTP10(true);    // disables chunked transfer — body is clean bytes
    http.addHeader("User-Agent", "TickerTouch/1.0");
    int code = http.GET();
    if (code != 200) {
      Serial.printf("[HTTP] %d %s\n", code, lg.abbr);
      http.end();
      client.stop();
      vTaskDelay(pdMS_TO_TICKS(200));  // reduced from 500
      continue;
    }

    WiFiClient *stream = http.getStreamPtr();
    int bLen = 0;
    int n = 0;
    buf[0] = 0;
    uint32_t deadline = millis() + 8000;  // reduced from 12000

    // Initial fill — read first chunk quickly
    for (int i = 0; i < 30 && bLen < 200; i++) {
      sportsFill(stream, buf, bLen, BSIZ, deadline);
      if (!stream->connected() && bLen == 0) break;
    }

    while (millis() < deadline && stagingCount < MAX_SCORES) {
      esp_task_wdt_reset(); // reset per game parsed — prevents WDT on large leagues
      // Ensure buffer has data
      if (bLen < 200) {
        sportsFill(stream, buf, bLen, BSIZ, deadline);
        if (bLen < 50 && !stream->connected()) break;
        continue;
      }

      // Find next competitors array
      char *cp = strstr(buf, "\"competitors\":[");
      if (!cp) {
        sportsFill(stream, buf, bLen, BSIZ, deadline);
        continue;
      }

      // The two team objects that follow "competitors":[ are large: a basketball
      // scoreboard puts over 11KB between that marker and the closing brace of the
      // second team. With the block at an arbitrary offset the 16KB buffer could
      // never hold it, so every NBA game was dropped while smaller leagues parsed.
      // Slide the marker to the front first so the whole buffer is available.
      int cpOff = (int)(cp - buf);
      if (cpOff > 0) {
        memmove(buf, cp, bLen - cpOff + 1);
        bLen -= cpOff;
        cp = buf;
        cpOff = 0;
      }
      while (bLen < 12500 && millis() < deadline) {
        const int oldLen = bLen;
        sportsFill(stream, buf, bLen, BSIZ, deadline);
        if (!stream->connected() && bLen == oldLen) break;
      }

      // Parse two competitor objects
      char homeTeam[8]="?", awayTeam[8]="?";
      char homeName[32]={}, awayName[32]={};
      char homeScore[8]="0", awayScore[8]="0";
      char homeConf[32]={}, awayConf[32]={};
      uint32_t homeColor=0x444444, awayColor=0x444444;
      uint8_t homeRank=0, awayRank=0;
      int found = 0;
      char *p = cp + 15;

      while (found < 2 && p < buf + bLen) {
        while (p < buf+bLen && *p != '{') p++;
        if (p >= buf+bLen) break;
        char *os = p;
        int dep = 1; p++;
        while (p < buf+bLen && dep > 0) {
          if (*p=='{') dep++; else if (*p=='}') dep--;
          p++;
        }
        char *oe = p;
        if (oe > buf+bLen) break;

        char sv = *oe; *oe = '\0';

        char ha[8]={}, sc[8]="0", ab[8]="?", conf[32]={}, dn[32]={};
        uint32_t col = 0x444444;
        uint8_t rank = 0;

        char *hap = strstr(os, "\"homeAway\":\"");
        if (hap) copyUntilQuote(ha, sizeof(ha), hap+12);

        char *scp = strstr(os, "\"score\":\"");
        if (scp) copyUntilQuote(sc, sizeof(sc), scp+9);

        // Rank from "curatedRank":{"current":N}
        char *rkp = strstr(os, "\"curatedRank\":{");
        if (rkp) {
          char *rcp = strstr(rkp, "\"current\":");
          if (rcp) { int r = atoi(rcp+10); if (r>0 && r<=25) rank=(uint8_t)r; }
        }

        // Only look for abbreviation and conference inside "team":{ block
        char *tp = strstr(os, "\"team\":{");
        if (tp && tp < oe) {
          char *te = tp+8; int td=1;
          while (te<oe && td>0) { if(*te=='{')td++; else if(*te=='}')td--; te++; }
          char tsv=*te; *te='\0';
          char *ap=strstr(tp,"\"abbreviation\":\"");
          if (ap) copyUntilQuote(ab,sizeof(ab),ap+16);
          char *dnp=strstr(tp,"\"displayName\":\"");
          if (dnp) copyUntilQuote(dn,sizeof(dn),dnp+15);
          char *clp=strstr(tp,"\"color\":\"");
          if (clp) { char cs[8]={}; copyUntilQuote(cs,sizeof(cs),clp+9);
            if(strlen(cs)==6) col=(uint32_t)strtoul(cs,nullptr,16); }
          // Conference short name
          char *cfp=strstr(tp,"\"shortName\":\"");
          if (cfp) copyUntilQuote(conf,sizeof(conf),cfp+13);
          *te=tsv;
        }
        *oe = sv;

        if (!strcmp(ha,"home")) {
          strlcpy(homeTeam,ab,sizeof(homeTeam));
          strlcpy(homeScore,sc,sizeof(homeScore));
          homeColor=col; homeRank=rank;
          if (dn[0])   strlcpy(homeName,dn,sizeof(homeName));
          if (conf[0] && !homeConf[0]) strlcpy(homeConf,conf,sizeof(homeConf));
          found++;
        } else if (!strcmp(ha,"away")) {
          strlcpy(awayTeam,ab,sizeof(awayTeam));
          strlcpy(awayScore,sc,sizeof(awayScore));
          awayColor=col; awayRank=rank;
          if (dn[0])   strlcpy(awayName,dn,sizeof(awayName));
          if (conf[0] && !awayConf[0]) strlcpy(awayConf,conf,sizeof(awayConf));
          found++;
        }
      }
      // p now points past end of competitors array in buf
      int afterCompOff = (p >= buf && p <= buf+bLen) ? (int)(p - buf) : bLen;
      char status[32] = {};

      // Status is in "notes":[],"status":{...,"shortDetail":"...",...}
      // It comes right after competitors — refill until we find it
      for (int attempt = 0; attempt < 20 && millis() < deadline; attempt++) {
        // Search from afterCompOff (adjusted for any slides)
        if (afterCompOff < bLen) {
          char *sdp = strstr(buf + afterCompOff, "\"shortDetail\":\"");
          if (sdp) {
            char *valStart = sdp + 15;
            // Find the closing quote — if not in buffer yet, refill first
            char *valEnd = strchr(valStart, '"');
            if (!valEnd) {
              // Closing quote not in buffer — refill and retry
              int oldBLen = bLen;
              sportsFill(stream, buf, bLen, BSIZ, deadline);
              if (bLen < oldBLen) {
                int removed = oldBLen - bLen;
                afterCompOff = (afterCompOff > removed) ? afterCompOff - removed : 0;
              }
              continue;
            }
            // Validate: closing quote must be within 32 chars (sane status length)
            if (valEnd - valStart > 30) { afterCompOff = (int)(valEnd - buf); continue; }
            char raw[48]={};
            copyUntilQuote(raw, sizeof(raw), valStart);
            // Reject obviously truncated values (no space = probably cut off)
            if (strlen(raw) < 2) { afterCompOff = (int)(valEnd - buf); continue; }
            const char *dsh = strstr(raw, " - ");
            strlcpy(status, dsh ? dsh+3 : raw, sizeof(status));
            break;
          }
        }
        // Not found yet — refill
        int oldBLen = bLen;
        sportsFill(stream, buf, bLen, BSIZ, deadline);
        if (bLen < oldBLen) {
          // Buffer slid — adjust offset
          int removed = oldBLen - bLen;
          afterCompOff = (afterCompOff > removed) ? afterCompOff - removed : 0;
        }
        if (bLen == oldBLen && !stream->connected()) break;
      }

      if (found == 2) {
        // Fill in status fallback if empty
        if (!status[0]) {
          if (strcmp(homeScore,"0")!=0 || strcmp(awayScore,"0")!=0)
            strlcpy(status, "Live", sizeof(status));
        }
        // Convert scheduled game times (e.g. "6:45 PM EDT") to local time
        convertGameTime(status, sizeof(status));
        ScoreEntry &e = scores[stagingCount++];
        strlcpy(e.league,    lg.abbr,   sizeof(e.league));
        strlcpy(e.homeTeam,  homeTeam,  sizeof(e.homeTeam));
        strlcpy(e.awayTeam,  awayTeam,  sizeof(e.awayTeam));
        strlcpy(e.homeName,  homeName[0] ? homeName : homeTeam, sizeof(e.homeName));
        strlcpy(e.awayName,  awayName[0] ? awayName : awayTeam, sizeof(e.awayName));
        strlcpy(e.homeScore, homeScore, sizeof(e.homeScore));
        strlcpy(e.awayScore, awayScore, sizeof(e.awayScore));
        strlcpy(e.status,    status,    sizeof(e.status));
        e.homeColor=homeColor; e.awayColor=awayColor;
        e.homeRank=homeRank;   e.awayRank=awayRank;
        // Conference: prefer home team's conf, fallback to away
        if (homeConf[0]) strlcpy(e.conference, homeConf, sizeof(e.conference));
        else             strlcpy(e.conference, awayConf, sizeof(e.conference));
        Serial.printf("[Sports] %s: %s %s-%s %s (%s)\n",
          lg.abbr,e.awayTeam,e.awayScore,e.homeScore,e.homeTeam,e.status);
        n++;
      }

      // Advance past what we just parsed — use afterCompOff as our position
      int adv = afterCompOff;
      if (adv < 1) adv = 50;
      if (adv > bLen) adv = bLen;
      memmove(buf, buf+adv, bLen-adv+1);
      bLen -= adv;
    }

    http.end();
    client.stop();
    buf[0] = 0; // reset for next league — single allocation reused
    Serial.printf("[Sports] %s: %d games\n", lg.abbr, n);
    vTaskDelay(pdMS_TO_TICKS(200)); // let TCP stack release the socket
  }

  heap_caps_free(buf); // free once at end

  // Atomically commit new scores — display sees complete old set until this point
  scoreCount = stagingCount;

  // Sort within each league: live → final → scheduled
  // Do this per-league to preserve league grouping in the UI
  auto gameOrder = [](const ScoreEntry &e) -> int {
    const char *s = e.status;
    if (!s[0]) return 0;
    if (strstr(s,"Final") || strstr(s,"F/")) return 1;
    if (strstr(s,"PM") || strstr(s,"AM")) return 2;
    return 0; // in-progress
  };

  // Find league boundaries and sort within each
  int i = 0;
  while (i < scoreCount) {
    int j = i + 1;
    while (j < scoreCount && strcmp(scores[j].league, scores[i].league) == 0) j++;
    // Insertion sort scores[i..j-1]
    for (int a = i+1; a < j; a++) {
      ScoreEntry key = scores[a];
      int b = a - 1;
      while (b >= i && gameOrder(scores[b]) > gameOrder(key)) {
        scores[b+1] = scores[b]; b--;
      }
      scores[b+1] = key;
    }
    i = j;
  }

  noteOk(FEED_SCORES);
  scoresReady = true;
  Serial.printf("[Sports] total=%d\n", scoreCount);
}

// ── Stocks via Yahoo Finance (no API key required) ────────────────────────────
void fetchStocks() {
  noteTry(FEED_STOCKS);
  if (!strlen(Storage::cfg.stocks)) { stockCount = 0; noteOk(FEED_STOCKS); stocksReady = true; return; }

  // Yahoo Finance v8 chart API — free, no key, returns current price + day change.
  // NOTE: the "meta" block is ~630 bytes, so the buffer must be bigger than that
  // or "chartPreviousClose" gets truncated (that's what zeroed every % change).
  // Response: {"chart":{"result":[{"meta":{"regularMarketPrice":X,"regularMarketChangePercent":Y,...}}]}}
  char stockBuf[1024];

  // Stage into a temp array and swap at the end: clearing stockCount/stocksReady
  // mid-fetch blanked the cards and made them pop back in a few seconds later.
  StockEntry staging[MAX_STOCKS];
  uint8_t stagingCount = 0;

  char syms[sizeof(Storage::cfg.stocks)];
  strlcpy(syms, Storage::cfg.stocks, sizeof(syms));

  char *start = syms;
  while (*start && stagingCount < MAX_STOCKS) {
    esp_task_wdt_reset();
    char *comma = strchr(start, ',');
    if (comma) *comma = '\0';

    char sym[16] = {};
    int si = 0;
    for (char *c = start; *c && si < (int)sizeof(sym)-1; c++)
      if (*c != ' ') sym[si++] = toupper((unsigned char)*c);
    sym[si] = '\0';

    if (si > 0) {
      char url[200];
      snprintf(url, sizeof(url),
        "https://query1.finance.yahoo.com/v8/finance/chart/%s"
        "?interval=1d&range=1d&includePrePost=false", sym);
      int len = httpGET_buf(url, stockBuf, sizeof(stockBuf), 8000);
      if (len > 0) {
        float price = 0.0f;
        char *pp = strstr(stockBuf, "\"regularMarketPrice\":");
        if (pp) {
          const char *v = strchr(pp, ':');
          if (v) price = atof(v + 1);
        }

        // Day change: prefer Yahoo's own percent (right next to the price),
        // otherwise compute it from the previous close.
        float dp = 0.0f;
        char *pc = strstr(stockBuf, "\"regularMarketChangePercent\":");
        if (pc) {
          const char *v = strchr(pc, ':');
          if (v) dp = atof(v + 1);
        } else {
          float prev = 0.0f;
          char *pv = strstr(stockBuf, "\"chartPreviousClose\":");
          if (!pv) pv = strstr(stockBuf, "\"previousClose\":");
          if (!pv) pv = strstr(stockBuf, "\"regularMarketPreviousClose\":");
          if (pv) {
            const char *v = strchr(pv, ':');
            if (v) prev = atof(v + 1);
          }
          if (prev > 0.01f && price > 0.01f) dp = (price - prev) / prev * 100.0f;
        }

        if (price > 0.01f) {
          StockEntry &e = staging[stagingCount];
          strlcpy(e.symbol, sym, sizeof(e.symbol));
          e.price = price;
          e.changePercent = dp;
          e.marketClosed = false;
          Serial.printf("[Stocks] %s $%.2f %+.2f%%\n", sym, price, dp);
          stagingCount++;
        } else {
          Serial.printf("[Stocks] %s: no price (len=%d)\n", sym, len);
        }
      } else {
        Serial.printf("[Stocks] %s: fetch failed\n", sym);
      }
    }

    if (comma) { *comma = ','; start = comma + 1; } else break;
  }
  // Commit only on success — a failed fetch keeps the previous prices on screen
  if (stagingCount > 0) {
    memcpy(stocks, staging, stagingCount * sizeof(StockEntry));
    stockCount = stagingCount;
  } else if (stockCount > 0) {
    Serial.println(F("[Stocks] no fresh data - keeping previous"));
  }
  if (stagingCount > 0) noteOk(FEED_STOCKS); else noteFail(FEED_STOCKS);
  stocksReady = true;
  Serial.printf("[Stocks] done: %d\n", stockCount);
}

// ── Crypto via CoinGecko (free, no key) ────────────────────────────────────
void fetchCrypto() {
  noteTry(FEED_CRYPTO);
  // Cleared in settings -> drop the cached entries, otherwise the old coins
  // keep rendering forever because the early return left cryptoCount alone.
  if (!strlen(Storage::cfg.crypto)) { cryptoCount = 0; noteOk(FEED_CRYPTO); cryptoReady = true; return; }
  if (!WiFiManager::isConnected())  { noteFail(FEED_CRYPTO); cryptoReady = true; return; }

  char url[256];
  snprintf(url, sizeof(url),
    "https://api.coingecko.com/api/v3/simple/price"
    "?ids=%s&vs_currencies=usd&include_24hr_change=true",
    Storage::cfg.crypto);

  const int CBUF = 1024;
  char *cbuf = (char*)heap_caps_malloc(CBUF, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!cbuf) { noteFail(FEED_CRYPTO); cryptoReady = true; return; }

  int len = httpGET_buf(url, cbuf, CBUF, 8000);

  // Use staging so existing display data isn't wiped during fetch
  CryptoEntry staging[MAX_CRYPTO];
  uint8_t stagingCount = 0;

  if (len > 0) {
    char ids[sizeof(Storage::cfg.crypto)];
    strlcpy(ids, Storage::cfg.crypto, sizeof(ids));
    char *start = ids;
    while (start && *start && stagingCount < MAX_CRYPTO) {
      char *comma = strchr(start, ',');
      if (comma) *comma = '\0';
      while (*start == ' ') start++;
      int si = strlen(start);
      while (si > 0 && start[si-1] == ' ') start[--si] = '\0';

      if (si > 0) {
        char key[32]; snprintf(key, sizeof(key), "\"%s\":{", start);
        char *p = strstr(cbuf, key);
        if (p) {
          float usd = 0, chg = 0;
          char *up = strstr(p, "\"usd\":");
          if (up) usd = atof(up + 6);
          char *cp = strstr(p, "\"usd_24h_change\":");
          if (cp) chg = atof(cp + 17);
          if (usd > 0) {
            strlcpy(staging[stagingCount].id, start, sizeof(staging[0].id));
            char sym[8] = {};
            if      (!strcmp(start,"bitcoin"))  strlcpy(sym,"BTC",sizeof(sym));
            else if (!strcmp(start,"ethereum")) strlcpy(sym,"ETH",sizeof(sym));
            else if (!strcmp(start,"solana"))   strlcpy(sym,"SOL",sizeof(sym));
            else if (!strcmp(start,"dogecoin")) strlcpy(sym,"DOGE",sizeof(sym));
            else if (!strcmp(start,"cardano"))  strlcpy(sym,"ADA",sizeof(sym));
            else if (!strcmp(start,"ripple"))   strlcpy(sym,"XRP",sizeof(sym));
            else { strlcpy(sym, start, 5); for(int i=0;sym[i];i++) sym[i]=toupper(sym[i]); }
            strlcpy(staging[stagingCount].symbol, sym, sizeof(staging[0].symbol));
            staging[stagingCount].priceUSD = usd;
            staging[stagingCount].change24h = chg;
            Serial.printf("[Crypto] %s $%.2f %+.2f%%\n", sym, usd, chg);
            stagingCount++;
          }
        }
      }
      if (comma) { *comma = ','; start = comma + 1; } else break;
    }
  }
  heap_caps_free(cbuf);

  // Only commit if we got data — don't blank on transient fetch failure
  if (stagingCount > 0) {
    memcpy(cryptos, staging, stagingCount * sizeof(CryptoEntry));
    cryptoCount = stagingCount;
  }
  if (stagingCount > 0) noteOk(FEED_CRYPTO); else noteFail(FEED_CRYPTO);
  cryptoReady = true;
  Serial.printf("[Crypto] done: %d\n", cryptoCount);
}

// ── Weather ─────────────────────────────────────────────────────────────────────
// Fetch weather for one city into the given WeatherData slot
static bool fetchWeatherCity(float lat, float lon, const char *cityName, WeatherData &out) {
  char url[400];
  snprintf(url, sizeof(url),
    "http://api.open-meteo.com/v1/forecast"
    "?latitude=%.4f&longitude=%.4f"
    "&current=temperature_2m,weather_code,apparent_temperature,relative_humidity_2m,wind_speed_10m"
    "&daily=temperature_2m_max,temperature_2m_min,weather_code,precipitation_probability_max"
    "&temperature_unit=fahrenheit&wind_speed_unit=mph&forecast_days=4&timezone=auto",
    lat, lon);

  const int WX_BUF = 2048;
  char *wxBuf = (char*)heap_caps_malloc(WX_BUF, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!wxBuf) return false;
  int wxLen = httpGET_buf(url, wxBuf, WX_BUF, 10000);
  if (wxLen == 0) { heap_caps_free(wxBuf); return false; }

  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, wxBuf);
  if (err != DeserializationError::Ok) { heap_caps_free(wxBuf); return false; }

  JsonObject cur = doc["current"];
  if (cur.isNull()) { heap_caps_free(wxBuf); return false; }
  float t = cur["temperature_2m"] | -999.0f;
  if (t < -998.0f) { heap_caps_free(wxBuf); return false; }

  out.tempF       = t;
  out.feelsLike   = cur["apparent_temperature"] | t;
  out.weatherCode = cur["weather_code"] | 0;
  out.humidity    = cur["relative_humidity_2m"] | 0;
  out.windMph     = cur["wind_speed_10m"] | 0.0f;
  out.highF = doc["daily"]["temperature_2m_max"][0] | t;
  out.lowF  = doc["daily"]["temperature_2m_min"][0] | t;
  for (int i = 0; i < 3; i++) {
    out.forecast[i].highF     = doc["daily"]["temperature_2m_max"][i+1] | 0.0f;
    out.forecast[i].lowF      = doc["daily"]["temperature_2m_min"][i+1] | 0.0f;
    out.forecast[i].code      = doc["daily"]["weather_code"][i+1] | 0;
    out.forecast[i].precipPct = doc["daily"]["precipitation_probability_max"][i+1] | 0;
  }
  heap_caps_free(wxBuf);
  strlcpy(out.city, cityName, sizeof(out.city));
  strlcpy(out.description, wmoDesc(out.weatherCode), sizeof(out.description));
  Serial.printf("[Weather] %s %.1fF (%s)\n", out.city, out.tempF, out.description);
  return true;
}

void fetchWeather() {
  noteTry(FEED_WEATHER);
  Serial.println(F("[Weather] fetching..."));

  // Geocode primary city if needed
  if (Storage::cfg.lat == 0.0f && Storage::cfg.lon == 0.0f) {
    geocodeCity();
    if (Storage::cfg.lat == 0.0f) { Serial.println(F("[Weather] no coords")); return; }
  }
  // Geocode extra cities if needed
  for (int i = 0; i < 2; i++) {
    auto &ec = Storage::cfg.extraCities[i];
    if (strlen(ec.city) == 0) continue;
    if (ec.lat == 0.0f && ec.lon == 0.0f) {
      // Geocode this extra city using same mechanism
      // Temporarily swap into main city slot, geocode, swap back
      char savedCity[64], savedState[32];
      float savedLat = Storage::cfg.lat, savedLon = Storage::cfg.lon;
      strlcpy(savedCity,  Storage::cfg.city,  sizeof(savedCity));
      strlcpy(savedState, Storage::cfg.state, sizeof(savedState));
      strlcpy(Storage::cfg.city,  ec.city,  sizeof(Storage::cfg.city));
      strlcpy(Storage::cfg.state, ec.state, sizeof(Storage::cfg.state));
      Storage::cfg.lat = 0.0f; Storage::cfg.lon = 0.0f;
      geocodeCity();
      ec.lat = Storage::cfg.lat; ec.lon = Storage::cfg.lon;
      // Restore
      strlcpy(Storage::cfg.city,  savedCity,  sizeof(Storage::cfg.city));
      strlcpy(Storage::cfg.state, savedState, sizeof(Storage::cfg.state));
      Storage::cfg.lat = savedLat; Storage::cfg.lon = savedLon;
      if (ec.lat != 0.0f) Storage::save(); // persist geocoded coords
    }
  }

  // Fetch all configured cities
  weatherCityCount = 0;
  if (fetchWeatherCity(Storage::cfg.lat, Storage::cfg.lon, Storage::cfg.city, weatherCities[0]))
    weatherCityCount++;

  for (int i = 0; i < 2; i++) {
    auto &ec = Storage::cfg.extraCities[i];
    if (strlen(ec.city) == 0 || ec.lat == 0.0f) continue;
    if (fetchWeatherCity(ec.lat, ec.lon, ec.city, weatherCities[weatherCityCount]))
      weatherCityCount++;
  }

  if (weatherCityCount > 0) {
    weather = weatherCities[0]; // keep primary alias in sync
    noteOk(FEED_WEATHER);
    weatherReady = true;
  } else {
    noteFail(FEED_WEATHER);
  }
  Serial.printf("[Weather] done: %d cities\n", weatherCityCount);
}

// ── Motorsports + Golf via ESPN ───────────────────────────────────────────────
static void extractJsonStr(const char *buf, const char *key, char *out, int outLen) {
  // Finds first occurrence of "key":"value" and copies value to out
  char search[48]; snprintf(search, sizeof(search), "\"%s\":\"", key);
  const char *p = strstr(buf, search);
  if (!p) { out[0]='\0'; return; }
  p += strlen(search);
  const char *e = strchr(p, '"');
  if (!e) { out[0]='\0'; return; }
  int l = (int)(e-p); if (l>=outLen) l=outLen-1;
  strncpy(out, p, l); out[l]='\0';
}

void fetchRacing() {
  noteTry(FEED_RACING);
  uint16_t mask = Storage::cfg.sportsLeagues;
  raceCount = 0;

  struct RS { const char *path; const char *abbr; uint16_t bit; };
  static const RS SERIES[] = {
    {"racing/nascar-premier", "NASCAR",  1<<9},
    {"racing/f1",             "F1",      1<<10},
    {"racing/irl",            "IndyCar", 1<<11},
    {"golf/pga",              "PGA",     1<<12},
  };

  char *cbuf = (char*)heap_caps_malloc(4096, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!cbuf) return;

  for (auto &s : SERIES) {
    if (!(mask & s.bit)) continue;
    if (raceCount >= MAX_RACES) break;
    esp_task_wdt_reset();

    char url[200];
    snprintf(url, sizeof(url),
      "http://site.api.espn.com/apis/site/v2/sports/%s/scoreboard", s.path);

    int len = httpGET_buf(url, cbuf, 4096, 8000);
    if (len <= 0) { Serial.printf("[Race] %s: no response\n", s.abbr); continue; }
    Serial.printf("[Race] %s: %d bytes, start: %.120s\n", s.abbr, len, cbuf);

    RaceEntry &e = races[raceCount];
    memset(&e, 0, sizeof(e));
    strlcpy(e.series, s.abbr, sizeof(e.series));

    // ESPN racing scoreboard: events[0].name = race name
    // If no events, check if the array exists but is empty
    const char *eventsStart = strstr(cbuf, "\"events\":[");
    if (!eventsStart) { Serial.printf("[Race] %s: no events key\n", s.abbr); continue; }

    // Check if events array is empty: "events":[]
    const char *afterBracket = eventsStart + 10; // skip past "events":[
    while (*afterBracket == ' ') afterBracket++;
    if (*afterBracket == ']') {
      // Off-season or between events — show series name with "No active event"
      strlcpy(e.event, "No active event", sizeof(e.event));
      strlcpy(e.status, "Off season", sizeof(e.status));
      Serial.printf("[Race] %s: off season\n", s.abbr);
      raceCount++;
      continue;
    }

    // Within the events array, find name/shortName
    // "shortName" for races is typically the race title
    extractJsonStr(eventsStart, "shortName", e.event, sizeof(e.event));
    if (!e.event[0]) extractJsonStr(eventsStart, "name", e.event, sizeof(e.event));

    // Status: look for "shortDetail" inside status object
    // ESPN puts it as: "status":{"clock":...,"type":{"shortDetail":"Race Day"}}
    const char *statusStart = strstr(eventsStart, "\"status\":");
    if (statusStart) {
      extractJsonStr(statusStart, "shortDetail", e.status, sizeof(e.status));
      if (!e.status[0]) extractJsonStr(statusStart, "description", e.status, sizeof(e.status));
      if (!e.status[0]) extractJsonStr(statusStart, "detail", e.status, sizeof(e.status));
    }

    // Leader: for motorsports, find position 1 in competitors
    // Format: "position":{"id":"1",...} or "order":1
    // Try finding the first competitor's athlete displayName
    const char *compStart = strstr(eventsStart, "\"competitors\":");
    if (compStart) {
      // Find first competitor's athlete name
      const char *athStart = strstr(compStart, "\"athlete\":");
      if (athStart) {
        extractJsonStr(athStart, "displayName", e.leader, sizeof(e.leader));
        // Abbreviate to first + last initial if too long
        if (strlen(e.leader) > 18) e.leader[18] = '\0';
      }
      // For golf: look for score/toPar
      if (!strcmp(s.abbr, "PGA")) {
        extractJsonStr(compStart, "score", e.detail, sizeof(e.detail));
      } else {
        // For motorsports try to get lap/position info
        extractJsonStr(eventsStart, "displayClock", e.detail, sizeof(e.detail));
      }
    }

    if (e.event[0] || e.status[0]) {
      Serial.printf("[Race] %s: %s | %s | %s\n", e.series, e.event, e.status, e.leader);
      raceCount++;
    }
  }

  heap_caps_free(cbuf);
  noteOk(FEED_RACING);
  racesReady = true;
  Serial.printf("[Race] done: %d series\n", raceCount);
}

// ── Ticker ─────────────────────────────────────────────────────────────────────
// Returns pointer to a static DRAM buffer — caller must not free
const char* buildTickerString() {
  static char t[1024];
  t[0] = '\0';
  int pos = 0;
  auto app = [&](const char *s) {
    int len = strlen(s);
    if (pos + len < (int)sizeof(t) - 1) {
      memcpy(t + pos, s, len);
      pos += len;
      t[pos] = '\0';
    }
  };
  app("  ");
  if (weatherReady&&(Storage::cfg.widgetMask&WIDGET_WEATHER)) {
    char b[96]; snprintf(b,sizeof(b),"%s - Currently %.0fF and %s  High: %.0f  Low: %.0f    ",
      weather.city,weather.tempF,weather.description,weather.highF,weather.lowF);
    app(b);
  }
  if (calReady&&calCount>0) {
    for (int i=0;i<calCount;i++) {
      char b[80];
      if (calEvents[i].allDay)
        snprintf(b,sizeof(b),"Today: %s    ",calEvents[i].title);
      else
        snprintf(b,sizeof(b),"Today %s: %s    ",calEvents[i].timeStr,calEvents[i].title);
      app(b);
    }
  }
  if (scoresReady&&(Storage::cfg.widgetMask&WIDGET_SPORTS)) {
    if (!scoreCount) app("No games today    ");
    else for (int i=0;i<scoreCount;i++) {
      auto &s = scores[i];
      bool isSched = !strstr(s.status,"Final") &&
                     (strstr(s.status,"PM") || strstr(s.status,"AM"));
      char b[64];
      if (isSched)
        snprintf(b,sizeof(b),"%s: %s vs %s %s    ",
          s.league, s.awayTeam, s.homeTeam, s.status);
      else
        snprintf(b,sizeof(b),"%s: %s %s-%s %s %s    ",
          s.league, s.awayTeam, s.awayScore, s.homeScore, s.homeTeam, s.status);
      app(b);
    }
  }
  if (stocksReady&&(Storage::cfg.widgetMask&WIDGET_STOCKS)&&stockCount>0)
    for (int i=0;i<stockCount;i++) {
      char b[40]; snprintf(b,sizeof(b),"%s $%.2f %+.2f%%    ",
        stocks[i].symbol,stocks[i].price,stocks[i].changePercent);
      app(b);
    }
  if (pos < 5) strlcpy(t, "  Connecting...  ", sizeof(t));
  return t;
}

// ── Calendar parse trace ────────────────────────────────────────────────────
// Kept small and static: this runs inside the calendar fetch, where the stack is
// already the tightest thing on the device.
static const int CAL_DEBUG_SLOTS = 24;
static CalDebugEvent _calDebug[CAL_DEBUG_SLOTS];
static uint8_t _calDebugCount = 0;   // how many slots are in use
static uint8_t _calDebugNext  = 0;   // where the next block is written

static void debugPush(const char *title, const char *dtstart, const char *recid,
                      const char *status, bool hasRule) {
  CalDebugEvent &e = _calDebug[_calDebugNext];
  strlcpy(e.title,   title,   sizeof(e.title));
  strlcpy(e.dtstart, dtstart, sizeof(e.dtstart));
  strlcpy(e.recid,   recid,   sizeof(e.recid));
  strlcpy(e.status,  status,  sizeof(e.status));
  e.hasRule = hasRule;
  _calDebugNext = (uint8_t)((_calDebugNext + 1) % CAL_DEBUG_SLOTS);
  if (_calDebugCount < CAL_DEBUG_SLOTS) _calDebugCount++;
}

int calDebugCount() { return _calDebugCount; }

const CalDebugEvent *calDebugAt(int i) {
  if (i < 0 || i >= _calDebugCount) return nullptr;
  const int oldest = (_calDebugCount < CAL_DEBUG_SLOTS) ? 0 : _calDebugNext;
  return &_calDebug[(oldest + i) % CAL_DEBUG_SLOTS];
}

// ── Calendar series trace ───────────────────────────────────────────────────
// Separate from the ring buffer: a monthly meeting's master block can sit hundreds
// of events back in the feed, so the rule text has to be kept for every series.
static const int CAL_SERIES_SLOTS = 16;
static CalSeriesTrace _calSeries[CAL_SERIES_SLOTS];
static uint8_t _calSeriesCount = 0;

static void seriesPush(const char *title, const char *dtstart,
                       const char *rule, const char *exdate) {
  if (_calSeriesCount >= CAL_SERIES_SLOTS) return;
  CalSeriesTrace &e = _calSeries[_calSeriesCount++];
  strlcpy(e.title,   title,   sizeof(e.title));
  strlcpy(e.dtstart, dtstart, sizeof(e.dtstart));
  strlcpy(e.rule,    rule,    sizeof(e.rule));
  strlcpy(e.exdate,  exdate ? exdate : "", sizeof(e.exdate));
}

int calSeriesCount() { return _calSeriesCount; }

const CalSeriesTrace *calSeriesAt(int i) {
  return (i >= 0 && i < _calSeriesCount) ? &_calSeries[i] : nullptr;
}

static void calSeriesReset() { _calSeriesCount = 0; }

// ── iCal line unfolding ─────────────────────────────────────────────────────
// RFC 5545 folds any line longer than 75 characters by breaking it and prefixing
// the next physical line with a single space. A long EXDATE list is folded, and
// parsing the pieces separately truncated it, so skipped dates were missed and a
// moved meeting kept appearing on its old day.
static bool icalIsFold(const char *s) { return s[0] == ' ' || s[0] == '\t'; }

static bool icalAppendFold(char *logical, int &len, int cap, const char *cont) {
  const int add = (int)strlen(cont) - 1;      // drop the single leading space
  if (add <= 0) return true;
  if (len + add >= cap) return false;
  memcpy(logical + len, cont + 1, add);
  len += add;
  logical[len] = '\0';
  return true;
}

// ── Recurring events ─────────────────────────────────────────────────────────
// An iCal feed ships a repeating event once, with an RRULE and a DTSTART in the
// past. Reading DTSTART alone therefore drops a daily 11:30 lunch from every day
// after the first, which is why it appeared online but never on the panel. These
// helpers decide whether a rule places an occurrence on a given local date inside
// the visible window.
static uint32_t civilDays(int y, int m, int d) {
  // Days since 1970-01-01 (Howard Hinnant's days-from-civil). No libc, no TZ.
  y -= (m <= 2);
  const int era = (y >= 0 ? y : y - 399) / 400;
  const int yoe = y - era * 400;
  const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (uint32_t)(era * 146097 + doe - 719468);
}

// 0 = Sunday
static int weekdayOf(uint32_t days) { return (int)((days + 4) % 7); }

static void ymdOf(const char *s, int &y, int &m, int &d) {
  y = m = d = 0;
  if (!s || strlen(s) < 8) return;
  y = (s[0]-'0')*1000 + (s[1]-'0')*100 + (s[2]-'0')*10 + (s[3]-'0');
  m = (s[4]-'0')*10 + (s[5]-'0');
  d = (s[6]-'0')*10 + (s[7]-'0');
}

// BYDAY carries both weekdays and ordinals: "MO,WE" is every Monday and
// Wednesday, "2TU" is the second Tuesday and "-1FR" the last Friday. Dropping the
// ordinal matched every Tuesday, which put a monthly meeting on the wrong weeks.
struct ByDayRule { int8_t wd; int8_t ord; };   // ord 0 = "any"

static int weekdayLetterPair(char a, char b) {
  if (a == 'M' && b == 'O') return 1;
  if (a == 'T' && b == 'U') return 2;
  if (a == 'W' && b == 'E') return 3;
  if (a == 'T' && b == 'H') return 4;
  if (a == 'F' && b == 'R') return 5;
  if (a == 'S' && b == 'A') return 6;
  if (a == 'S' && b == 'U') return 0;
  return -1;
}

static int byDayParse(const char *v, int len, ByDayRule *out, int maxOut) {
  int n = 0, i = 0;
  while (i < len && n < maxOut) {
    if (v[i] == ',' || v[i] == ' ') { i++; continue; }
    int ord = 0, sign = 1;
    if (v[i] == '+' || v[i] == '-') { if (v[i] == '-') sign = -1; i++; }
    bool digits = false;
    while (i < len && v[i] >= '0' && v[i] <= '9') { ord = ord * 10 + (v[i] - '0'); i++; digits = true; }
    if (digits) ord *= sign;
    if (i + 1 < len) {
      const int wd = weekdayLetterPair(v[i], v[i + 1]);
      if (wd >= 0) {
        out[n].wd = (int8_t)wd;
        out[n].ord = digits ? (int8_t)ord : 0;
        n++;
        i += 2;
        continue;
      }
    }
    i++;
  }
  return n;
}

static int daysInMonth(int y, int m) {
  int ny = y, nm = m + 1;
  if (nm > 12) { nm = 1; ny++; }
  return (int)(civilDays(ny, nm, 1) - civilDays(y, m, 1));
}

// Does any entry name today's weekday, ignoring ordinals?
static bool byDayPlain(const ByDayRule *bd, int n, int wd) {
  for (int i = 0; i < n; i++) if (bd[i].wd == wd) return true;
  return false;
}

// Does any entry name today's weekday *and* the right occurrence of it?
static bool byDayInMonth(const ByDayRule *bd, int n, int y, int m, int d) {
  const int wd = weekdayOf(civilDays(y, m, d));
  for (int i = 0; i < n; i++) {
    if (bd[i].wd != wd) continue;
    if (bd[i].ord == 0) return true;
    if (bd[i].ord > 0) {
      int idx = 0;
      for (int dd = 1; dd <= d; dd++) if (weekdayOf(civilDays(y, m, dd)) == wd) idx++;
      if (idx == bd[i].ord) return true;
    } else {
      int back = 0;
      const int total = daysInMonth(y, m);
      for (int dd = total; dd >= d; dd--) if (weekdayOf(civilDays(y, m, dd)) == wd) back++;
      if (back == -bd[i].ord) return true;
    }
  }
  return false;
}

static bool recurMatches(const char *rule, const char *dtstart, int dayShift,
                         const char *ymdToday, const char *exdates) {
  int my, mm, md, ty, tm, td;
  ymdOf(dtstart, my, mm, md);
  ymdOf(ymdToday, ty, tm, td);
  if (!my || !ty) return false;

  // EXDATE removes specific occurrences (a cancelled meeting in a series).
  if (exdates && *exdates) {
    const char *p = exdates;
    while (*p) {
      while (*p == ',' || *p == ' ') p++;
      if (strncmp(p, ymdToday, 8) == 0) return false;
      while (*p && *p != ',') p++;
    }
  }

  enum { FREQ_NONE, FREQ_DAILY, FREQ_WEEKLY, FREQ_MONTHLY, FREQ_YEARLY };
  int freq = FREQ_NONE, interval = 1, count = 0;
  ByDayRule bd[8];
  int bdCount = 0;
  uint32_t until = 0;

  const char *s = rule;
  while (*s) {
    const char *eq = strchr(s, '=');
    if (!eq) break;
    const int klen = (int)(eq - s);
    const char *vb = eq + 1;
    const char *ve = strchr(vb, ';');
    const int vlen = ve ? (int)(ve - vb) : (int)strlen(vb);
    if (klen == 4 && !strncmp(s, "FREQ", 4)) {
      if      (vlen >= 5 && !strncmp(vb, "DAILY", 5))  freq = FREQ_DAILY;
      else if (vlen >= 6 && !strncmp(vb, "WEEKLY", 6)) freq = FREQ_WEEKLY;
      else if (vlen >= 7 && !strncmp(vb, "MONTHLY", 7))freq = FREQ_MONTHLY;
      else if (vlen >= 6 && !strncmp(vb, "YEARLY", 6)) freq = FREQ_YEARLY;
    } else if (klen == 8 && !strncmp(s, "INTERVAL", 8)) {
      interval = atoi(vb); if (interval < 1) interval = 1;
    } else if (klen == 5 && !strncmp(s, "COUNT", 5)) {
      count = atoi(vb);
    } else if (klen == 5 && !strncmp(s, "UNTIL", 5)) {
      int uy, um, ud; ymdOf(vb, uy, um, ud); if (uy) until = civilDays(uy, um, ud);
    } else if (klen == 5 && !strncmp(s, "BYDAY", 5)) {
      bdCount = byDayParse(vb, vlen, bd, 8);
    }
    if (!ve) break;
    s = ve + 1;
  }
  if (freq == FREQ_NONE) return false;

  // dayShift moves the rule's anchor to the local date: a UTC start late enough
  // in the evening belongs to the previous local day.
  const int64_t dMasterSigned = (int64_t)civilDays(my, mm, md) + dayShift;
  if (dMasterSigned < 0) return false;
  const uint32_t dMaster = (uint32_t)dMasterSigned;
  const uint32_t dToday  = civilDays(ty, tm, td);
  const int mdiff = (ty - my) * 12 + (tm - mm);
  const int ydiff = ty - my;

  if (dToday < dMaster) return false;          // nothing before DTSTART
  if (until && dToday > until) return false;

  bool hit = false;
  switch (freq) {
    case FREQ_DAILY:
      hit = ((dToday - dMaster) % (uint32_t)interval) == 0;
      break;
    case FREQ_WEEKLY: {
      const uint32_t weeks = (dToday - dMaster) / 7;
      if (weeks % (uint32_t)interval) return false;
      hit = bdCount ? byDayPlain(bd, bdCount, weekdayOf(dToday))
                    : weekdayOf(dToday) == weekdayOf(dMaster);
      break;
    }
    case FREQ_MONTHLY:
      if (mdiff < 0 || (mdiff % interval)) return false;
      hit = bdCount ? byDayInMonth(bd, bdCount, ty, tm, td) : (td == md);
      break;
    case FREQ_YEARLY:
      if (ydiff < 0 || (ydiff % interval)) return false;
      hit = (tm == mm) &&
            (bdCount ? byDayPlain(bd, bdCount, weekdayOf(dToday)) : (td == md));
      break;
    default:
      return false;
  }
  if (!hit) return false;

  // COUNT caps the total number of occurrences, not the window.
  if (count > 0) {
    int idx = 1;
    if      (freq == FREQ_DAILY)   idx = (int)((dToday - dMaster) / (uint32_t)interval) + 1;
    else if (freq == FREQ_WEEKLY)  idx = (int)(((dToday - dMaster) / 7) / (uint32_t)interval) + 1;
    else if (freq == FREQ_MONTHLY) idx = mdiff / interval + 1;
    else                           idx = ydiff / interval + 1;
    if (idx > count) return false;
  }
  return true;
}

// ── Calendar via iCal ────────────────────────────────────────────────────────
// Stream-parses iCal line by line — handles feeds of any size without buffering.
void fetchCalendar() {
  noteTry(FEED_CAL);
  if (!strlen(Storage::cfg.icalUrl)) { noteOk(FEED_CAL); calReady = true; return; }
  Serial.println(F("[Cal] fetching..."));
  if (!WiFiManager::isConnected()) { noteFail(FEED_CAL); calReady = true; return; }

  // Build date strings for today through next 6 days (7 total)
  #define CAL_DAYS 7
  time_t now_t = time(nullptr);
  char dateDays[CAL_DAYS][9];
  char dateUTCnext[CAL_DAYS][9];

  for (int d = 0; d < CAL_DAYS; d++) {
    time_t dt = now_t + (time_t)d * 86400;
    struct tm *lt2 = localtime(&dt);
    snprintf(dateDays[d], sizeof(dateDays[d]), "%04d%02d%02d",
      lt2->tm_year+1900, lt2->tm_mon+1, lt2->tm_mday);
    // UTC "next day" that maps to this local day (for negative tz offsets)
    time_t dt2 = dt + 86400;
    struct tm *lt3 = localtime(&dt2);
    snprintf(dateUTCnext[d], sizeof(dateUTCnext[d]), "%04d%02d%02d",
      lt3->tm_year+1900, lt3->tm_mon+1, lt3->tm_mday);
  }

  Serial.printf("[Cal] fetching for %s (+6 days)\n", dateDays[0]);
  const char *calUrl = Storage::cfg.icalUrl;

  // Connect
  HTTPClient http;
  WiFiClient plain;
  WiFiClientSecure tls;
  bool ok = false;
  if (strncmp(calUrl, "https", 5) == 0) {
    tls.setInsecure();
    ok = http.begin(tls, String(calUrl));
  } else {
    ok = http.begin(plain, String(calUrl));
  }
  if (!ok) { noteFail(FEED_CAL); calReady = true; return; }
  http.setTimeout(15000);
  http.useHTTP10(true);
  http.addHeader("User-Agent", "TickerTouch/1.0");
  int code = http.GET();
  if (code != 200) {
    Serial.printf("[Cal] HTTP %d\n", code);
    http.end(); noteFail(FEED_CAL); calReady = true; return;
  }

  // Stream-parse line by line
  WiFiClient *stream = http.getStreamPtr();
  uint32_t deadline = millis() + 30000;

  char lineBuf[256];
  int linePos = 0;
  char logical[256];            // physical lines joined back together
  int logicalLen = 0;
  logical[0] = '\0';
  bool inEvent = false;
  char curTitle[64] = {};
  char curDtStart[32] = {};
  char curRRule[160] = {};
  char curExdate[160] = {};
  char curRecId[24] = {};       // RECURRENCE-ID: the date the instance came from
  char curStatus[12] = {};      // STATUS: CANCELLED, TENTATIVE, CONFIRMED
  char curUid[40] = {};         // UID: stable identity of a series
  bool curIsOverride = false;   // this VEVENT edits one instance of a series
  uint8_t stagingCount = 0;
  static CalendarEvent staging[MAX_CAL_EVENTS]; // static — 50 events × ~100 bytes would overflow stack
  // 1 = the entry is an edited instance (came with a RECURRENCE-ID), so it owns
  // that day's slot for its title instead of the master's generated copy.
  static uint8_t stagingFlags[MAX_CAL_EVENTS];
  // Where each edited instance came from: its title and the date it used to
  // occupy, so the master's copy of that day can be removed.
  static char    overTitle[MAX_CAL_EVENTS][64];
  static char    overUid[MAX_CAL_EVENTS][40];
  static char    overDate[MAX_CAL_EVENTS][9];
  static char    stagingUid[MAX_CAL_EVENTS][40];   // UID of the block an entry came from
  static uint8_t overCount = 0;
  memset(staging, 0, sizeof(staging));
  memset(stagingFlags, 0, sizeof(stagingFlags));
  memset(stagingUid, 0, sizeof(stagingUid));
  overCount = 0;
  calSeriesReset();
  int totalBytes = 0;

  // A block cut off mid-line (socket closed, byte cap) still ends the logical line.
  auto flushPartial = [&]() {
    if (linePos <= 0) return;
    lineBuf[linePos] = '\0';
    strlcpy(logical, lineBuf, sizeof(logical));
    logicalLen = linePos;
    linePos = 0;
  };

  auto processLine = [&](char *l) {

    if (strcmp(l, "BEGIN:VEVENT") == 0) {
      inEvent = true; curTitle[0] = '\0'; curDtStart[0] = '\0';
      curRRule[0] = '\0'; curExdate[0] = '\0'; curRecId[0] = '\0';
      curStatus[0] = '\0'; curUid[0] = '\0';
      curIsOverride = false;
    } else if (strcmp(l, "END:VEVENT") == 0 && inEvent) {
      inEvent = false;

      // Record where an edited instance came from before the title check: a moved
      // occurrence has to be traceable even if the feed omits its SUMMARY.
      if (curIsOverride && strlen(curRecId) >= 8 && overCount < MAX_CAL_EVENTS) {
        strlcpy(overTitle[overCount], curTitle, sizeof(overTitle[0]));
        strlcpy(overUid[overCount],   curUid,   sizeof(overUid[0]));
        memcpy(overDate[overCount], curRecId, 8);
        overDate[overCount][8] = '\0';
        overCount++;
      }

      if (!strlen(curTitle) || !strlen(curDtStart)) return;

      // Trace every block, including the ones dropped below, so /cal can answer
      // "why is this on screen" without access to the feed itself.
      debugPush(curTitle, curDtStart, curRecId, curStatus, curRRule[0] != '\0');
      if (curRRule[0]) seriesPush(curTitle, curDtStart, curRRule, curExdate);

      // An instance that was declined or removed arrives as a normal VEVENT
      // carrying STATUS:CANCELLED at its original slot. Showing it is exactly the
      // "meeting that is not there" symptom.
      if (!strcasecmp(curStatus, "CANCELLED")) return;

      bool isUTC = (strlen(curDtStart) >= 16 && curDtStart[15] == 'Z');
      int matchDay = -1; // which day 0-6 this event falls on, -1 = not in range

      // Determine the UTC H/M offset once for UTC events
      int utcH = 0, utcM = 0;
      if (isUTC && strlen(curDtStart) >= 13)
        sscanf(curDtStart + 9, "%2d%2d", &utcH, &utcM);

      for (int d = 0; d < CAL_DAYS && matchDay < 0; d++) {
        if (strncmp(curDtStart, dateDays[d], 8) == 0) {
          if (!isUTC) {
            matchDay = d; // non-UTC: date matches directly
          } else {
            int localMins = utcH*60 + utcM + (Storage::cfg.tzOffsetSec / 60);
            if (localMins >= 0 && localMins < 1440) matchDay = d;
            // if localMins >= 1440 the event is actually day d+1 — skip here, will match next iteration
          }
        }
        // UTC events where UTC date = local date+1 (negative timezone, e.g. UTC event at 1am = prev local day)
        if (matchDay < 0 && isUTC && strncmp(curDtStart, dateUTCnext[d], 8) == 0) {
          int localMins = utcH*60 + utcM + (Storage::cfg.tzOffsetSec / 60);
          if (localMins < 0) matchDay = d; // event is behind UTC, so local date is earlier
        }
      }

      // Time of day, resolved once so recurring copies can reuse it.
      const bool isAllDay = (strlen(curDtStart) == 8);
      int hh = utcH, mm = utcM;
      if (!isAllDay && !isUTC && strlen(curDtStart) >= 13)
        sscanf(curDtStart + 9, "%2d%2d", &hh, &mm);
      int curDayShift = 0;
      if (isUTC) {
        const int total = hh*60 + mm + (Storage::cfg.tzOffsetSec/60);
        if (total < 0)        curDayShift = -1;
        else if (total >= 1440) curDayShift = 1;
        const int localMins = ((total % 1440) + 1440) % 1440;
        hh = localMins / 60; mm = localMins % 60;
      }

      auto addEvent = [&](int day) {
        if (day < 0 || day >= CAL_DAYS || stagingCount >= MAX_CAL_EVENTS) return;
        stagingFlags[stagingCount] = curIsOverride ? 1 : 0;
        strlcpy(stagingUid[stagingCount], curUid, sizeof(stagingUid[0]));
        CalendarEvent &e = staging[stagingCount++];
        strlcpy(e.title, curTitle, sizeof(e.title));
        e.dayOffset = day;
        e.allDay = isAllDay;
        if (isAllDay) {
          strlcpy(e.timeStr, "All Day", sizeof(e.timeStr));
          e.startMinute = 9999;
        } else {
          e.startMinute = hh * 60 + mm;
          const bool pm = (hh >= 12);
          int dh = hh % 12; if (dh == 0) dh = 12;
          snprintf(e.timeStr, sizeof(e.timeStr), "%d:%02d %s", dh, mm, pm?"PM":"AM");
        }
      };

      if (curRRule[0]) {
        bool any = false;
        for (int d = 0; d < CAL_DAYS; d++) {
          if (recurMatches(curRRule, curDtStart, curDayShift, dateDays[d], curExdate)) {
            addEvent(d);
            any = true;
          }
        }
        // A rule we cannot expand still shows on its own DTSTART day.
        if (!any && matchDay >= 0) addEvent(matchDay);
      } else if (matchDay >= 0) {
        addEvent(matchDay);
      }

    } else if (inEvent) {
      if (strncmp(l, "SUMMARY", 7) == 0) {
        char *colon = strchr(l, ':');
        if (colon) strlcpy(curTitle, colon+1, sizeof(curTitle));
      } else if (strncmp(l, "DTSTART", 7) == 0) {
        char *colon = strchr(l, ':');
        if (colon) strlcpy(curDtStart, colon+1, sizeof(curDtStart));
      } else if (strncmp(l, "RRULE", 5) == 0) {
        char *colon = strchr(l, ':');
        if (colon) strlcpy(curRRule, colon+1, sizeof(curRRule));
      } else if (strncmp(l, "EXDATE", 6) == 0) {
        char *colon = strchr(l, ':');
        if (colon) strlcpy(curExdate, colon+1, sizeof(curExdate));
      } else if (strncmp(l, "RECURRENCE-ID", 13) == 0) {
        // The feed ships one copy of a repeating event plus one extra VEVENT for
        // each instance that was edited. The value is the date the instance used
        // to occupy, which is how a moved meeting is traced to its old slot.
        curIsOverride = true;
        char *colon = strchr(l, ':');
        if (colon) strlcpy(curRecId, colon+1, sizeof(curRecId));
      } else if (strncmp(l, "STATUS", 6) == 0) {
        char *colon = strchr(l, ':');
        if (colon) strlcpy(curStatus, colon+1, sizeof(curStatus));
      } else if (strncmp(l, "UID", 3) == 0) {
        char *colon = strchr(l, ':');
        if (colon) strlcpy(curUid, colon+1, sizeof(curUid));
      }
    }
  };

  // read() returns int (safe — no PSRAM pointer dereference).
  // Tight inner loop reads multiple bytes before yielding to watchdog.
  const int MAX_CAL_BYTES = 600000;
  int yieldAt = 32768;
  while (millis() < deadline && totalBytes < MAX_CAL_BYTES) {
    int got = 0;
    while (totalBytes < MAX_CAL_BYTES && got < 256) {
      int c = stream->read();
      if (c < 0) break;
      char ch = (char)c;
      totalBytes++; got++;
      if (ch == '\n') {
        if (linePos > 0 && lineBuf[linePos-1] == '\r') linePos--;
        lineBuf[linePos] = '\0';
        if (linePos > 0 && icalIsFold(lineBuf)) {
          icalAppendFold(logical, logicalLen, (int)sizeof(logical), lineBuf);
        } else {
          if (logicalLen) processLine(logical);
          strlcpy(logical, lineBuf, sizeof(logical));
          logicalLen = linePos;
        }
        linePos = 0;
      } else if (linePos < (int)sizeof(lineBuf)-1) {
        lineBuf[linePos++] = ch;
      }
    }
    if (got == 0) {
      if (!stream->connected()) { flushPartial(); break; }
      vTaskDelay(1);
    } else if (totalBytes >= yieldAt) {
      yieldAt += 32768;
      vTaskDelay(1);
      esp_task_wdt_reset(); // 540KB takes ~15s on slow connections
    }
  }
  if (totalBytes >= MAX_CAL_BYTES) flushPartial();

  http.end();
  if (strncmp(calUrl, "https", 5) == 0) tls.stop(); else plain.stop();
  Serial.printf("[Cal] %d bytes, %d events (7-day)\n", totalBytes, stagingCount);

  // Sort by dayOffset first, then startMinute within each day
  for (int i = 1; i < stagingCount; i++) {
    CalendarEvent key = staging[i];
    int j = i - 1;
    while (j >= 0 && (staging[j].dayOffset > key.dayOffset ||
           (staging[j].dayOffset == key.dayOffset && staging[j].startMinute > key.startMinute))) {
      staging[j+1] = staging[j]; j--;
    }
    staging[j+1] = key;
  }

  if (logicalLen) processLine(logical);   // last block if the feed ends without a newline

  // Collapse the copies the feed implies: expansion adds the master's version of
  // every instance, and an edited instance arrives as its own VEVENT. Same title,
  // same day means one entry; if only one of them carries a RECURRENCE-ID, that
  // one is the edit the user actually made, so it wins (including when the time
  // was moved, which is why this cannot just compare timestamps).
  {
    // 1. A moved instance still occupies its old day as far as the master's rule
    //    is concerned, so a meeting shifted to next week kept showing on the
    //    morning it originally held. Remove those copies.
    static uint8_t dead[MAX_CAL_EVENTS];
    memset(dead, 0, sizeof(dead));
    for (int i = 0; i < stagingCount; i++) {
      if (stagingFlags[i]) continue;
      for (int k = 0; k < overCount && !dead[i]; k++) {
        // Same series by UID (survives a rename) or by title.
        const bool sameSeries =
            (overUid[k][0] && !strcmp(overUid[k], stagingUid[i])) ||
            (overTitle[k][0] && !strcmp(overTitle[k], staging[i].title));
        if (!sameSeries) continue;
        for (int d = 0; d < CAL_DAYS; d++) {
          if (d == staging[i].dayOffset && !strncmp(dateDays[d], overDate[k], 8)) {
            dead[i] = 1;
            break;
          }
        }
      }
    }

    // 2. Collapse the remaining copies.
    int kept = 0;
    for (int i = 0; i < stagingCount; i++) {
      if (dead[i]) continue;
      bool drop = false;
      for (int j = 0; j < kept && !drop; j++) {
        if (strcmp(staging[i].title, staging[j].title) != 0) continue;
        if (staging[i].dayOffset != staging[j].dayOffset) continue;
        if (staging[i].startMinute == staging[j].startMinute) {
          drop = true;                                  // exact duplicate
        } else if (stagingFlags[i] && !stagingFlags[j]) {
          staging[j] = staging[i];                      // edited instance replaces
          stagingFlags[j] = stagingFlags[i];
          drop = true;
        } else if (!stagingFlags[i] && stagingFlags[j]) {
          drop = true;                                  // master copy of that instance
        }
      }
      if (drop) continue;
      if (kept != i) {
        staging[kept] = staging[i];
        stagingFlags[kept] = stagingFlags[i];
        strlcpy(stagingUid[kept], stagingUid[i], sizeof(stagingUid[0]));
      }
      kept++;
    }
    stagingCount = (uint8_t)kept;
  }

  memcpy(calEvents, staging, stagingCount * sizeof(CalendarEvent));
  calCount = stagingCount;
  noteOk(FEED_CAL);
  calReady = true;
}

// ── forceRefresh ───────────────────────────────────────────────────────────────
void forceRefresh() {
  Serial.println(F("[Data] forceRefresh"));
  if (Storage::cfg.lat==0.0f&&Storage::cfg.lon==0.0f) geocodeCity();
  uint32_t big=0xFFFFFFFF;
  lastSports=big-REFRESH_SPORTS_MS; lastStocks=big-REFRESH_STOCKS_MS;
  lastWeather=big-REFRESH_WEATHER_MS; lastCrypto=big-REFRESH_CRYPTO_MS;
  lastCalendar=big-REFRESH_CALENDAR_MS;
  // Reset ready flags — tabs will rebuild when data returns
  scoresReady=false; stocksReady=false; weatherReady=false; cryptoReady=false; calReady=false;
}

// ── begin / tick ───────────────────────────────────────────────────────────────
void begin() {
  Serial.println(F("[Data] begin"));
  fetchTimezone();
  esp_task_wdt_reset();   // tz lookup + up to 10s of NTP waiting
  if (Storage::cfg.lat==0.0f&&Storage::cfg.lon==0.0f) geocodeCity();
  else Serial.printf("[Geo] saved: %s %.4f, %.4f\n",Storage::cfg.city,Storage::cfg.lat,Storage::cfg.lon);
  esp_task_wdt_reset();   // geocode tries two endpoints back to back
  uint32_t now=millis();
  lastSports   = now - REFRESH_SPORTS_MS  + 3000;   // sports fires 3s after boot
  lastStocks   = now - REFRESH_STOCKS_MS  + 10000;  // stocks fires 10s after boot
  lastWeather  = now - REFRESH_WEATHER_MS;           // weather fires immediately
  lastCalendar = now - REFRESH_CALENDAR_MS;          // calendar fires immediately
  lastCrypto    = now - REFRESH_CRYPTO_MS + 15000;   // crypto fires 15s after boot
  lastRacing   = now - REFRESH_RACING_MS  + 20000;   // racing fires 20s after boot
  Serial.println(F("[Data] begin done"));
}

void tick() {
  uint32_t n = millis();
  if ((n-lastWeather) >=REFRESH_WEATHER_MS)  { lastWeather =millis(); fetchWeather();  return; }
  if ((n-lastCalendar)>=REFRESH_CALENDAR_MS) {
    if (time(nullptr) < 100000) { lastCalendar = n - REFRESH_CALENDAR_MS + 5000; return; }
    lastCalendar=millis(); fetchCalendar(); return;
  }
  // ESPN is asked for "today", and "today" comes from localtime(). Before SNTP
  // lands that is 1970-01-01, so the scoreboard comes back empty and the Sports
  // tab sits on "No games today" even though the leagues and filters are fine.
  // Hang back and retry every 5s until the clock is real.
  if ((n-lastSports)  >=REFRESH_SPORTS_MS) {
    if (time(nullptr) < 100000) { lastSports = n - REFRESH_SPORTS_MS + 5000; return; }
    lastSports = millis(); fetchSports(); return;
  }
  if ((n-lastStocks)  >=REFRESH_STOCKS_MS)   { lastStocks  =millis(); fetchStocks();   return; }
  if ((n-lastCrypto)  >=REFRESH_CRYPTO_MS)   { lastCrypto  =millis(); fetchCrypto();   return; }
  if ((n-lastRacing)  >=REFRESH_RACING_MS)   { lastRacing  =millis(); fetchRacing();   return; }
}

} // namespace DataManager
