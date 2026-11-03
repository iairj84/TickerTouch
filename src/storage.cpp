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
  cfg.brightness   = prefs.getUChar("brightness", 200);
  cfg.clock24h     = prefs.getBool("clock24h", false);
  cfg.saverSec     = prefs.getUInt("saverSec", 600);

  prefs.getString(NVS_KEY_STOCKS, cfg.stocks, sizeof(cfg.stocks));
  if (strlen(cfg.stocks) == 0) strlcpy(cfg.stocks, "SPY,AAPL,TSLA,NVDA", sizeof(cfg.stocks));

  // Crypto: an empty list is a valid choice ("no crypto"), not a missing value.
  // getString() leaves the buffer untouched when the stored value is empty, and the
  // old fallback re-seeded the defaults on every boot, so clearing crypto silently
  // undid itself at the next restart. Only a never-configured device gets defaults.
  cfg.crypto[0] = '\0';
  prefs.getString(NVS_KEY_CRYPTO, cfg.crypto, sizeof(cfg.crypto));
  if (strlen(cfg.crypto) == 0 && !cfg.configured)
    strlcpy(cfg.crypto, "bitcoin,ethereum,solana", sizeof(cfg.crypto));

  prefs.getString("finnhub", cfg.finnhubKey, sizeof(cfg.finnhubKey));
  prefs.getString(NVS_KEY_CITY, cfg.city, sizeof(cfg.city));
  if (strlen(cfg.city) == 0) strlcpy(cfg.city, "New York", sizeof(cfg.city));
  prefs.getString("state", cfg.state, sizeof(cfg.state));

  cfg.lat = prefs.getFloat(NVS_KEY_LAT, 0.0f);
  cfg.lon = prefs.getFloat(NVS_KEY_LON, 0.0f);
  cfg.weatherCityCount = prefs.getUChar("wcityCount", 1);
  if (cfg.weatherCityCount < 1) cfg.weatherCityCount = 1;
  if (cfg.weatherCityCount > 3) cfg.weatherCityCount = 3;
  prefs.getString("city2", cfg.extraCities[0].city,  sizeof(cfg.extraCities[0].city));
  prefs.getString("state2",cfg.extraCities[0].state, sizeof(cfg.extraCities[0].state));
  cfg.extraCities[0].lat = prefs.getFloat("lat2", 0.0f);
  cfg.extraCities[0].lon = prefs.getFloat("lon2", 0.0f);
  prefs.getString("city3", cfg.extraCities[1].city,  sizeof(cfg.extraCities[1].city));
  prefs.getString("state3",cfg.extraCities[1].state, sizeof(cfg.extraCities[1].state));
  cfg.extraCities[1].lat = prefs.getFloat("lat3", 0.0f);
  cfg.extraCities[1].lon = prefs.getFloat("lon3", 0.0f);
  prefs.getString("icalUrl", cfg.icalUrl, sizeof(cfg.icalUrl));
  prefs.getString("icalUrl2", cfg.icalUrl2, sizeof(cfg.icalUrl2));
  cfg.calIcon1 = prefs.getUChar("calIcon1", CALICON_HOME);
  cfg.calIcon2 = prefs.getUChar("calIcon2", CALICON_KEYBOARD);
  if (cfg.calIcon1 >= CALICON_COUNT) cfg.calIcon1 = CALICON_HOME;
  if (cfg.calIcon2 >= CALICON_COUNT) cfg.calIcon2 = CALICON_KEYBOARD;
  cfg.tabMask = prefs.getUChar("tabMask", 0b0111);
  prefs.getString("teamFilter", cfg.teamFilter, sizeof(cfg.teamFilter));
  prefs.getString("cfbConf", cfg.cfbConf, sizeof(cfg.cfbConf)); // Sports+Finance+Weather default
}

void save() {
  prefs.putBool(NVS_KEY_CONFIGURED, cfg.configured);
  prefs.putUChar(NVS_KEY_THEME, cfg.theme);
  prefs.putUInt(NVS_KEY_WIDGETS, cfg.widgetMask);
  prefs.putUInt(NVS_KEY_SPORTS, cfg.sportsLeagues);
  prefs.putUChar("tspeed", cfg.tickerSpeed);
  prefs.putUChar("brightness", cfg.brightness);
  prefs.putBool("clock24h", cfg.clock24h);
  prefs.putUInt("saverSec", cfg.saverSec);
  prefs.putString(NVS_KEY_STOCKS, cfg.stocks);
  prefs.putString(NVS_KEY_CRYPTO, cfg.crypto);
  prefs.putString(NVS_KEY_CITY, cfg.city);
  prefs.putString("state", cfg.state);
  prefs.putString("finnhub", cfg.finnhubKey);
  prefs.putFloat(NVS_KEY_LAT, cfg.lat);
  prefs.putFloat(NVS_KEY_LON, cfg.lon);
  prefs.putUChar("wcityCount", cfg.weatherCityCount);
  prefs.putString("city2",  cfg.extraCities[0].city);
  prefs.putString("state2", cfg.extraCities[0].state);
  prefs.putFloat("lat2",    cfg.extraCities[0].lat);
  prefs.putFloat("lon2",    cfg.extraCities[0].lon);
  prefs.putString("city3",  cfg.extraCities[1].city);
  prefs.putString("state3", cfg.extraCities[1].state);
  prefs.putFloat("lat3",    cfg.extraCities[1].lat);
  prefs.putFloat("lon3",    cfg.extraCities[1].lon);
  prefs.putString("icalUrl", cfg.icalUrl);
  prefs.putString("icalUrl2", cfg.icalUrl2);
  prefs.putUChar("calIcon1", cfg.calIcon1);
  prefs.putUChar("calIcon2", cfg.calIcon2);
  prefs.putUChar("tabMask", cfg.tabMask);
  prefs.putString("teamFilter", cfg.teamFilter);
  prefs.putString("cfbConf", cfg.cfbConf);
}

bool isConfigured() { return cfg.configured; }
void setConfigured(bool v) { cfg.configured = v; }

void reset() {
  prefs.clear();
  cfg = UserConfig{};
}

} // namespace Storage
