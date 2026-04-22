# TickerTouch

A desk dashboard for an ESP32-S3 display board showing live sports scores, stocks, crypto, weather, and calendar events on a 4.3" touchscreen.

<img width="1024" height="770" alt="image" src="https://github.com/user-attachments/assets/1bd5a074-e73f-412d-9c74-761056413006" />


---

## Hardware

| Component | Detail |
|-----------|--------|
| Board | JC4827W543 (Guition / Waveshare / compatible) |
| SoC | ESP32-S3-WROOM-1-N4R8 (4MB Flash, 8MB OPI PSRAM) |
| Display | 4.3" IPS 480x272, NV3041A driver, 4-bit QSPI |
| Touch | GT911 capacitive I2C (SDA=8, SCL=4) |
| Backlight | PWM on GPIO 1 |

---

## Arduino IDE Setup

### Board Settings

| Setting | Value |
|---------|-------|
| Board | ESP32S3 Dev Module |
| Flash Size | 4MB (32Mb) |
| Partition Scheme | Huge APP (3MB No OTA / 1MB SPIFFS) |
| PSRAM | OPI PSRAM |
| CPU Frequency | 240MHz |
| USB CDC On Boot | Enabled |

### Required Libraries

| Library | Version | Notes |
|---------|---------|-------|
| Arduino_GFX_Library | >= 1.6.1 | Library Manager |
| LVGL | 8.3.x | **Not** 9.x — breaking API changes |
| ArduinoJson | 6.21.x | **Not** 7.x |
| bb_captouch | any | Library Manager |

### lv_conf.h

Copy `lv_conf.h` from this project into your Arduino `libraries/` folder — next to (not inside) the `lvgl/` directory. This file configures LVGL fonts, PSRAM allocation, canvas support, and QR code support.

---

## First-Time Setup

1. Flash the firmware via Arduino IDE - or use the online flash tool - https://iairj84.github.io/TickerTouch/
2. The device shows a QR code — scan it to join the **TickerTouch-Setup** WiFi hotspot
3. The setup page opens automatically (or navigate to `http://192.168.4.1`) 
5. Select your WiFi network, enter your city, choose sports leagues, pick a theme
6. Tap **Save & Connect** — device reboots and connects to your network
7. The device IP address is shown in the gear icon settings panel
8. <img width="726" height="1208" alt="image" src="https://github.com/user-attachments/assets/7c468573-ac82-49a7-afc0-ecaf951eb37c" />


If the device cannot connect to WiFi at any point it automatically returns to the captive portal.

---

## Web Settings

Visit the device IP address in a browser while on the same network. The IP is shown in the on-device gear menu.

<img width="726" height="1208" alt="image" src="https://github.com/user-attachments/assets/0bf57648-1a3f-4969-8f4f-e9de1c15a300" />


| Section | Description |
|---------|-------------|
| Location & Weather | Up to 3 cities. Each city is geocoded automatically on first save. |
| Sports Leagues | Enable/disable: NFL, NBA, NHL, MLB, MLS, EPL, CFB, CBB, WNBA, NASCAR, F1, IndyCar, PGA Golf |
| Sports Filter | Select specific teams — leave all unchecked to show everything. Leagues not yet enabled are shown greyed out but can still be pre-configured. |
| Stocks | Comma-separated tickers (e.g. SPY, AAPL, TSLA). Prices via Yahoo Finance - no API key needed. |
| Crypto | Comma-separated CoinGecko IDs (e.g. bitcoin, ethereum, solana) |
| Calendar | iCal URL from Google, Outlook, or Apple Calendar |
| Visible Tabs | Toggle Sports, Finance, Weather, Calendar tabs on/off |
| Save Settings | Saves all settings. Filter changes trigger an automatic restart. |
| Restart Device | Reboots the device without saving |

### Getting your iCal URL

| Provider | Steps |
|----------|-------|
| Google | Calendar Settings → pick one specific calendar → Secret address in iCal format |
| Outlook | Calendar → Share → Get a link → ICS |
| Apple | iCloud.com → Calendar → Share icon → Public Calendar URL |

Use a single specific calendar rather than your full merged feed — large feeds (>500KB) take longer to parse.

---

## On-Device Settings (Gear Icon)

Tap the ⚙ gear icon at the top right of the main screen.

| Control | Description |
|---------|-------------|
| http://... | Settings page URL — tap to copy mentally or note it down |
| Theme | Dark, Retro (amber), Neon (cyan/magenta), Clean (light), Sports (green/gold) |
| Speed | Ticker scroll speed 1 (slow) to 5 (fast) |
| Bright | Backlight brightness |
| Sleep | Screensaver timeout: 5 min, 10 min, 30 min, or Off |
| Restart Device | Reboots immediately |

