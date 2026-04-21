#pragma once
// Static team and conference lists for sports filter UI
// Each entry: { abbreviation, display name }

struct TeamDef { const char *abbr; const char *name; };
struct ConfDef  { const char *id;   const char *name; };

// ── NFL ──────────────────────────────────────────────────────────────────────
static const TeamDef NFL_TEAMS[] = {
  {"ARI","Arizona Cardinals"},   {"ATL","Atlanta Falcons"},
  {"BAL","Baltimore Ravens"},    {"BUF","Buffalo Bills"},
  {"CAR","Carolina Panthers"},   {"CHI","Chicago Bears"},
  {"CIN","Cincinnati Bengals"},  {"CLE","Cleveland Browns"},
  {"DAL","Dallas Cowboys"},      {"DEN","Denver Broncos"},
  {"DET","Detroit Lions"},       {"GB","Green Bay Packers"},
  {"HOU","Houston Texans"},      {"IND","Indianapolis Colts"},
  {"JAX","Jacksonville Jaguars"},{"KC","Kansas City Chiefs"},
  {"LAC","LA Chargers"},         {"LAR","LA Rams"},
  {"LV","Las Vegas Raiders"},    {"MIA","Miami Dolphins"},
  {"MIN","Minnesota Vikings"},   {"NE","New England Patriots"},
  {"NO","New Orleans Saints"},   {"NYG","NY Giants"},
  {"NYJ","NY Jets"},             {"PHI","Philadelphia Eagles"},
  {"PIT","Pittsburgh Steelers"}, {"SEA","Seattle Seahawks"},
  {"SF","San Francisco 49ers"},  {"TB","Tampa Bay Buccaneers"},
  {"TEN","Tennessee Titans"},    {"WSH","Washington Commanders"},
};

// ── NBA ──────────────────────────────────────────────────────────────────────
static const TeamDef NBA_TEAMS[] = {
  {"ATL","Atlanta Hawks"},       {"BKN","Brooklyn Nets"},
  {"BOS","Boston Celtics"},      {"CHA","Charlotte Hornets"},
  {"CHI","Chicago Bulls"},       {"CLE","Cleveland Cavaliers"},
  {"DAL","Dallas Mavericks"},    {"DEN","Denver Nuggets"},
  {"DET","Detroit Pistons"},     {"GSW","Golden State Warriors"},
  {"HOU","Houston Rockets"},     {"IND","Indiana Pacers"},
  {"LAC","LA Clippers"},         {"LAL","LA Lakers"},
  {"MEM","Memphis Grizzlies"},   {"MIA","Miami Heat"},
  {"MIL","Milwaukee Bucks"},     {"MIN","Minnesota Timberwolves"},
  {"NOP","New Orleans Pelicans"},{"NYK","New York Knicks"},
  {"OKC","Oklahoma City Thunder"},{"ORL","Orlando Magic"},
  {"PHI","Philadelphia 76ers"},  {"PHX","Phoenix Suns"},
  {"POR","Portland Trail Blazers"},{"SAC","Sacramento Kings"},
  {"SAS","San Antonio Spurs"},   {"TOR","Toronto Raptors"},
  {"UTA","Utah Jazz"},           {"WAS","Washington Wizards"},
};

// ── NHL ──────────────────────────────────────────────────────────────────────
static const TeamDef NHL_TEAMS[] = {
  {"ANA","Anaheim Ducks"},       {"ARI","Arizona Coyotes"},
  {"BOS","Boston Bruins"},       {"BUF","Buffalo Sabres"},
  {"CGY","Calgary Flames"},      {"CAR","Carolina Hurricanes"},
  {"CHI","Chicago Blackhawks"},  {"COL","Colorado Avalanche"},
  {"CBJ","Columbus Blue Jackets"},{"DAL","Dallas Stars"},
  {"DET","Detroit Red Wings"},   {"EDM","Edmonton Oilers"},
  {"FLA","Florida Panthers"},    {"LAK","LA Kings"},
  {"MIN","Minnesota Wild"},      {"MTL","Montreal Canadiens"},
  {"NSH","Nashville Predators"}, {"NJD","New Jersey Devils"},
  {"NYI","NY Islanders"},        {"NYR","NY Rangers"},
  {"OTT","Ottawa Senators"},     {"PHI","Philadelphia Flyers"},
  {"PIT","Pittsburgh Penguins"}, {"STL","St. Louis Blues"},
  {"SJS","San Jose Sharks"},     {"SEA","Seattle Kraken"},
  {"TBL","Tampa Bay Lightning"}, {"TOR","Toronto Maple Leafs"},
  {"UTA","Utah Hockey Club"},    {"VAN","Vancouver Canucks"},
  {"VGK","Vegas Golden Knights"},{"WSH","Washington Capitals"},
  {"WPG","Winnipeg Jets"},
};

