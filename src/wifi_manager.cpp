#include "wifi_manager.h"
#include "storage.h"
#include "data/data_manager.h"
#include "sports_teams.h"
#include "slideshow.h"
#include <Preferences.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include "screensaver.h"

// Declared in TickerTouch.ino — must be outside any namespace
extern volatile bool gNeedRefresh;
extern TaskHandle_t  lvglTaskHandle;
extern TaskHandle_t  mainTaskHandle;
extern TaskHandle_t  webTaskHandle;

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
.note{font-size:.78rem;color:#555;margin-top:.3rem}
</style></head>
<body><div class="card">
<div style="font-size:1.8rem;margin-bottom:.4rem">📊</div>
<h1>TickerTouch Setup</h1><p class="sub">First-time configuration - you can change all of this later</p>
<form id="f" action="/save" method="POST">
<input type="hidden" name="ssid" id="h_ssid">
<input type="hidden" name="pass" id="h_pass">

<div class="section"><strong>WiFi Network</strong>
<div class="wifi-list" id="wlist"><em style="color:#666;font-size:.85rem">Tap Scan</em></div>
<button type="button" class="scan-btn" onclick="doScan()">⟳ Scan</button>
<div id="pw-row"><label>Password for <span id="pw-label"></span></label>
<input type="password" id="pw" oninput="document.getElementById('h_pass').value=this.value"></div>
</div>

<div class="section"><strong>Location</strong>
<label>City</label>
<input type="text" name="city" placeholder="Portland">
<label>State / Region</label>
<input type="text" name="state" placeholder="Oregon">
</div>

<div class="section"><strong>Sports Leagues</strong>
<div class="grid">
<label class="check"><input type="checkbox" name="nfl"> NFL</label>
<label class="check"><input type="checkbox" name="nba" checked> NBA</label>
<label class="check"><input type="checkbox" name="nhl" checked> NHL</label>
<label class="check"><input type="checkbox" name="mlb" checked> MLB</label>
<label class="check"><input type="checkbox" name="cfb"> College FB</label>
<label class="check"><input type="checkbox" name="mls"> MLS</label>
<label class="check"><input type="checkbox" name="epl"> EPL</label>
<label class="check"><input type="checkbox" name="cbb"> College BB</label>
<label class="check"><input type="checkbox" name="wnba"> WNBA</label>
<label class="check"><input type="checkbox" name="nascar"> NASCAR</label>
<label class="check"><input type="checkbox" name="f1"> F1</label>
<label class="check"><input type="checkbox" name="indycar"> IndyCar</label>
<label class="check"><input type="checkbox" name="pga"> PGA Golf</label>
</div></div>

<div class="section"><strong>Stocks</strong>
<label>Tickers (comma-separated, e.g. AAPL, MSFT, SPY)</label>
<input type="text" name="stocks" value="SPY,AAPL,TSLA,NVDA">
<p class="note">Prices via Yahoo Finance - no API key needed.</p>
</div>

<div class="section"><strong>Theme</strong>
<select name="theme">
<option value="0">Dark</option>
<option value="3">Clean</option>
<option value="1">Retro</option>
<option value="2">Neon</option>
<option value="4">Sports</option>
</select></div>

<button class="btn" type="button" onclick="go()">Save &amp; Connect</button>
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

  // Location + Weather Cities
  h += "<div class='section'><strong>Location &amp; Weather</strong>";
  h += "<label>City 1 <small style='color:#888'>(primary)</small></label>";
  h += "<div style='display:flex;gap:.5rem'>";
  h += "<input type='text' name='city' placeholder='Portland' value='";
  h += Storage::cfg.city; h += "' style='flex:2'>";
  h += "<input type='text' name='state' placeholder='State' value='";
  h += Storage::cfg.state; h += "' style='flex:1'>";
  h += "</div>";
  if (Storage::cfg.lat != 0.0f) {
    h += "<div class='info' style='color:#22c55e'>&#10003; Geocoded: ";
    h += String(Storage::cfg.lat,4); h += ", "; h += String(Storage::cfg.lon,4);
    h += "</div>";
  } else {
    h += "<div class='info' style='color:#f59e0b'>&#9888; Not geocoded yet — save to geocode</div>";
  }

  // Extra cities
  for (int i = 0; i < 2; i++) {
    auto &ec = Storage::cfg.extraCities[i];
    h += "<label style='margin-top:8px'>City "; h += String(i+2);
    h += " <small style='color:#888'>(optional)</small></label>";
    h += "<div style='display:flex;gap:.5rem'>";
    h += "<input type='text' name='city"; h += String(i+2);
    h += "' placeholder='e.g. Seattle' value='"; h += ec.city; h += "' style='flex:2'>";
    h += "<input type='text' name='state"; h += String(i+2);
    h += "' placeholder='State' value='"; h += ec.state; h += "' style='flex:1'>";
    h += "</div>";
    if (ec.lat != 0.0f) {
      h += "<div class='info' style='color:#22c55e;font-size:.72rem'>&#10003; ";
      h += String(ec.lat,4); h += ", "; h += String(ec.lon,4); h += "</div>";
    }
  }
  h += "</div>";

  // Sports
  h += "<div class='section'><strong>Sports</strong><div class='grid'>";
  struct { const char*n; const char*l; uint16_t b; } sp[]={
    {"nfl","NFL",1<<0},{"nba","NBA",1<<1},{"nhl","NHL",1<<2},{"mlb","MLB",1<<3},
    {"cfb","College FB",1<<4},{"mls","MLS",1<<5},{"epl","EPL",1<<6},{"cbb","College BB",1<<7},
    {"wnba","WNBA",1<<8},{"nascar","NASCAR",1<<9},{"f1","F1",1<<10},
    {"indycar","IndyCar",1<<11},{"pga","PGA Golf",1<<12}
  };
  for (auto &s:sp) {
    h += "<label class='ck'><input type='checkbox' name='";
    h += s.n; h += "'";
    if (Storage::cfg.sportsLeagues & s.b) h += " checked";
    h += "> "; h += s.l; h += "</label>";
  }
  h += "</div></div>";

  // Sports Filter — all leagues shown always; disabled ones appear dimmed
  h += "<div class='section'>"
       "<div style='display:flex;align-items:center;justify-content:space-between;"
       "cursor:pointer;padding:.2rem 0' onclick=\"tog('sf')\">"
       "<strong>Sports Filter</strong>"
       "<span id='sf_arr' style='color:#6366f1;font-size:.8rem'>&#9660; expand</span>"
       "</div>"
       "<div id='sf' style='display:none;margin-top:.5rem'>"
       "<p style='color:#888;font-size:.82rem;margin-bottom:.6rem'>"
       "Select teams to show only their games. Leave all unselected to show everything. "
       "Leagues shown in grey are not currently enabled in the Sports section above.</p>"
       "<input type='hidden' name='teamFilter' id='tfv' value='";
  h += Storage::cfg.teamFilter;
  h += "'><input type='hidden' name='cfbConf' id='cfv' value='";
  h += Storage::cfg.cfbConf;
  h += "'>";

  struct LgDef { const char *id,*label; const TeamDef *teams; int tc; const ConfDef *confs; int cc; uint16_t bit; };
  LgDef lgs[] = {
    {"NFL","NFL",  NFL_TEAMS, 32,nullptr,0,1<<0},
    {"NBA","NBA",  NBA_TEAMS, 30,nullptr,0,1<<1},
    {"NHL","NHL",  NHL_TEAMS, 33,nullptr,0,1<<2},
    {"MLB","MLB",  MLB_TEAMS, 30,nullptr,0,1<<3},
    {"CFB","College Football",nullptr,0,CFB_CONFS,12,1<<4},
    {"MLS","MLS",  MLS_TEAMS, 29,nullptr,0,1<<5},
    {"EPL","EPL",  EPL_TEAMS, 20,nullptr,0,1<<6},
    {"CBB","College Basketball",nullptr,0,CBB_CONFS,11,1<<7},
    {"WNBA","WNBA",WNBA_TEAMS,15,nullptr,0,1<<8},
  };
  uint16_t sl = Storage::cfg.sportsLeagues;

  for (auto &lg : lgs) {
    bool on = (sl & lg.bit) != 0;
    h += "<div style='margin-top:.4rem;border:1px solid ";
    h += on ? "#2a2a3a" : "#1e1e28";
    h += ";border-radius:8px;overflow:hidden'>";
    h += "<div onclick=\"tog('l_"; h += lg.id; h += "')\""
         " style='display:flex;justify-content:space-between;align-items:center;"
         "padding:.35rem .7rem;background:#12121a;cursor:pointer'>"
         "<span style='font-size:.75rem;font-weight:600;color:";
    h += on ? "#6366f1" : "#444";
    h += ";text-transform:uppercase;letter-spacing:.05em'>"; h += lg.label;
    if (!on) h += " <span style='font-size:.68rem;color:#444;text-transform:none'>(not enabled)</span>";
    h += "</span><span style='font-size:.68rem;color:#444'>▼</span></div>";
    h += "<div id='l_"; h += lg.id; h += "' style='display:none;padding:.4rem .5rem'>";
    if (lg.teams && lg.tc > 0) {
      h += "<div class='grid'>";
      for (int i=0;i<lg.tc;i++) {
        h += "<label class='ck' style='font-size:.78rem'><input type='checkbox' class='tf' data-abbr='";
        h += lg.id; h += ":"; h += lg.teams[i].abbr;
        h += "'> "; h += lg.teams[i].name; h += "</label>";
      }
      h += "</div>";
    }
    if (lg.confs && lg.cc > 0) {
      h += "<div class='grid'>";
      for (int i=0;i<lg.cc;i++) {
        h += "<label class='ck' style='font-size:.78rem'><input type='checkbox' class='cf' data-conf='";
        h += lg.confs[i].id; h += "'> "; h += lg.confs[i].name; h += "</label>";
      }
      h += "</div>";
    }
    h += "</div></div>";
  }

  h += "<script>"
    "function tog(id){var el=document.getElementById(id);if(!el)return;"
    "el.style.display=el.style.display==='none'?'block':'none';"
    "var a=document.getElementById(id+'_arr');if(a)a.textContent=el.style.display==='block'?'\u25b2 collapse':'\u25bc expand';}"
    "var sv=document.getElementById('tfv').value.split(',').map(function(s){return s.trim().toUpperCase();}).filter(Boolean);"
    "var sc=document.getElementById('cfv').value.split(',').map(function(s){return s.trim();}).filter(Boolean);"
    "document.querySelectorAll('.tf').forEach(function(cb){"
    "if(sv.indexOf(cb.dataset.abbr.toUpperCase())>=0){cb.checked=true;"
    "var p=cb.closest('[id^=l_]');if(p)p.style.display='block';}});"
    "document.querySelectorAll('.cf').forEach(function(cb){"
    "if(sc.indexOf(cb.dataset.conf)>=0){cb.checked=true;"
    "var p=cb.closest('[id^=l_]');if(p)p.style.display='block';}});"
    "if(sv.length||sc.length){var sf=document.getElementById('sf');if(sf)sf.style.display='block';"
    "var a=document.getElementById('sf_arr');if(a)a.textContent='\u25b2 collapse';}"
    "document.querySelector('form').addEventListener('submit',function(){"
    "var t=[];document.querySelectorAll('.tf:checked').forEach(function(cb){t.push(cb.dataset.abbr);});"
    "document.getElementById('tfv').value=t.join(',');"
    "var c=[];document.querySelectorAll('.cf:checked').forEach(function(cb){c.push(cb.dataset.conf);});"
    "document.getElementById('cfv').value=c.join(',');});"
    "</script></div></div>";
    h += "<div class='section'><strong>Stocks &amp; Crypto</strong>";
  h += "<label>Stock Tickers (comma-separated, e.g. AAPL,MSFT,SPY)</label>";
  h += "<input type='text' name='stocks' value='";
  h += Storage::cfg.stocks;
  h += "'>";
  h += "<small style='color:#888'>US stocks &amp; ETFs. Prices via Yahoo Finance - no API key needed.</small>";
  h += "<label style='margin-top:8px'>Crypto (CoinGecko IDs, e.g. bitcoin,ethereum,solana)</label>";
  h += "<input type='text' name='crypto' value='";
  h += Storage::cfg.crypto;
  h += "'>";
  h += "<small style='color:#888'>Free, no API key. Use CoinGecko IDs: bitcoin, ethereum, solana, dogecoin, cardano, ripple, etc.</small>";
  h += "</div>";

  // Theme + Display
  h += "<div class='section'><strong>Display</strong>";
  h += "<label>Theme</label><select name='theme'>";
  const char* themes[]={"Dark","Retro","Neon","Clean","Sports"};
  int tvals[]={0,1,2,3,4};
  for (int i=0;i<5;i++) {
    h += "<option value='"; h += tvals[i]; h += "'";
    if (Storage::cfg.theme==tvals[i]) h += " selected";
    h += ">"; h += themes[i]; h += "</option>";
  }
  h += "</select>";

  h += "</div>";

  // Calendar
  h += "<div class='section'><strong>Calendar</strong>";
  h += "<label>iCal URL <small style='color:#888'>(Google / Outlook / Apple)</small></label>";
  h += "<input type='text' name='icalUrl' placeholder='https://calendar.google.com/calendar/ical/...' value='";
  h += Storage::cfg.icalUrl;
  h += "'>";
  h += "<div style='margin-top:.6rem;font-size:.8rem;color:#888;line-height:1.7'>"
       "<div><b style='color:#aaa'>Google</b> - Settings &rarr; pick ONE specific calendar "
       "(not &lsquo;My calendars&rsquo;) &rarr; Secret address in iCal format</div>"
       "<div><b style='color:#aaa'>Outlook</b> - Calendar &rarr; Share &rarr; Get a link &rarr; ICS</div>"
       "<div><b style='color:#aaa'>Apple</b> - iCloud.com &rarr; Calendar &rarr; Share icon &rarr; Public Calendar URL</div>"
       "<div style='margin-top:.4rem;color:#555;font-size:.75rem'>"
       "Use one specific calendar, not a merged feed - keeps it small and fast.</div>"
       "</div></div>";

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

  // Screensaver photo set — lives on its own page because it uploads files
  h += "<div class='section'><strong>Screensaver</strong>"
       "<a href='/images' style='display:inline-block;margin-top:.6rem;padding:.55rem .9rem;"
       "border:1px solid #2a2a3a;border-radius:8px;color:#6366f1;text-decoration:none;font-size:.9rem'>"
       "Manage screensaver images &rarr;</a>"
       "<small style='display:block;color:#888;margin-top:.4rem'>"
       "Photos that scroll past after the idle timeout. Any photo works - the page shrinks it before upload."
       "</small></div>";

  h += "<button class='btn' type='submit'>Save Settings</button>";
  // Restart button — outside the form, uses a separate endpoint
  h += "<button class='btn' type='button' "
       "style='margin-top:.5rem;background:#374151' "
       "onclick=\"if(confirm('Restart TickerTouch?')){"
       "this.textContent='Restarting...';this.disabled=true;"
       "fetch('/restart',{method:'POST'});}\">"
       "Restart Device</button>";
  h += "</form>";
  h += "<div style='text-align:center;margin-top:1rem;font-size:.8rem'>"
       "<a href='/diag' style='color:#6366f1;text-decoration:none'>Device diagnostics &rarr;</a></div>";
  h += "</div></body></html>";
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

  // Extra weather cities
  uint8_t cityCount = 1;
  for (int i = 0; i < 2; i++) {
    auto &ec = Storage::cfg.extraCities[i];
    String cn = server.arg(String("city")  + String(i+2)); cn.trim();
    String sn = server.arg(String("state") + String(i+2)); sn.trim();
    bool changed = (cn.length() && cn != String(ec.city)) || (sn != String(ec.state));
    if (changed) { ec.lat = 0.0f; ec.lon = 0.0f; }
    if (cn.length()) { cn.toCharArray(ec.city,  sizeof(ec.city));
                       sn.toCharArray(ec.state, sizeof(ec.state)); cityCount++; }
    else { ec.city[0] = '\0'; ec.state[0] = '\0'; ec.lat = 0.0f; ec.lon = 0.0f; }
  }
  Storage::cfg.weatherCityCount = cityCount;

  // Sports — build bitmask from submitted checkboxes
  uint16_t sports = 0;
  if (server.arg("nfl").length())  sports |= (1<<0);
  if (server.arg("nba").length())  sports |= (1<<1);
  if (server.arg("nhl").length())  sports |= (1<<2);
  if (server.arg("mlb").length())  sports |= (1<<3);
  if (server.arg("cfb").length())  sports |= (1<<4);
  if (server.arg("mls").length())  sports |= (1<<5);
  if (server.arg("epl").length())  sports |= (1<<6);
  if (server.arg("cbb").length())  sports |= (1<<7);
  if (server.arg("wnba").length())    sports |= (1<<8);
  if (server.arg("nascar").length())  sports |= (1<<9);
  if (server.arg("f1").length())      sports |= (1<<10);
  if (server.arg("indycar").length()) sports |= (1<<11);
  if (server.arg("pga").length())     sports |= (1<<12);
  Storage::cfg.sportsLeagues = sports;
  Serial.printf("[Settings] saved leagues bitmask=%d\n", sports);

  // Stocks
  String stocks = server.arg("stocks");
  stocks.trim();
  if (stocks.length()) stocks.toCharArray(Storage::cfg.stocks, sizeof(Storage::cfg.stocks));

  // Crypto
  String crypto = server.arg("crypto");
  crypto.trim();
  if (crypto.length()) crypto.toCharArray(Storage::cfg.crypto, sizeof(Storage::cfg.crypto));
  else Storage::cfg.crypto[0] = '\0';

  // Finnhub API key
  String finnhub = server.arg("finnhub");
  finnhub.trim();
  if (finnhub.length())
    finnhub.toCharArray(Storage::cfg.finnhubKey, sizeof(Storage::cfg.finnhubKey));

  // iCal URL
  String icalUrl = server.arg("icalUrl");
  icalUrl.trim();
  icalUrl.toCharArray(Storage::cfg.icalUrl, sizeof(Storage::cfg.icalUrl));

  // Sports filter — capture old values to detect changes
  char oldTeamFilter[sizeof(Storage::cfg.teamFilter)];
  char oldCfbConf[sizeof(Storage::cfg.cfbConf)];
  strlcpy(oldTeamFilter, Storage::cfg.teamFilter, sizeof(oldTeamFilter));
  strlcpy(oldCfbConf, Storage::cfg.cfbConf, sizeof(oldCfbConf));

  String teamFilter = server.arg("teamFilter");
  teamFilter.trim();
  teamFilter.toCharArray(Storage::cfg.teamFilter, sizeof(Storage::cfg.teamFilter));

  String cfbConf = server.arg("cfbConf");
  cfbConf.trim();
  cfbConf.toCharArray(Storage::cfg.cfbConf, sizeof(Storage::cfg.cfbConf));

  bool filterChanged = strcmp(oldTeamFilter, Storage::cfg.teamFilter) != 0 ||
                       strcmp(oldCfbConf, Storage::cfg.cfbConf) != 0;

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
  Serial.printf("[Settings] saved: city=%s stocks=%s crypto=%s sports=%d filter=%s\n",
    Storage::cfg.city, Storage::cfg.stocks, Storage::cfg.crypto,
    Storage::cfg.sportsLeagues, Storage::cfg.teamFilter);

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

  // Team filter change — restart so widgets rebuild cleanly from scratch
  if (filterChanged) {
    server.send(200, "text/html",
      "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
      "<meta http-equiv='refresh' content='5;url=/'></head>"
      "<body style='background:#0f0f14;color:#e8e8f0;font-family:sans-serif;"
      "display:flex;align-items:center;justify-content:center;height:100vh;text-align:center'>"
      "<div><div style='font-size:2rem'>🔄</div>"
      "<h2 style='margin-top:.5rem'>Filter Updated</h2>"
      "<p style='color:#6366f1;margin-top:.3rem'>Restarting... returning to settings in 5s</p>"
      "</div></body></html>");
    delay(1500);
    ESP.restart();
    return;
  }

  // Normal save — redirect back
  server.send(200, "text/html",
    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<meta http-equiv='refresh' content='2;url=/'></head>"
    "<body style='background:#0f0f14;color:#e8e8f0;font-family:sans-serif;"
    "display:flex;align-items:center;justify-content:center;height:100vh;text-align:center'>"
    "<div><div style='font-size:2rem'>✅</div>"
    "<h2 style='margin-top:.5rem'>Saved</h2>"
    "<p style='color:#6366f1;margin-top:.3rem'>Returning to settings...</p>"
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

// ── Screensaver images ────────────────────────────────────────────────────────
// The page resizes every picked photo to Slideshow::IMG_W x IMG_H in the browser
// (canvas + JPEG re-encode) before posting it, so a 12MP camera image never has
// to fit in the device's RAM and the user needs no image tooling.
static String imagesPage() {
  String h;
  h.reserve(7000);
  h += "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>"
       "<meta name='viewport' content='width=device-width,initial-scale=1'>"
       "<title>TickerTouch - Screensaver Images</title><style>"
       "*{box-sizing:border-box;margin:0;padding:0}"
       "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#0f0f14;color:#e8e8f0;padding:1rem}"
       ".wrap{max-width:560px;margin:0 auto}"
       "h1{font-size:1.25rem;font-weight:600;margin:.6rem 0 .2rem}"
       ".sub{color:#888;font-size:.85rem;margin-bottom:1rem;line-height:1.5}"
       ".card{background:#1a1a24;border:1px solid #2a2a3a;border-radius:14px;padding:1.1rem;margin-bottom:1rem}"
       ".card strong{font-size:.78rem;color:#aaa;text-transform:uppercase;letter-spacing:.05em}"
       "input[type=file]{display:block;width:100%;margin-top:.7rem;padding:.6rem;background:#12121a;"
       "border:1px dashed #3a3a4a;border-radius:8px;color:#aaa;font-size:.85rem}"
       ".btn{display:inline-block;margin-top:.7rem;padding:.55rem 1rem;border:none;border-radius:8px;"
       "font-weight:600;font-size:.9rem;cursor:pointer;background:#6366f1;color:#fff}"
       ".btn.ghost{background:#374151;margin-top:1rem}"
       ".btn.sm{background:#7f1d1d;padding:.35rem .7rem;margin:0;font-size:.8rem}"
       ".row{display:flex;align-items:center;justify-content:space-between;padding:.55rem 0;"
       "border-bottom:1px solid #23232f;font-size:.9rem}"
       ".row:last-child{border-bottom:none}"
       ".mono{color:#666;font-size:.8rem;margin-left:.5rem;font-variant-numeric:tabular-nums}"
       ".bar{height:8px;background:#12121a;border-radius:4px;overflow:hidden;margin:.7rem 0 .4rem}"
       ".fill{height:100%;background:#6366f1;width:";
  {
    size_t total = Slideshow::totalBytes(), used = Slideshow::usedBytes();
    h += String(total ? (unsigned)((used * 100) / total) : 0);
    h += "%}"
         "#log{font-size:.82rem;color:#9aa;min-height:1.2rem;margin-top:.7rem;white-space:pre-wrap}"
         "a.back{color:#6366f1;font-size:.85rem;text-decoration:none}"
         ".thumb{display:inline-block;width:172px;margin:.6rem .5rem 0 0;vertical-align:top}"
         ".thumb canvas{width:172px;height:97px;border-radius:6px;display:block}"
         ".thumb small{display:block;color:#8b8b9a;font-size:.68rem;margin-top:.25rem;line-height:1.35}"
         "</style></head><body><div class='wrap'>"
         "<a class='back' href='/'>&larr; Settings</a>"
         "<h1>Screensaver Images</h1>"
         "<p class='sub'>Pick photos from your phone or computer. Each one is composed for the 480x272 panel and "
         "compressed in the browser, then uploaded - so large camera images are fine and nothing needs "
         "resizing by hand. You get a preview of exactly what the device will show before anything is sent.</p>";
    h += "<div class='card'><strong>Storage</strong>";
    h += "<div class='bar'><div class='fill'></div></div>";
    h += "<div class='mono' style='margin:0'>";
    h += String((unsigned)(used / 1024)); h += " KB of ";
    h += String((unsigned)(total / 1024)); h += " KB used &middot; ";
    h += String(Slideshow::count()); h += " image(s)</div></div>";
  }

  h += "<div class='card'><strong>Add photos</strong>"
       "<input type='file' id='f' accept='image/*' multiple>"
       "<button class='btn' id='up' type='button' disabled>Upload</button>"
       "<div id='log'></div>"
       "<div id='pvc' style='display:none'>"
       "<strong style='display:block;margin-top:1rem'>Preview - as it will appear</strong>"
       "<div id='pv'></div>"
       "<small style='display:block;color:#8b8b9a;margin-top:.5rem;font-size:.72rem;line-height:1.4'>"
       "The device draws the clock and ticker over the bottom strip shown here.</small></div>"
       "</div>";

  h += "<div class='card'><strong>Current set</strong>";
  const int n = Slideshow::count();
  if (n == 0) {
    h += "<div class='mono' style='margin:.6rem 0 0'>No images yet - the screensaver stays as the floating clock.</div>";
  } else {
    for (int i = 0; i < n; i++) {
      h += "<div class='row'><span>";
      h += String(i + 1);
      h += ". <span class='mono'>";
      h += String((unsigned)(Slideshow::sizeAt(i) / 1024));
      h += " KB</span></span><button class='btn sm' type='button' onclick='del(";
      h += String(i);
      h += ")'>Remove</button></div>";
    }
    h += "</div><button class='btn ghost' type='button' onclick='clearAll()'>Remove all images</button>";
  }
  h += "</div>";

  h += "<div class='card' style='font-size:.8rem;color:#777;line-height:1.6'>"
       "Images are kept in device flash, so they survive a restart and a firmware update "
       "(decline the erase prompt when flashing). About ";
  h += String((unsigned)(Slideshow::freeBytes() / 1024));
  h += " KB is free for roughly ";
  h += String((unsigned)(Slideshow::freeBytes() / 40000));
  h += " more photo(s) at the usual size.</div>";

  h += "<script>"
       "var W=480,H=272,CROPMAX=.32,queue=[];"
       "function log(m){document.getElementById('log').textContent=m;}"
       "var input=document.getElementById('f'),btn=document.getElementById('up');"
       "var pv=document.getElementById('pv'),pvc=document.getElementById('pvc');"
       "function thumb(c,name,note){"
       "var d=document.createElement('div');d.className='thumb';"
       "var p=document.createElement('canvas');p.width=344;p.height=194;var px=p.getContext('2d');"
       "px.imageSmoothingQuality='high';px.drawImage(c,0,0,p.width,p.height);"
       "var bh=Math.round(p.height*48/272);"
       "px.fillStyle='rgba(0,0,0,.75)';px.fillRect(0,p.height-bh,p.width,bh);"
       "px.fillStyle='#fff';px.font='bold 13px sans-serif';px.fillText('12:34',6,p.height-bh+bh/2+5);"
       "px.fillStyle='#b9b9ff';px.font='11px sans-serif';px.fillText('TICKER TEXT SCROLLS HERE',52,p.height-bh+bh/2+4);"
       "d.appendChild(p);"
       "var s=document.createElement('small');s.textContent=name+' - '+note;d.appendChild(s);"
       "pv.appendChild(d);pvc.style.display='block';"
       "}"
       "input.onchange=function(){"
       "var files=Array.prototype.slice.call(input.files);"
       "queue=[];pv.innerHTML='';pvc.style.display='none';btn.disabled=true;"
       "if(!files.length){log('');return;}"
       "var i=0;(function prep(){"
       "if(i>=files.length){log(queue.length+' photo(s) ready - check the preview, then press Upload.');btn.disabled=!queue.length;return;}"
       "var f=files[i++];log('Preparing '+i+'/'+files.length+': '+f.name);"
       "shrink(f).then(function(res){"
       "queue.push({blob:res.blob,name:f.name.replace(/[.][^.]+$/,'')+'.jpg'});"
       "thumb(res.c,f.name,res.note);prep();"
       "}).catch(function(e){log('Cannot read '+f.name+': '+e.message);prep();});"
       "})();"
       "};"
       "function shrink(file){return createImageBitmap(file).then(function(bmp){"
       "var c=document.createElement('canvas');c.width=W;c.height=H;var x=c.getContext('2d');"
       "x.imageSmoothingQuality='high';var note='cropped to fill';"
       "var g=Math.max(W/bmp.width,H/bmp.height),gw=bmp.width*g,gh=bmp.height*g;"
       "if(1-(W*H)/(gw*gh)<=CROPMAX){"
       "x.drawImage(bmp,(W-gw)/2,(H-gh)/2,gw,gh);"
       "}else{"
       "note='whole photo, blurred sides';"
       "var f=Math.min(W/bmp.width,H/bmp.height),fw=bmp.width*f,fh=bmp.height*f;"
       "var t=document.createElement('canvas');t.width=48;t.height=27;"
       "var tx=t.getContext('2d');var s=Math.max(t.width/bmp.width,t.height/bmp.height);"
       "tx.drawImage(bmp,(t.width-bmp.width*s)/2,(t.height-bmp.height*s)/2,bmp.width*s,bmp.height*s);"
       "x.drawImage(t,0,0,W,H);"
       "x.fillStyle='rgba(0,0,0,.45)';x.fillRect(0,0,W,H);"
       "x.drawImage(bmp,(W-fw)/2,(H-fh)/2,fw,fh);"
       "}"
       "return new Promise(function(r){c.toBlob(function(b){r({blob:b,c:c,note:note});},'image/jpeg',0.92);});"
       "});}"
       "btn.onclick=function(){"
       "btn.disabled=true;"
       "var i=0;(function next(){"
       "if(i>=queue.length){log('Done - reloading...');setTimeout(function(){location.reload();},600);return;}"
       "var q=queue[i++];log('Uploading '+i+'/'+queue.length+' ('+Math.round(q.blob.size/1024)+' KB)...');"
       "var fd=new FormData();fd.append('f',q.blob,q.name);"
       "fetch('/upload',{method:'POST',body:fd}).then(function(r){"
       "if(!r.ok)throw new Error('device returned '+r.status);next();"
       "}).catch(function(e){log('Failed on '+q.name+': '+e.message);btn.disabled=false;});"
       "})();"
       "};"
       "function del(i){if(!confirm('Remove this image?'))return;"
       "fetch('/img/delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
       "body:'idx='+i}).then(function(){location.reload();});}"
       "function clearAll(){if(!confirm('Remove all screensaver images?'))return;"
       "fetch('/img/clear',{method:'POST'}).then(function(){location.reload();});}"
       "</script></div></body></html>";
  return h;
}

static void handleImagesGet() { server.send(200, "text/html", imagesPage()); }

static void handleUploadChunk() {
  HTTPUpload &u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    Serial.printf("[Images] upload start: %s\n", u.filename.c_str());
    if (!Slideshow::uploadBegin()) Serial.println(F("[Images] cannot accept upload"));
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (Slideshow::uploading()) Slideshow::uploadWrite(u.buf, u.currentSize);
  } else if (u.status == UPLOAD_FILE_END) {
    Serial.printf("[Images] upload finished: %u bytes\n", (unsigned)u.totalSize);
    Slideshow::uploadEnd();
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    Serial.println(F("[Images] upload aborted"));
    Slideshow::uploadAbort();
  }
}

static void handleUploadDone() {
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleImageDelete() {
  const int idx = server.arg("idx").toInt();
  const bool ok = Slideshow::removeAt(idx);
  server.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void handleImagesClear() {
  Slideshow::removeAll();
  server.send(200, "application/json", "{\"ok\":true}");
}

// ── Diagnostics ──────────────────────────────────────────────────────────────
// The board normally runs with nothing attached to USB, so everything the
// firmware would otherwise only print to Serial is served here too: why it last
// reset, how much stack each task has left, and how stale each feed's data is.
// Deliberately no secrets - the iCal URL and Finnhub key never reach this page.
static const char *resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_EXT:       return "external pin";
    case ESP_RST_SW:        return "software restart";
    case ESP_RST_PANIC:     return "panic / exception";
    case ESP_RST_INT_WDT:   return "interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "task watchdog";
    case ESP_RST_WDT:       return "other watchdog";
    case ESP_RST_DEEPSLEEP: return "deep-sleep wake";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "unknown";
  }
}

static void ageStr(char *out, size_t n, uint32_t sinceMs, uint32_t nowMs) {
  if (!sinceMs) { snprintf(out, n, "never"); return; }
  const uint32_t s = (nowMs - sinceMs) / 1000;
  if (s < 60)        snprintf(out, n, "%us ago", (unsigned)s);
  else if (s < 3600) snprintf(out, n, "%um ago", (unsigned)(s / 60));
  else               snprintf(out, n, "%uh %um ago", (unsigned)(s / 3600), (unsigned)((s % 3600) / 60));
}

static bool feedReady(uint8_t f) {
  switch (f) {
    case DataManager::FEED_WEATHER: return DataManager::weatherReady;
    case DataManager::FEED_SCORES:  return DataManager::scoresReady;
    case DataManager::FEED_STOCKS:  return DataManager::stocksReady;
    case DataManager::FEED_CRYPTO:  return DataManager::cryptoReady;
    case DataManager::FEED_RACING:  return DataManager::racesReady;
    case DataManager::FEED_CAL:     return DataManager::calReady;
    default:                        return false;
  }
}

static int feedItems(uint8_t f) {
  switch (f) {
    case DataManager::FEED_WEATHER: return DataManager::weatherCityCount;
    case DataManager::FEED_SCORES:  return DataManager::scoreCount;
    case DataManager::FEED_STOCKS:  return DataManager::stockCount;
    case DataManager::FEED_CRYPTO:  return DataManager::cryptoCount;
    case DataManager::FEED_RACING:  return DataManager::raceCount;
    case DataManager::FEED_CAL:     return DataManager::calCount;
    default:                        return 0;
  }
}

static void diagRow(String &h, const char *k, const String &v, const char *cls = "") {
  h += "<div class='r'><span>";
  h += k;
  h += "</span><b";
  if (cls && *cls) { h += " class='"; h += cls; h += "'"; }
  h += ">";
  h += v;
  h += "</b></div>";
}

static void handleDiagGet() {
  const uint32_t now = millis();
  char buf[48];
  String h;
  h.reserve(7000);
  h += "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>"
       "<meta name='viewport' content='width=device-width,initial-scale=1'>"
       "<meta http-equiv='refresh' content='10'>"
       "<title>TickerTouch - Diagnostics</title><style>"
       "*{box-sizing:border-box;margin:0;padding:0}"
       "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#0f0f14;color:#e8e8f0;padding:1rem}"
       ".wrap{max-width:600px;margin:0 auto}h1{font-size:1.15rem;margin:.4rem 0 .1rem}"
       ".sub{color:#888;font-size:.78rem;margin-bottom:1rem}"
       ".card{background:#1a1a24;border:1px solid #2a2a3a;border-radius:12px;padding:.9rem 1rem;margin-bottom:.8rem}"
       ".card>strong{font-size:.72rem;color:#aaa;text-transform:uppercase;letter-spacing:.05em;display:block;margin-bottom:.5rem}"
       ".r{display:flex;justify-content:space-between;gap:.8rem;padding:.3rem 0;border-bottom:1px solid #23232f;font-size:.8rem}"
       ".r:last-child{border-bottom:none}.r span{color:#9a9aa8}"
       ".r b{font-weight:600;font-variant-numeric:tabular-nums;text-align:right}"
       ".ok{color:#4ade80}.warn{color:#fbbf24}.bad{color:#f87171}"
       "a.back{color:#6366f1;font-size:.8rem;text-decoration:none}"
       "</style></head><body><div class='wrap'>"
       "<a class='back' href='/'>&larr; Settings</a>"
       "<h1>Diagnostics</h1><p class='sub'>";
  h += FW_VERSION;
  h += " &middot; refreshes every 10s</p>";

  h += "<div class='card'><strong>Build</strong>";
  diagRow(h, "Version", FW_VERSION);
  diagRow(h, "Compiled", String(__DATE__) + " " + __TIME__);
  diagRow(h, "Chip", String(ESP.getChipModel()) + " rev " + String(ESP.getChipRevision()));
  diagRow(h, "CPU", String(getCpuFrequencyMhz()) + " MHz x " + String(ESP.getChipCores()));
  {
    const uint32_t up = now / 1000;
    snprintf(buf, sizeof(buf), "%ud %uh %um",
             (unsigned)(up / 86400), (unsigned)((up % 86400) / 3600), (unsigned)((up % 3600) / 60));
    diagRow(h, "Uptime", buf);
  }
  diagRow(h, "Last reset", resetReasonName(esp_reset_reason()));
  h += "</div>";

  h += "<div class='card'><strong>Memory</strong>";
  diagRow(h, "Heap free", String((unsigned)(esp_get_free_heap_size() / 1024)) + " KB");
  {
    const uint32_t low = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    diagRow(h, "Heap low-water", String((unsigned)(low / 1024)) + " KB",
            low < 40 * 1024 ? "bad" : (low < 80 * 1024 ? "warn" : ""));
  }
  diagRow(h, "PSRAM free", String((unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024)) +
            " KB of " + String((unsigned)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024)) + " KB");
  h += "</div>";

  h += "<div class='card'><strong>Task stack headroom</strong>";
  struct { const char *name; TaskHandle_t h; } tasks[] = {
    { "Main (network)", mainTaskHandle },
    { "LVGL (UI)",      lvglTaskHandle },
    { "Web (HTTP)",     webTaskHandle  },
  };
  for (auto &t : tasks) {
    if (!t.h) { diagRow(h, t.name, "not running", "bad"); continue; }
    const uint32_t freeB = (uint32_t)uxTaskGetStackHighWaterMark(t.h) * sizeof(StackType_t);
    diagRow(h, t.name, String((unsigned)freeB) + " B free",
            freeB < 2048 ? "bad" : (freeB < 6144 ? "warn" : "ok"));
  }
  h += "</div>";

  h += "<div class='card'><strong>Network</strong>";
  const bool netUp = isConnected();
  diagRow(h, "WiFi", netUp ? "connected" : "down", netUp ? "ok" : "bad");
  if (netUp) {
    diagRow(h, "SSID", WiFi.SSID());
    diagRow(h, "IP", getIP());
    const int rssi = WiFi.RSSI();
    diagRow(h, "Signal", String(rssi) + " dBm", rssi > -67 ? "ok" : (rssi > -78 ? "warn" : "bad"));
  }
  h += "</div>";

  h += "<div class='card'><strong>Data feeds</strong>";
  for (uint8_t f = 0; f < DataManager::FEED_COUNT; f++) {
    const DataManager::FeedHealth &fh = DataManager::feedHealth[f];
    const bool ready = feedReady(f);
    const int items = feedItems(f);
    char age[24];
    ageStr(age, sizeof(age), fh.okMs, now);
    String v = String(items) + (items == 1 ? " item" : " items") + " &middot; " + age;
    if (!ready)              v += " &middot; waiting";
    else if (fh.fails)       v += " &middot; " + String(fh.fails) + " failed";
    diagRow(h, DataManager::feedName(f), v,
            !ready ? "" : (fh.fails ? "warn" : "ok"));
  }
  h += "</div>";

  h += "<div class='card'><strong>Screensaver</strong>";
  diagRow(h, "Photos", String((int)Slideshow::count()) + " of " + String((int)Slideshow::MAX_IMAGES));
  diagRow(h, "Photo storage", String((unsigned)(Slideshow::usedBytes() / 1024)) + " KB of " +
            String((unsigned)(Slideshow::totalBytes() / 1024)) + " KB");
  diagRow(h, "Free", String((unsigned)(Slideshow::freeBytes() / 1024)) + " KB");
  diagRow(h, "Sleep after", Storage::cfg.saverSec
            ? (String((unsigned)(Storage::cfg.saverSec / 60)) + " min") : String("never"));
  {
    const bool saver = Screensaver::isActive();
    diagRow(h, "Showing", saver ? "screensaver" : "dashboard", saver ? "ok" : "");
  }
  h += "</div>";

  h += "</div></body></html>";
  server.send(200, "text/html", h);
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
  server.on("/",        HTTP_GET,  handleSettingsGet);
  server.on("/save",    HTTP_POST, handleSettingsSave);
  server.on("/scan",    HTTP_GET,  handleScan);
  server.on("/images",  HTTP_GET,  handleImagesGet);
  server.on("/upload",  HTTP_POST, handleUploadDone, handleUploadChunk);
  server.on("/img/delete", HTTP_POST, handleImageDelete);
  server.on("/img/clear",  HTTP_POST, handleImagesClear);
  server.on("/diag",       HTTP_GET,  handleDiagGet);
  server.on("/restart", HTTP_POST, [](){
    server.send(200, "text/plain", "Restarting...");
    delay(500);
    ESP.restart();
  });
  server.begin();
  Serial.printf("[WiFi] Settings at http://%s/\n", WiFi.localIP().toString().c_str());
}

void handleSettingsServer() { server.handleClient(); }

bool isConnected()  { return WiFi.status() == WL_CONNECTED; }
const char* getIP() {
  // Static char buffer, not a shared static String: the web task and the LVGL
  // task both call this, and reassigning a String frees the buffer the other
  // task may still be printing from.
  static char ip[20];
  IPAddress a = WiFi.localIP();
  snprintf(ip, sizeof(ip), "%u.%u.%u.%u",
           (unsigned)a[0], (unsigned)a[1], (unsigned)a[2], (unsigned)a[3]);
  return ip;
}

} // namespace WiFiManager