Changing the theme triggers an automatic reboot. All other changes apply instantly on Done.

---

## Tabs

### Home
Summary view of all active data. Shows live/final sports scores (filtered to your teams), weather for all cities, stocks, crypto, calendar events for today, and a "See Sports tab" prompt when motorsports/golf are active.

### Sports
Full scoreboard grouped by league. Each league shows its games with team color badges, scores, and status (live period/inning, final, or scheduled local time). Motorsports and golf appear below the regular sports with current event name, status, and leader.

### Finance
Stock price cards (symbol, price, % change) and crypto cards (name, price, 24h change).

### Weather
One or more city panels depending on how many cities are configured. Each shows current temp, feels like, H/L, humidity, wind, and a 3-day forecast. Scrollable when multiple cities are shown.

### Calendar
Full week view (today + 6 days). Events grouped under day headers. Today's events use accent color; future days use muted text.

---

## Ticker

Scrolls continuously at the bottom of all screens. Segments in order:
1. **Clock** — current time, updates every minute
2. **Weather** — all configured cities: city, temp, condition, H/L
3. **Sports** — live and final scores plus motorsports/golf events
4. **Stocks** — symbol, price, % change
5. **Crypto** — symbol, price, 24h % change
6. **Calendar** — today's events only

Segments are separated by ` - `. Empty or disabled segments are skipped automatically.

---

## Screensaver

Activates after configurable idle timeout. Shows a large bouncing clock, date, and a scrolling ticker at the bottom. Tap anywhere to wake.

---

## Data Sources

| Feed | API | Key Required |
|------|-----|-------------|
| Sports scores | ESPN public scoreboard API | No |
| Motorsports / Golf | ESPN public scoreboard API | No |
| Weather | Open-Meteo | No |
| Geocoding | Open-Meteo + Nominatim fallback | No |
| Stocks | Yahoo Finance | No |
| Crypto | CoinGecko | No |
| Calendar | iCal (any provider) | No |
| Timezone | ip-api.com | No |

---

## Sports Leagues

| League | Bit | Teams/Notes |
|--------|-----|-------------|
| NFL | 0 | 32 teams |
| NBA | 1 | 30 teams |
| NHL | 2 | 33 teams |
| MLB | 3 | 30 teams |
| CFB | 4 | Conference filter (SEC, Big Ten, Top 25, etc.) |
| MLS | 5 | 29 teams |
| EPL | 6 | 20 teams |
| CBB | 7 | Conference filter |
| WNBA | 8 | 15 teams (2025 roster incl. Portland Fire, Toronto Tempo, Golden State Valkyries) |
| NASCAR | 9 | Event/status/leader display |
| F1 | 10 | Event/status/leader display |
| IndyCar | 11 | Event/status/leader display |
| PGA Golf | 12 | Event/status/leader display |

Motorsports and golf show "Off season" when no active event is found in the ESPN feed.

---

## Troubleshooting

**White screen on boot** — normal for ~2 seconds before LVGL and the splash screen initialize.

**Watchdog crash / sports never loads** — each league fetch takes a few seconds. With many leagues enabled the total fetch time can approach the 60s watchdog limit. Try enabling 4-5 leagues maximum, or disable those currently out of season.

**Calendar shows "Loading..."** — large iCal feeds take up to 30 seconds to stream and parse. Use a single specific Google/Outlook/Apple calendar rather than a merged "all calendars" feed.

**Stocks not showing prices** — Yahoo Finance is used directly, no API key required. Check your ticker symbols are valid US tickers.

**Location not geocoding** — make sure you enter both City and State/Region in the settings, then save. The device geocodes on first save and caches the coordinates.

**WiFi lost after settings change** — device falls back to captive portal automatically if it cannot connect on boot.

**NASCAR/F1/IndyCar/PGA shows "Off season"** — these sports show the current active event only. Between race weekends the ESPN API returns an empty events array, which is shown as "Off season".

---

## Architecture Notes

- **Core 1**: LVGL task (12KB stack) — all UI rendering, touch, screensaver, ticker scroll
- **Core 0**: Main task (20KB stack) — all HTTP fetches, data parsing, watchdog resets
- **Core 0**: Web task (4KB stack, priority 2) — settings web server
- LVGL heap is allocated from PSRAM via custom allocator in `lv_conf.h`
- All HTTP response buffers use `MALLOC_CAP_INTERNAL` to avoid PSRAM memcpy crashes
- `CONFIG_SPIRAM_USE_MALLOC` is explicitly **not** set — this caused String/malloc crashes
