/**
 * TickerTouch — JC4827W543 (ESP32-S3)
 * Core 1: LVGL + UI (under mutex)
 * Core 0: WiFi data fetches + web server
 */

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <time.h>
#include "config.h"
#include "src/wifi_manager.h"
#include "src/display.h"
#include "src/storage.h"
#include "src/ticker_engine.h"
#include "src/screensaver.h"
#include "src/data/data_manager.h"
#include "src/screens/screen_manager.h"

TaskHandle_t      lvglTaskHandle = nullptr;
TaskHandle_t      mainTaskHandle = nullptr;
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
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  // Increase watchdog timeout — sports+calendar fetch can take 30-60s
  esp_task_wdt_config_t wdt_config = { .timeout_ms = 60000, .idle_core_mask = 0, .trigger_panic = true };
  esp_task_wdt_reconfigure(&wdt_config);
  Serial.println(F("[Boot] start"));

  Storage::begin();

  Display::begin();

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
  xTaskCreatePinnedToCore(lvglTask, "LVGL", 6144, nullptr, 5, &lvglTaskHandle, 1);

  Serial.println(F("[Boot] display OK"));

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
  DataManager::begin();

  if (xSemaphoreTake(lvglMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    ScreenManager::showDashboard();
    Screensaver::begin(lv_scr_act(), Display::setBrightness);
    xSemaphoreGive(lvglMutex);
  }

  // Web server task — dedicated so settings page is never blocked by data fetching
  static TaskHandle_t webTaskHandle = nullptr;
  xTaskCreatePinnedToCore(webTask, "Web", 4096, nullptr, 2, &webTaskHandle, 0);

  // Main task — 20KB stack (HTTP responses can be large)
  xTaskCreatePinnedToCore(mainTask, "Main", 20480, nullptr, 3, &mainTaskHandle, 0);
  Serial.printf("[Boot] ready http://%s/\n", WiFiManager::getIP());
}

void loop() { vTaskDelay(pdMS_TO_TICKS(500)); }
