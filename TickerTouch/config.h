#pragma once

// ── Hardware: JC4827W543 (ESP32-S3-WROOM-1-N4R8) ─────────────────────────────
// SoC  : ESP32-S3 dual-core 240MHz
// Flash: 4MB (QSPI)   ← N4 in N4R8
// PSRAM: 8MB (OSPI)   ← R8 in N4R8
// LCD  : NV3041A, 4-bit QSPI interface, 480×272 IPS
// Touch: GT911 capacitive, I2C

// ── Display ──────────────────────────────────────────────────────────────────
#define TFT_WIDTH        480
#define TFT_HEIGHT       272
#define LVGL_BUF_LINES   64          // LVGL draw buffer lines — uses PSRAM

// ── NV3041A 4-bit QSPI pins (verified from profi-max reference project) ───────
#define LCD_CS           45
#define LCD_SCK          47
#define LCD_D0           21
#define LCD_D1           48
#define LCD_D2           40
#define LCD_D3           39
#define LCD_BL           1           // Backlight PWM pin

// ── Touch (GT911 I2C) — use TouchLib by mmMicky ───────────────────────────────
// TouchLib auto-detects GT911 address (0x5D or 0x14)
#define TOUCH_SDA        8
#define TOUCH_SCL        9
#define TOUCH_RST        38
#define TOUCH_INT        -1          // -1 = polling / interrupt-less mode

// ── LVGL ─────────────────────────────────────────────────────────────────────
#define LVGL_TICK_MS     1

// ── WiFi ─────────────────────────────────────────────────────────────────────
#define AP_SSID          "TickerTouch-Setup"
#define AP_PASS          ""                  // open AP for captive portal
#define WIFI_TIMEOUT_MS  20000

// ── Data refresh intervals (ms) ──────────────────────────────────────────────
#define REFRESH_SPORTS_MS     60000   // 1 min
#define REFRESH_STOCKS_MS     30000   // 30 sec
#define REFRESH_CRYPTO_MS     20000   // 20 sec
#define REFRESH_WEATHER_MS   300000   // 5 min
#define REFRESH_NEWS_MS      120000   // 2 min

// ── Ticker ───────────────────────────────────────────────────────────────────
#define TICKER_SPEED_PX  2           // pixels per LVGL tick
#define TICKER_FONT_SIZE 18          // px (use nearest LVGL built-in)
#define TICKER_BAR_H     36          // px height of scrolling bar

// ── Themes ───────────────────────────────────────────────────────────────────
#define THEME_DARK    0
#define THEME_RETRO   1
#define THEME_NEON    2
#define THEME_CLEAN   3
#define THEME_SPORTS  4

// ── NVS keys ─────────────────────────────────────────────────────────────────
#define NVS_NAMESPACE      "tickertch"
#define NVS_KEY_CONFIGURED "configured"
#define NVS_KEY_THEME      "theme"
#define NVS_KEY_WIDGETS    "widgets"
#define NVS_KEY_SPORTS     "sports"
#define NVS_KEY_STOCKS     "stocks"
#define NVS_KEY_CRYPTO     "crypto"
#define NVS_KEY_LAT        "lat"
#define NVS_KEY_LON        "lon"
#define NVS_KEY_CITY       "city"

// ── API endpoints (free tiers) ────────────────────────────────────────────────
#define API_ESPN_SCORES    "http://site.api.espn.com/apis/site/v2/sports/%s/%s/scoreboard"
#define API_WEATHER        "http://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s&current_weather=true&daily=temperature_2m_max,temperature_2m_min,weathercode,precipitation_probability_max&temperature_unit=fahrenheit&timezone=auto&forecast_days=4"
#define API_COINGECKO      "https://api.coingecko.com/api/v3/simple/price?ids=%s&vs_currencies=usd&include_24hr_change=true"
#define API_YAHOO_FINANCE  "https://query1.finance.yahoo.com/v8/finance/chart/%s?interval=1d&range=1d"

// ── Sports leagues (ESPN path components) ────────────────────────────────────
// format: {sport}/{league}
#define SPORT_NFL  "football/nfl"
#define SPORT_NBA  "basketball/nba"
#define SPORT_NHL  "hockey/nhl"
#define SPORT_MLB  "baseball/mlb"
#define SPORT_CFB  "football/college-football"
#define SPORT_CBB  "basketball/mens-college-basketball"
#define SPORT_MLS  "soccer/usa.1"
#define SPORT_EPL  "soccer/eng.1"

// ── Default widgets (bitmask) ─────────────────────────────────────────────────
#define WIDGET_SPORTS   (1 << 0)
#define WIDGET_STOCKS   (1 << 1)
#define WIDGET_CRYPTO   (1 << 2)
#define WIDGET_WEATHER  (1 << 3)
#define WIDGET_NEWS     (1 << 4)
#define WIDGETS_DEFAULT (WIDGET_SPORTS | WIDGET_STOCKS | WIDGET_CRYPTO | WIDGET_WEATHER)

// ArduinoJson nesting limit — ESPN JSON is deeply nested
#define ARDUINOJSON_DEFAULT_NESTING_LIMIT 50
