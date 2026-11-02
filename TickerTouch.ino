/**
 * TickerTouch — JC4827W543 (ESP32-S3)
 * Core 1: LVGL + UI (under mutex)
 * Core 0: WiFi data fetches + web server
 */

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <time.h>
#include "config.h"
#include "src/wifi_manager.h"
#include "src/display.h"
#include "src/storage.h"
#include "src/ticker_engine.h"
#include "src/screensaver.h"
#include "src/slideshow.h"
#include "src/data/data_manager.h"
#include "src/screens/screen_manager.h"

TaskHandle_t      lvglTaskHandle = nullptr;
TaskHandle_t      mainTaskHandle = nullptr;
TaskHandle_t      webTaskHandle  = nullptr;
SemaphoreHandle_t lvglMutex      = nullptr;
volatile bool     gNeedRefresh   = false;

void lvglTask(void *) {
  for (;;) {
    if (xSemaphoreTake(lvglMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      lv_timer_handler();
      TickerEngine::tick();
      Screensaver::tick();
      xSemaphoreGive(lvglMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

static uint32_t _lastFetch = 0;
static uint32_t _nextDump  = 25000;

// Web server task — runs independently so settings page always responds
void webTask(void *) {
  for (;;) {
    WiFiManager::handleSettingsServer();
    vTaskDelay(pdMS_TO_TICKS(10)); // 100Hz — very responsive
  }
}

void mainTask(void *) {
  // Subscribe this task to watchdog and set a generous timeout
  // Sports + calendar fetching can take 30+ seconds
  esp_task_wdt_add(NULL);
  // All network setup runs here, never in setup(). setup() runs on the Arduino
  // loopTask, whose stack is CONFIG_ARDUINO_LOOP_STACK_SIZE (8KB by default) and
  // cannot be raised from the sketch — an mbedTLS handshake alone needs far more
  // than that, so calling DataManager::begin() from setup() overflowed it.
  DataManager::begin();
  for (;;) {
    esp_task_wdt_reset();
    uint32_t now = millis();
    if (now - _lastFetch >= 1000) {
      _lastFetch = now;
      if (gNeedRefresh) { gNeedRefresh = false; DataManager::forceRefresh(); }
      DataManager::tick();
    }
    if (now > _nextDump) {
      _nextDump = now + 30000;
      time_t t = time(nullptr);
      char tb[24] = "no-sync";
      if (t > 100000) strftime(tb, sizeof(tb), "%H:%M:%S", localtime(&t));
      Serial.printf("\n=== %s WiFi:%s %s ===\n"
        "City:%s %.4f,%.4f\n"
        "Wx:%d %.1fF | Spt:%d n=%d mask=%d\n"
        "Stk:%d n=%d '%s'\nHeap:%d\n===\n",
        tb, WiFiManager::isConnected()?"OK":"DOWN", WiFiManager::getIP(),
        Storage::cfg.city, Storage::cfg.lat, Storage::cfg.lon,
        DataManager::weatherReady, DataManager::weather.tempF,
        DataManager::scoresReady, DataManager::scoreCount, (int)Storage::cfg.sportsLeagues,
        DataManager::stocksReady, DataManager::stockCount, Storage::cfg.stocks,
        (int)esp_get_free_heap_size());
      // Headroom diagnostics: whatever panic comes next, the last dump before it
      // shows which task was nearly out of stack (bytes left) or out of PSRAM.
      Serial.printf("StkMain:%u StkLVGL:%u StkWeb:%u | PSRAMfree:%u\n",
        (unsigned)uxTaskGetStackHighWaterMark(NULL),
        (unsigned)(lvglTaskHandle ? uxTaskGetStackHighWaterMark(lvglTaskHandle) : 0),
        (unsigned)(webTaskHandle ? uxTaskGetStackHighWaterMark(webTaskHandle) : 0),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  // Why did the last boot happen? This is kept in RTC memory across a reset, so
  // it also tells you what killed the *previous* run. TASK_WDT / PANIC / INT_WDT /
  // BROWNOUT point at completely different bugs — check this first after a crash.
  esp_reset_reason_t rr = esp_reset_reason();
  const char *rrName = "UNKNOWN";
  switch (rr) {
    case ESP_RST_POWERON:   rrName = "POWERON";   break;
    case ESP_RST_SW:        rrName = "SOFTWARE";  break;
    case ESP_RST_PANIC:     rrName = "PANIC";     break;
    case ESP_RST_INT_WDT:   rrName = "INT_WDT";   break;
    case ESP_RST_TASK_WDT:  rrName = "TASK_WDT";  break;
    case ESP_RST_WDT:       rrName = "OTHER_WDT"; break;
    case ESP_RST_BROWNOUT:  rrName = "BROWNOUT";  break;
    case ESP_RST_DEEPSLEEP: rrName = "DEEPSLEEP"; break;
    default: break;
  }
  Serial.printf("[Boot] reset reason: %s (%d)\n", rrName, (int)rr);
  // Increase watchdog timeout — sports+calendar fetch can take 30-60s
  esp_task_wdt_config_t wdt_config = { .timeout_ms = 60000, .idle_core_mask = 0, .trigger_panic = true };
  esp_task_wdt_reconfigure(&wdt_config);
  Serial.println(F("[Boot] start"));

  Storage::begin();

  Display::begin();
  // Apply saved brightness (Display::begin sets it to 200; override with saved value)
  Display::setBrightness(Storage::cfg.brightness);

  lvglMutex = xSemaphoreCreateMutex();
  lv_init();
  Display::initLvgl();

  esp_timer_handle_t tmr;
  esp_timer_create_args_t ta = {
    .callback = [](void*){ lv_tick_inc(LVGL_TICK_MS); },
    .name = "lv"
  };
  esp_timer_create(&ta, &tmr);
  esp_timer_start_periodic(tmr, LVGL_TICK_MS * 1000);

  // LVGL task — 6KB stack is enough for rendering
  xTaskCreatePinnedToCore(lvglTask, "LVGL", 12288, nullptr, 5, &lvglTaskHandle, 1);

  Serial.println(F("[Boot] display OK"));

  // Show splash immediately — sets dark background before WiFi connect delay
  if (xSemaphoreTake(lvglMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    ScreenManager::showSplash();
    xSemaphoreGive(lvglMutex);
  }

  if (!Storage::isConfigured()) {
    if (xSemaphoreTake(lvglMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
      ScreenManager::showCaptivePortalScreen();
      xSemaphoreGive(lvglMutex);
    }
    WiFiManager::startCaptivePortal();
    Storage::setConfigured(true);
    Storage::save();
    ESP.restart();
  }

  WiFiManager::connectSTA();
  if (!WiFiManager::isConnected()) {
    if (xSemaphoreTake(lvglMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
      ScreenManager::showCaptivePortalScreen();
      xSemaphoreGive(lvglMutex);
    }
    WiFiManager::startCaptivePortal();
    Storage::setConfigured(true);
    Storage::save();
    ESP.restart();
  }
  Serial.printf("[Boot] WiFi: %s\n", WiFiManager::getIP());

  WiFiManager::startSettingsServer();
  // NOTE: DataManager::begin() is now called at the top of mainTask, not here.
  // It does TLS work and this code runs on the 8KB loopTask stack.

  if (xSemaphoreTake(lvglMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    ScreenManager::showDashboard();
    Screensaver::begin(lv_scr_act(), Display::setBrightness);
  // Mount the photo partition up front: the screensaver checks count() on every
  // idle timeout, and an unmounted filesystem silently reports zero images.
  Slideshow::begin();
    xSemaphoreGive(lvglMutex);
  }

  // Web server task — dedicated so settings page is never blocked by data fetching.
  // 8KB: Arduino WebServer + Preferences + the String-heavy settings page building
  // needs well over the 4KB it had, which was a stack-overflow candidate.
  xTaskCreatePinnedToCore(webTask, "Web", 8192, nullptr, 2, &webTaskHandle, 0);

  // Main task — 40KB: it owns every TLS handshake (ip-api, Nominatim, Yahoo,
  // CoinGecko, Google iCal) and the 600KB iCal stream parse. 20KB was below what
  // mbedTLS needs and is the likely cause of a "stack overflow in task Main".
  xTaskCreatePinnedToCore(mainTask, "Main", 40960, nullptr, 3, &mainTaskHandle, 0);
  Serial.printf("[Boot] ready http://%s/\n", WiFiManager::getIP());
}

void loop() { vTaskDelay(pdMS_TO_TICKS(500)); }
