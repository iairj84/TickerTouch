#include "storage.h"

namespace Storage {

UserConfig cfg;
static Preferences prefs;

void begin() {
  prefs.begin(NVS_NAMESPACE, false);
  load();
}

void load() {
  cfg.configured   = prefs.getBool(NVS_KEY_CONFIGURED, false);
  cfg.theme        = prefs.getUChar(NVS_KEY_THEME, THEME_DARK);
  cfg.widgetMask   = prefs.getUInt(NVS_KEY_WIDGETS, WIDGETS_DEFAULT);
  cfg.sportsLeagues= (uint16_t)prefs.getUInt(NVS_KEY_SPORTS, 0b00001111); // NFL+NBA+NHL+MLB
  // If somehow 0, reset to defaults
  if (cfg.sportsLeagues == 0) cfg.sportsLeagues = 0b00001111;
  cfg.tickerSpeed  = prefs.getUChar("tspeed", 2);
  cfg.clock24h     = prefs.getBool("clock24h", false);

  prefs.getString(NVS_KEY_STOCKS, cfg.stocks, sizeof(cfg.stocks));
  if (strlen(cfg.stocks) == 0) strlcpy(cfg.stocks, "SPY,AAPL,TSLA,NVDA", sizeof(cfg.stocks));

  prefs.getString(NVS_KEY_CRYPTO, cfg.crypto, sizeof(cfg.crypto));
  if (strlen(cfg.crypto) == 0) strlcpy(cfg.crypto, "bitcoin,ethereum,solana", sizeof(cfg.crypto));

  prefs.getString("finnhub", cfg.finnhubKey, sizeof(cfg.finnhubKey));
  prefs.getString(NVS_KEY_CITY, cfg.city, sizeof(cfg.city));
  if (strlen(cfg.city) == 0) strlcpy(cfg.city, "New York", sizeof(cfg.city));
  prefs.getString("state", cfg.state, sizeof(cfg.state));

  cfg.lat = prefs.getFloat(NVS_KEY_LAT, 0.0f);  // 0 = not geocoded
  cfg.lon = prefs.getFloat(NVS_KEY_LON, 0.0f);
  prefs.getString("icalUrl", cfg.icalUrl, sizeof(cfg.icalUrl));
  cfg.tabMask = prefs.getUChar("tabMask", 0b0111); // Sports+Finance+Weather default
}

void save() {
  prefs.putBool(NVS_KEY_CONFIGURED, cfg.configured);
  prefs.putUChar(NVS_KEY_THEME, cfg.theme);
  prefs.putUInt(NVS_KEY_WIDGETS, cfg.widgetMask);
  prefs.putUInt(NVS_KEY_SPORTS, cfg.sportsLeagues);
  prefs.putUChar("tspeed", cfg.tickerSpeed);
  prefs.putBool("clock24h", cfg.clock24h);
  prefs.putString(NVS_KEY_STOCKS, cfg.stocks);
  prefs.putString(NVS_KEY_CRYPTO, cfg.crypto);
  prefs.putString(NVS_KEY_CITY, cfg.city);
  prefs.putString("state", cfg.state);
  prefs.putString("finnhub", cfg.finnhubKey);
  prefs.putFloat(NVS_KEY_LAT, cfg.lat);
  prefs.putFloat(NVS_KEY_LON, cfg.lon);
  prefs.putString("icalUrl", cfg.icalUrl);
  prefs.putUChar("tabMask", cfg.tabMask);
}

bool isConfigured() { return cfg.configured; }
void setConfigured(bool v) { cfg.configured = v; }

void reset() {
  prefs.clear();
  cfg = UserConfig{};
}

} // namespace Storage
