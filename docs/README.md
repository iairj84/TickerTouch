# TickerTouch — Setup & Build Guide

## Hardware

- **Board**: JC4827W543 (Guition / Waveshare / compatible)
- **SoC**: ESP32-S3-WROOM-1-**N4R8**
  - CPU: dual-core 240MHz
  - Flash: **4MB** QSPI  ← the "N4" in N4R8
  - PSRAM: **8MB** OSPI  ← the "R8" in N4R8
- **Display**: 4.3" IPS, 480×272, driver **NV3041A**, 4-bit QSPI interface
- **Touch**: GT911 capacitive, I2C (address 0x5D or 0x14)
- **Backlight**: PWM on GPIO 1

> ⚠️ The display uses **NV3041A via 4-bit QSPI** — NOT an RGB parallel interface.
> Pin numbers are confirmed from the profi-max reference project.

---

## Arduino IDE Setup

### 1. Install ESP32 Arduino Core
Add to Preferences → Additional Boards Manager URLs:
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```
Install: **esp32 by Espressif Systems** (2.0.14 recommended)

### 2. Board Settings
| Setting | Value |
|---------|-------|
| Board | **ESP32S3 Dev Module** (or u-blox NORA-W10) |
| Upload Speed | 921600 |
| CPU Frequency | 240MHz |
| Flash Size | **4MB (32Mb)** |
| Partition Scheme | **Huge APP (3MB No OTA/1MB SPIFFS)** |
| PSRAM | **OPI PSRAM** |
| Arduino Runs On | Core 1 |

### 3. Install Libraries

| Library | How to Install |
|---------|---------------|
| Arduino_GFX_Library ≥ 1.6.1 | Library Manager |
| LVGL **8.4.0** (exact version) | Library Manager — search "lvgl", pick 8.4.0 |
| ArduinoJson ≥ 6.21 | Library Manager |
| TouchLib (mmMicky) | **ZIP install** → https://github.com/mmMicky/TouchLib |

### 4. Configure lv_conf.h
After installing LVGL:
1. Copy `lv_conf.h` from this project to your Arduino libraries folder, **next to** (not inside) the `lvgl/` directory:
```
Arduino/libraries/
├── lvgl/           ← library folder
├── lv_conf.h       ← put it here
└── Arduino_GFX_Library/
```

---

## Pin Map (NV3041A QSPI)

| Signal | GPIO |
|--------|------|
| LCD CS | 45 |
| LCD SCK | 47 |
| LCD D0 | 21 |
| LCD D1 | 48 |
| LCD D2 | 40 |
| LCD D3 | 39 |
| Backlight PWM | 1 |
| Touch SDA | 8 |
| Touch SCL | 9 |
| Touch RST | 38 |

---

## First Boot Flow

```
Power on → NVS empty
  └─ QR code screen: "Connect to TickerTouch-Setup WiFi"
       └─ User connects to AP → browser opens 192.168.4.1
            └─ Captive portal: WiFi creds, sports, stocks, crypto, weather, theme
                 └─ Save → device connects → reboots into dashboard
```

## Normal Boot Flow

```
Power on → NVS has config
  └─ Connect WiFi (STA mode)
       └─ NTP time sync
            └─ Dashboard (loading state)
                 └─ Fetch tasks start (Core 0):
                      ├─ ESPN scores    — every 60s
                      ├─ Yahoo Finance  — every 30s
                      ├─ CoinGecko      — every 20s
                      └─ Open-Meteo     — every 5min
```

---

## Project Structure

```
TickerTouch/
├── TickerTouch.ino              # Entry point, task setup
├── config.h                     # All pins, constants, API URLs
├── lv_conf.h                    # LVGL 8.4.0 configuration
├── src/
│   ├── display.h / .cpp         # NV3041A + TouchLib + LVGL init
│   ├── storage.h / .cpp         # NVS preferences
│   ├── wifi_manager.h / .cpp    # Captive portal + STA
│   ├── data/
│   │   └── data_manager.h/.cpp  # ESPN, Yahoo, CoinGecko, Open-Meteo
│   ├── screens/
│   │   └── screen_manager.h/.cpp # All LVGL screens
│   └── themes/
│       └── themes.h              # 5 color palettes
└── docs/
    ├── README.md
    └── LIBRARIES.txt
```

---

## Free APIs Used

| Data | Provider | Auth Required |
|------|----------|---------------|
| Sports scores | ESPN Public API | None |
| Weather | Open-Meteo | None |
| Crypto | CoinGecko (free tier) | None |
| Stocks | Yahoo Finance (unofficial) | None |
| Time | NTP pool.ntp.org | None |

---

## Themes

| # | Name | Look |
|---|------|------|
| 0 | **Dark** | Deep navy + indigo — default |
| 1 | **Retro** | Black + amber phosphor |
| 2 | **Neon** | Deep purple + cyan/magenta |
| 3 | **Clean** | Light mode + indigo |
| 4 | **Sports** | Dark green field + gold |

Tap the ⚙ gear icon in the header to switch themes live.

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| Blank screen | Confirm `LCD_CS=45, LCD_SCK=47, D0=21, D1=48, D2=40, D3=39` in config.h |
| `gfx->begin() FAILED` | Wrong QSPI pins or display driver — recheck pin map |
| Touch not responding | TouchLib auto-detects I2C address; confirm SDA=8, SCL=9, RST=38 |
| WiFi won't connect | ESP32-S3 is 2.4GHz only — check band |
| No data on ticker | Open Serial Monitor (115200) — HTTP status codes logged |
| Heap crash / restart | Reduce `DynamicJsonDocument` sizes in data_manager.cpp |
| LVGL compile errors | Ensure LVGL 8.4.0 exact — 9.x has breaking API changes |
| `lv_conf.h` not found | Place it in `Arduino/libraries/lv_conf.h`, not inside `lvgl/` |

---

## Extending

**New data feed**: Add fetch function in `data_manager.cpp`, add `WIDGET_*` flag in `config.h`, include in `buildTickerString()`, add tab in `screen_manager.cpp`.

**New theme**: Add `case THEME_X:` in `themes.h`, update roller options in `showSettings()`.

**OTA**: Use `ArduinoOTA` or the ESP-IDF `Update` class — hook into the OTA button in the settings screen.