// ── MLB ──────────────────────────────────────────────────────────────────────
static const TeamDef MLB_TEAMS[] = {
  {"ARI","Arizona Diamondbacks"}, {"ATL","Atlanta Braves"},
  {"BAL","Baltimore Orioles"},    {"BOS","Boston Red Sox"},
  {"CHC","Chicago Cubs"},         {"CWS","Chicago White Sox"},
  {"CIN","Cincinnati Reds"},      {"CLE","Cleveland Guardians"},
  {"COL","Colorado Rockies"},     {"DET","Detroit Tigers"},
  {"HOU","Houston Astros"},       {"KC","Kansas City Royals"},
  {"LAA","LA Angels"},            {"LAD","LA Dodgers"},
  {"MIA","Miami Marlins"},        {"MIL","Milwaukee Brewers"},
  {"MIN","Minnesota Twins"},      {"NYM","NY Mets"},
  {"NYY","NY Yankees"},           {"OAK","Oakland Athletics"},
  {"PHI","Philadelphia Phillies"},{"PIT","Pittsburgh Pirates"},
  {"SD","San Diego Padres"},      {"SF","San Francisco Giants"},
  {"SEA","Seattle Mariners"},     {"STL","St. Louis Cardinals"},
  {"TB","Tampa Bay Rays"},        {"TEX","Texas Rangers"},
  {"TOR","Toronto Blue Jays"},    {"WSH","Washington Nationals"},
};

// ── MLS ──────────────────────────────────────────────────────────────────────
static const TeamDef MLS_TEAMS[] = {
  {"ATL","Atlanta United"},       {"AUS","Austin FC"},
  {"CHI","Chicago Fire"},         {"CIN","FC Cincinnati"},
  {"COL","Colorado Rapids"},      {"CLB","Columbus Crew"},
  {"DAL","FC Dallas"},            {"DC","D.C. United"},
  {"HOU","Houston Dynamo"},       {"LAG","LA Galaxy"},
  {"LAFC","LAFC"},                {"MIA","Inter Miami"},
  {"MNU","Minnesota United"},     {"MTL","CF Montréal"},
  {"NE","New England Revolution"},{"NSH","Nashville SC"},
  {"NJY","NY Red Bulls"},         {"NYC","NYCFC"},
  {"ORL","Orlando City"},         {"PHI","Philadelphia Union"},
  {"POR","Portland Timbers"},     {"RSL","Real Salt Lake"},
  {"SEA","Seattle Sounders"},     {"SJ","San Jose Earthquakes"},
  {"SKC","Sporting KC"},          {"STL","St. Louis City"},
  {"TOR","Toronto FC"},           {"VAN","Vancouver Whitecaps"},
};

// ── EPL ──────────────────────────────────────────────────────────────────────
static const TeamDef EPL_TEAMS[] = {
  {"ARS","Arsenal"},              {"AVL","Aston Villa"},
  {"BOU","Bournemouth"},          {"BRE","Brentford"},
  {"BHA","Brighton"},             {"CHE","Chelsea"},
  {"CRY","Crystal Palace"},       {"EVE","Everton"},
  {"FUL","Fulham"},               {"IPS","Ipswich Town"},
  {"LEI","Leicester City"},       {"LIV","Liverpool"},
  {"MCI","Manchester City"},      {"MUN","Manchester United"},
  {"NEW","Newcastle United"},     {"NFO","Nottm Forest"},
  {"SOU","Southampton"},          {"TOT","Tottenham"},
  {"WHU","West Ham"},             {"WOL","Wolves"},
};

// ── CFB Conferences ──────────────────────────────────────────────────────────
static const ConfDef CFB_CONFS[] = {
  {"Top 25",    "⭐ Top 25 Ranked"},
  {"ACC",       "ACC"},
  {"Big 12",    "Big 12"},
  {"Big Ten",   "Big Ten"},
  {"SEC",       "SEC"},
  {"Pac-12",    "Pac-12"},
  {"AAC",       "AAC (American)"},
  {"C-USA",     "Conference USA"},
  {"MAC",       "MAC"},
  {"MWC",       "Mountain West"},
  {"Sun Belt",  "Sun Belt"},
  {"Ind",       "Independents (Notre Dame, etc.)"},
};

// ── CBB Conferences ──────────────────────────────────────────────────────────
static const ConfDef CBB_CONFS[] = {
  {"Top 25",    "⭐ Top 25 Ranked"},
  {"ACC",       "ACC"},
  {"Big 12",    "Big 12"},
  {"Big Ten",   "Big Ten"},
  {"SEC",       "SEC"},
  {"Pac-12",    "Pac-12"},
  {"Big East",  "Big East"},
  {"AAC",       "AAC (American)"},
  {"A-10",      "Atlantic 10"},
  {"MWC",       "Mountain West"},
  {"WCC",       "West Coast Conference"},
};

// ── WNBA (2025 season) ───────────────────────────────────────────────────────
static const TeamDef WNBA_TEAMS[] = {
  {"ATL","Atlanta Dream"},         {"CHI","Chicago Sky"},
  {"CON","Connecticut Sun"},       {"DAL","Dallas Wings"},
  {"GSV","Golden State Valkyries"},{"IND","Indiana Fever"},
  {"LAS","Las Vegas Aces"},        {"LA","LA Sparks"},
  {"MIN","Minnesota Lynx"},        {"NY","New York Liberty"},
  {"PHX","Phoenix Mercury"},       {"POR","Portland Fire"},
  {"SEA","Seattle Storm"},         {"TOR","Toronto Tempo"},
  {"WSH","Washington Mystics"},
};
