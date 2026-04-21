#include "wifi_manager.h"
#include "storage.h"
#include "data/data_manager.h"
#include <Preferences.h>

// Declared in TickerTouch.ino — must be outside any namespace
extern volatile bool gNeedRefresh;

namespace WiFiManager {

WebServer server(80);
DNSServer dnsServer;
bool      portalActive = false;

// ── WiFi scan endpoint ────────────────────────────────────────────────────────
static void handleScan() {
  int n = WiFi.scanNetworks();
  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i) json += ",";
    json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + WiFi.RSSI(i) +
            ",\"enc\":" + (WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "1" : "0") + "}";
  }
  json += "]";
  server.send(200, "application/json", json);
}

// ── Captive portal HTML ───────────────────────────────────────────────────────
static const char PORTAL_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="en">
<head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>TickerTouch Setup</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#0f0f14;color:#e8e8f0;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:1rem}
.card{background:#1a1a24;border:1px solid #2a2a3a;border-radius:16px;padding:2rem;max-width:480px;width:100%}
h1{font-size:1.4rem;font-weight:600;margin-bottom:.2rem}.sub{color:#888;font-size:.85rem;margin-bottom:1.5rem}
.section{margin-top:1.5rem;border-top:1px solid #2a2a3a;padding-top:1.25rem}
.section strong{font-size:.8rem;color:#aaa;text-transform:uppercase;letter-spacing:.05em}
label{display:block;font-size:.8rem;color:#aaa;margin:.75rem 0 .25rem}
input,select{width:100%;padding:.6rem .85rem;background:#12121a;border:1px solid #2a2a3a;border-radius:8px;color:#e8e8f0;font-size:.95rem;outline:none}
input:focus,select:focus{border-color:#6366f1}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:.5rem;margin-top:.5rem}
.check{display:flex;align-items:center;gap:.5rem;padding:.5rem;background:#12121a;border:1px solid #2a2a3a;border-radius:8px;cursor:pointer}
.check input{width:auto}
.btn{display:block;width:100%;margin-top:1.5rem;padding:.75rem;background:#6366f1;color:#fff;border:none;border-radius:8px;font-size:1rem;font-weight:600;cursor:pointer}
.wifi-list{margin-top:.5rem;display:flex;flex-direction:column;gap:.4rem;max-height:180px;overflow-y:auto}
.wifi-item{display:flex;align-items:center;gap:.6rem;padding:.6rem .85rem;background:#12121a;border:1px solid #2a2a3a;border-radius:8px;cursor:pointer}
.wifi-item.sel{border-color:#6366f1;background:#1a1a30}
.scan-btn{margin-top:.5rem;padding:.45rem 1rem;background:transparent;border:1px solid #2a2a3a;border-radius:6px;color:#aaa;cursor:pointer;font-size:.85rem}
#pw-row{display:none;margin-top:.5rem}
</style></head>
<body><div class="card">
<div style="font-size:1.8rem;margin-bottom:.4rem">📊</div>
<h1>TickerTouch Setup</h1><p class="sub">First-time configuration</p>
<form id="f" action="/save" method="POST">
<input type="hidden" name="ssid" id="h_ssid">
<input type="hidden" name="pass" id="h_pass">
<div class="section"><strong>WiFi Network</strong>
<div class="wifi-list" id="wlist"><em style="color:#666;font-size:.85rem">Tap Scan</em></div>
<button type="button" class="scan-btn" onclick="doScan()">⟳ Scan</button>
<div id="pw-row"><label>Password for <span id="pw-label"></span></label>
<input type="password" id="pw" oninput="document.getElementById('h_pass').value=this.value"></div></div>
<div class="section"><strong>Location</strong>
<label>City name</label>
<input type="text" name="city" placeholder="Portland" value="Portland">
<label>State / Region <small style="color:#888">(e.g. Oregon, helps resolve ambiguous cities)</small></label>
<input type="text" name="state" placeholder="Oregon"></div>
<div class="section"><strong>Sports</strong>
<div class="grid">
<label class="check"><input type="checkbox" name="nfl" checked> NFL</label>
<label class="check"><input type="checkbox" name="nba" checked> NBA</label>
<label class="check"><input type="checkbox" name="nhl" checked> NHL</label>
<label class="check"><input type="checkbox" name="mlb" checked> MLB</label>
<label class="check"><input type="checkbox" name="cfb"> College FB</label>
<label class="check"><input type="checkbox" name="mls"> MLS</label>
<label class="check"><input type="checkbox" name="epl"> EPL</label>
<label class="check"><input type="checkbox" name="cbb"> College BB</label>
</div></div>
<div class="section"><strong>Stocks</strong>
<label>Tickers (comma-separated)</label>
<input type="text" name="stocks" value="SPY,AAPL,TSLA,NVDA"></div>
<div class="section"><strong>Stock Data API Key</strong>
<label>Finnhub API Key <small style="color:#6366f1">(<a href="https://finnhub.io" target="_blank" style="color:#6366f1">free at finnhub.io</a>)</small></label>
<input type="text" name="finnhub" placeholder="Paste your free Finnhub key here"></div>
<div class="section"><strong>Theme</strong>
<select name="theme">
<option value="0">Dark</option><option value="3">Clean</option>
<option value="1">Retro</option><option value="2">Neon</option><option value="4">Sports</option>
</select></div>
<button class="btn" type="button" onclick="go()">Save &amp; Connect →</button>
</form></div>
<script>
var sel='';
function doScan(){
  var el=document.getElementById('wlist');
  el.innerHTML='<em style="color:#666">Scanning...</em>';
  fetch('/scan').then(r=>r.json()).then(nets=>{
    nets.sort((a,b)=>b.rssi-a.rssi); el.innerHTML='';
    nets.forEach(n=>{
      var d=document.createElement('div'); d.className='wifi-item';
      d.innerHTML='<span style="flex:1">'+n.ssid+'</span>'+(n.enc?'🔒':'');
      d.onclick=function(){
        document.querySelectorAll('.wifi-item').forEach(x=>x.classList.remove('sel'));
        d.classList.add('sel'); sel=n.ssid;
        document.getElementById('h_ssid').value=n.ssid;
        var pr=document.getElementById('pw-row');
        pr.style.display=n.enc?'block':'none';
        document.getElementById('pw-label').textContent=n.ssid;
        if(!n.enc) document.getElementById('h_pass').value='';
      }; el.appendChild(d);
    });
    if(!nets.length) el.innerHTML='<em style="color:#666">None found</em>';
  }).catch(()=>{el.innerHTML='<em style="color:#f87">Scan failed</em>';});
}
function go(){
  if(!document.getElementById('h_ssid').value){alert('Select a network first');return;}
  document.getElementById('f').submit();
}
window.onload=doScan;
</script></body></html>
)HTML";

static const char SAVED_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head><meta charset="UTF-8">
<style>body{font-family:sans-serif;background:#0f0f14;color:#e8e8f0;display:flex;align-items:center;justify-content:center;height:100vh;text-align:center}</style>
</head><body><div><div style="font-size:3rem">✅</div><h1>Connected!</h1><p style="color:#888;margin-top:.5rem">Restarting...</p></div></body></html>
)HTML";

// ── Settings page (served while connected) ────────────────────────────────────
static String settingsPage() {
  String h = F("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>TickerTouch Settings</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;background:#0f0f14;color:#e8e8f0;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:1rem}"
    ".card{background:#1a1a24;border:1px solid #2a2a3a;border-radius:16px;padding:2rem;max-width:500px;width:100%}"
    "h1{font-size:1.3rem;font-weight:600;margin-bottom:.1rem}"
    ".sub{color:#6366f1;font-size:.8rem;margin-bottom:1.5rem}"
    ".section{margin-top:1.25rem;border-top:1px solid #2a2a3a;padding-top:1rem}"
    "strong{font-size:.75rem;color:#aaa;text-transform:uppercase;letter-spacing:.05em}"
    "label{display:block;font-size:.8rem;color:#aaa;margin:.6rem 0 .2rem}"
    "input,select{width:100%;padding:.55rem .8rem;background:#12121a;border:1px solid #2a2a3a;border-radius:8px;color:#e8e8f0;font-size:.9rem;outline:none}"
    "input:focus,select:focus{border-color:#6366f1}"
    ".grid{display:grid;grid-template-columns:1fr 1fr;gap:.4rem;margin-top:.4rem}"
    ".ck{display:flex;align-items:center;gap:.4rem;padding:.45rem .6rem;background:#12121a;border:1px solid #2a2a3a;border-radius:6px;cursor:pointer;font-size:.85rem}"
    ".ck input{width:auto}"
    ".btn{display:block;width:100%;margin-top:1.25rem;padding:.7rem;background:#6366f1;color:#fff;border:none;border-radius:8px;font-size:.95rem;font-weight:600;cursor:pointer}"
    ".btn:hover{background:#4f46e5}"
    ".info{background:#12121a;border:1px solid #2a2a3a;border-radius:6px;padding:.6rem;font-size:.75rem;color:#6366f1;margin-top:.5rem}"
    "</style></head><body><div class='card'>"
    "<div style='font-size:1.5rem;margin-bottom:.3rem'>📊</div>"
    "<h1>TickerTouch Settings</h1>");

  // Show IP
  h += "<p class='sub'>Device IP: ";
  h += WiFi.localIP().toString();
  h += " &nbsp;|&nbsp; Open this page: http://";
  h += WiFi.localIP().toString();
  h += "/</p>";

  h += "<form method='POST' action='/save'>";

  // Location
  h += "<div class='section'><strong>Location &amp; Weather</strong>";
  h += "<label>City name</label>";
  h += "<input type='text' name='city' placeholder='Portland' value='";
  h += Storage::cfg.city;
  h += "'>";
  h += "<label>State / Region <small style='color:#888'>(helps resolve ambiguous cities)</small></label>";
  h += "<input type='text' name='state' placeholder='Oregon' value='";
  h += Storage::cfg.state;
  h += "'>";
  if (Storage::cfg.lat != 0.0f) {
    h += "<div class='info' style='color:#22c55e'>&#10003; Location: ";
    h += String(Storage::cfg.lat, 4);
    h += ", ";
    h += String(Storage::cfg.lon, 4);
    h += "</div>";
  } else {
    h += "<div class='info' style='color:#f59e0b'>&#9888; No coordinates yet — save to geocode</div>";
  }
  h += "</div>";

  // Sports
  h += "<div class='section'><strong>Sports</strong><div class='grid'>";
  struct { const char*n; const char*l; uint16_t b; } sp[]={
    {"nfl","NFL",1<<0},{"nba","NBA",1<<1},{"nhl","NHL",1<<2},{"mlb","MLB",1<<3},
    {"cfb","College FB",1<<4},{"mls","MLS",1<<5},{"epl","EPL",1<<6},{"cbb","College BB",1<<7}
  };
  for (auto &s:sp) {
    h += "<label class='ck'><input type='checkbox' name='";
    h += s.n; h += "'";
    if (Storage::cfg.sportsLeagues & s.b) h += " checked";
    h += "> "; h += s.l; h += "</label>";
  }
  h += "</div></div>";

  // Stocks
  h += "<div class='section'><strong>Stocks</strong>";
  h += "<label>Tickers (comma-separated, e.g. AAPL,MSFT,SPY)</label>";
  h += "<input type='text' name='stocks' value='";
  h += Storage::cfg.stocks;
  h += "'>";
  h += "<small style='color:#888'>US stocks &amp; ETFs. Data from stooq.com - no API key needed.</small>";
  h += "</div>";

  // Theme
  h += "<div class='section'><strong>Display Theme</strong><select name='theme'>";
  const char* themes[]={"Dark","Retro","Neon","Clean","Sports"};
  int tvals[]={0,1,2,3,4};
  for (int i=0;i<5;i++) {
    h += "<option value='"; h += tvals[i]; h += "'";
    if (Storage::cfg.theme==tvals[i]) h += " selected";
    h += ">"; h += themes[i]; h += "</option>";
  }
  h += "</select></div>";

  // Calendar
  h += "<div class='section'><strong>Calendar</strong>";
  h += "<label>iCal URL <small style='color:#888'>(Google/Outlook/Apple iCal feed)</small></label>";
  h += "<input type='text' name='icalUrl' placeholder='https://calendar.google.com/calendar/ical/...' value='";
  h += Storage::cfg.icalUrl;
  h += "'>";
  h += "<small style='color:#888'><b>Google:</b> Settings &rarr; pick ONE specific calendar (not 'My calendars') &rarr; 'Secret address in iCal format'. ";
  h += "Using a single calendar with only upcoming events keeps the feed small and fast. ";
  h += "<b>Outlook:</b> Calendar &rarr; Share &rarr; Get a link &rarr; ICS. ";
  h += "<b>Apple:</b> iCloud.com &rarr; Calendar &rarr; share icon &rarr; Public Calendar URL.</small></div>";

  // Tab visibility
  h += "<div class='section'><strong>Visible Tabs</strong><div class='grid'>";
  struct { const char*n; const char*l; uint8_t b; } tbls[]={
    {"tab_sports","Sports",0x01},{"tab_fin","Finance",0x02},
    {"tab_weather","Weather",0x04},{"tab_cal","Calendar",0x08}
  };
  for (auto &t:tbls) {
    h += "<label class='ck'><input type='checkbox' name='";
    h += t.n; h += "'";
    if (Storage::cfg.tabMask & t.b) h += " checked";
    h += "> "; h += t.l; h += "</label>";
  }
  h += "</div></div>";

  // WiFi section
  h += "<div class='section'><strong>Change WiFi</strong>";
  h += "<label>New network (leave blank to keep current)</label>";
  h += "<input type='text' name='new_ssid' placeholder='Network name'>";
  h += "<label>Password</label>";
  h += "<input type='password' name='new_pass' placeholder='Password'></div>";

  h += "<button class='btn' type='submit'>Save Settings</button>";
  h += "</form></div></body></html>";
  return h;
}

static void handleSettingsGet() {
  server.send(200, "text/html", settingsPage());
}

static void handleSettingsSave() {
  // City + state — reset coords if either changes so geocode re-runs
  String city  = server.arg("city");
  String state = server.arg("state");
  city.trim(); state.trim();
  bool locationChanged = (city.length() && city != String(Storage::cfg.city)) ||
                         (state != String(Storage::cfg.state));
  if (locationChanged) {
    Storage::cfg.lat = 0.0f;
    Storage::cfg.lon = 0.0f;
  }
  if (city.length())  city.toCharArray(Storage::cfg.city,   sizeof(Storage::cfg.city));
  if (state.length()) state.toCharArray(Storage::cfg.state, sizeof(Storage::cfg.state));
  else if (locationChanged) Storage::cfg.state[0] = '\0';

  // Sports — build bitmask from submitted checkboxes
  uint16_t sports = 0;
  if (server.arg("nfl").length()) sports |= (1<<0);
  if (server.arg("nba").length()) sports |= (1<<1);
  if (server.arg("nhl").length()) sports |= (1<<2);
  if (server.arg("mlb").length()) sports |= (1<<3);
  if (server.arg("cfb").length()) sports |= (1<<4);
  if (server.arg("mls").length()) sports |= (1<<5);
  if (server.arg("epl").length()) sports |= (1<<6);
  if (server.arg("cbb").length()) sports |= (1<<7);
  Storage::cfg.sportsLeagues = sports;
  Serial.printf("[Settings] saved leagues bitmask=%d\n", sports);

  // Stocks
  String stocks = server.arg("stocks");
  stocks.trim();
  if (stocks.length()) stocks.toCharArray(Storage::cfg.stocks, sizeof(Storage::cfg.stocks));

  // Crypto — removed (HTTPS not supported on this hardware)
  // String crypto = server.arg("crypto");

  // Finnhub API key
  String finnhub = server.arg("finnhub");
  finnhub.trim();
  if (finnhub.length())
    finnhub.toCharArray(Storage::cfg.finnhubKey, sizeof(Storage::cfg.finnhubKey));

  // iCal URL
  String icalUrl = server.arg("icalUrl");
  icalUrl.trim();
  icalUrl.toCharArray(Storage::cfg.icalUrl, sizeof(Storage::cfg.icalUrl));

  // Tab visibility bitmask
  Storage::cfg.tabMask = 0;
  if (server.hasArg("tab_sports"))  Storage::cfg.tabMask |= 0x01;
  if (server.hasArg("tab_fin"))     Storage::cfg.tabMask |= 0x02;
  if (server.hasArg("tab_weather")) Storage::cfg.tabMask |= 0x04;
  if (server.hasArg("tab_cal"))     Storage::cfg.tabMask |= 0x08;

  // Theme
  String theme = server.arg("theme");
  if (theme.length()) Storage::cfg.theme = (uint8_t)theme.toInt();

  // Save everything to NVS
  Storage::save();
  Serial.printf("[Settings] saved: city=%s stocks=%s crypto=%s sports=%d\n",
    Storage::cfg.city, Storage::cfg.stocks, Storage::cfg.crypto, Storage::cfg.sportsLeagues);

  // WiFi change (only if new SSID provided)
  String newSSID = server.arg("new_ssid");
  newSSID.trim();
  if (newSSID.length()) {
    Preferences prefs;
    prefs.begin("tickertch_wifi", false);
    prefs.putString("ssid", newSSID);
    prefs.putString("pass", server.arg("new_pass"));
    prefs.end();
    server.send(200, "text/html",
      "<html><head><meta http-equiv='refresh' content='3;url=/'></head>"
      "<body style='background:#0f0f14;color:#e8e8f0;font-family:sans-serif;display:flex;align-items:center;justify-content:center;height:100vh;text-align:center'>"
      "<div><h2>Saved</h2><p style='color:#6366f1;margin-top:.5rem'>Restarting...</p></div></body></html>");
    delay(2000);
    ESP.restart();
    return;
  }

  // Redirect back to settings page with success message
  server.send(200, "text/html",
    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<meta http-equiv='refresh' content='2;url=/'></head>"
    "<body style='background:#0f0f14;color:#e8e8f0;font-family:sans-serif;"
    "display:flex;align-items:center;justify-content:center;height:100vh;text-align:center'>"
    "<div><h2 style='font-size:2rem'>OK</h2>"
    "<p style='color:#6366f1;margin-top:.5rem'>Saved! Returning...</p>"
    "</div></body></html>");

  // Set a flag — picked up by mainTask on next tick
  gNeedRefresh = true;
}

// ── Captive portal save handler ───────────────────────────────────────────────
static String savedSSID, savedPass;
static volatile bool credentialsSaved = false;

static void handlePortalRoot() { server.send(200, "text/html", PORTAL_HTML); }

static void handlePortalSave() {
  savedSSID = server.arg("ssid");
  savedPass = server.arg("pass");

  String city = server.arg("city");
  if (city.length()) city.toCharArray(Storage::cfg.city, sizeof(Storage::cfg.city));
  String state = server.arg("state");
  state.trim();
  if (state.length()) state.toCharArray(Storage::cfg.state, sizeof(Storage::cfg.state));
  // Always reset coords on first-time setup so geocoding runs fresh
  Storage::cfg.lat = 0.0f;
  Storage::cfg.lon = 0.0f;

  uint16_t sports = 0;
  if (server.arg("nfl").length()) sports |= (1<<0);
  if (server.arg("nba").length()) sports |= (1<<1);
  if (server.arg("nhl").length()) sports |= (1<<2);
  if (server.arg("mlb").length()) sports |= (1<<3);
  if (server.arg("cfb").length()) sports |= (1<<4);
  if (server.arg("mls").length()) sports |= (1<<5);
  if (server.arg("epl").length()) sports |= (1<<6);
  if (server.arg("cbb").length()) sports |= (1<<7);
  Storage::cfg.sportsLeagues = sports;

  String stocks = server.arg("stocks");
  if (stocks.length()) stocks.toCharArray(Storage::cfg.stocks, sizeof(Storage::cfg.stocks));
  String finnhub = server.arg("finnhub");
  finnhub.trim();
  if (finnhub.length()) finnhub.toCharArray(Storage::cfg.finnhubKey, sizeof(Storage::cfg.finnhubKey));
  Storage::cfg.theme = (uint8_t)server.arg("theme").toInt();

  Preferences prefs;
  prefs.begin("tickertch_wifi", false);
  prefs.putString("ssid", savedSSID);
  prefs.putString("pass", savedPass);
  prefs.end();

  server.send(200, "text/html", SAVED_HTML);
  credentialsSaved = true;
}

static void handleNotFound() {
  server.sendHeader("Location", "http://192.168.4.1", true);
  server.send(302, "text/plain", "Redirect");
}

const char* portalURL() { return "192.168.4.1"; }

// ── Public API ────────────────────────────────────────────────────────────────
void startCaptivePortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("[WiFi] AP: %s\n", WiFi.softAPIP().toString().c_str());

  dnsServer.start(53, "*", WiFi.softAPIP());
  server.on("/",      HTTP_GET,  handlePortalRoot);
  server.on("/scan",  HTTP_GET,  handleScan);
  server.on("/save",  HTTP_POST, handlePortalSave);
  server.onNotFound(handleNotFound);
  server.begin();
  portalActive = true;

  while (!credentialsSaved) {
    dnsServer.processNextRequest();
    server.handleClient();
    delay(2);
  }

  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSSID.c_str(), savedPass.c_str());
  uint32_t t = millis();
  while (WiFi.status()!=WL_CONNECTED && millis()-t < WIFI_TIMEOUT_MS) {
    delay(250); Serial.print('.');
  }
  Serial.println();
  if (WiFi.status()==WL_CONNECTED)
    Serial.printf("[WiFi] Connected: %s\n", WiFi.localIP().toString().c_str());
  else
    Serial.println(F("[WiFi] Connect failed"));
}

void connectSTA() {
  Preferences prefs;
  prefs.begin("tickertch_wifi", true);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();
  if (ssid.isEmpty()) { Serial.println(F("[WiFi] No credentials")); return; }
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t t = millis();
  while (WiFi.status()!=WL_CONNECTED && millis()-t < WIFI_TIMEOUT_MS) {
    delay(250); Serial.print('.');
  }
  Serial.println();
  if (WiFi.status()==WL_CONNECTED)
    Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
  else
    Serial.println(F("[WiFi] Failed"));
}

void startSettingsServer() {
  server.on("/",      HTTP_GET,  handleSettingsGet);
  server.on("/save",  HTTP_POST, handleSettingsSave);
  server.on("/scan",  HTTP_GET,  handleScan);
  server.begin();
  Serial.printf("[WiFi] Settings at http://%s/\n", WiFi.localIP().toString().c_str());
}

void handleSettingsServer() { server.handleClient(); }

bool isConnected()  { return WiFi.status() == WL_CONNECTED; }
const char* getIP() { static String ip; ip = WiFi.localIP().toString(); return ip.c_str(); }

} // namespace WiFiManager
