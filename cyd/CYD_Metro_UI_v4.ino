#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <XPT2046_Bitbang.h>

// ===== TOUCH PINS =====
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

XPT2046_Bitbang ts(XPT2046_MOSI, XPT2046_MISO, XPT2046_CLK, XPT2046_CS);

// ===== MULTIPLE WIFI SETTINGS =====
const char* ssids[] = {
  "Najeeb_Home",
  "S",
  "Najeeb_iPhone"
};
const char* passwords[] = {
  "meowmeow@1234",
  "najeeb1999",
  "najeeb1999"
};
const int numNetworks = 3;

// ===== ESP32S3 TARGET IP =====
Preferences prefs;
String esp32s3IPStr = "192.168.0.188";
String connectedSSID = "";

TFT_eSPI tft = TFT_eSPI();
WebServer configServer(80);

// ===== EDEN THEME COLORS =====
uint16_t COL_BG, COL_TILE, COL_TILE_ON, COL_ACCENT, COL_TEXT, COL_SUBTEXT, COL_DANGER;

// ===== LIVE STATE (polled from ESP32S3) =====
// soil_a/b now in %, light in raw 0-3000
float soil_a = 0, soil_b = 0;
int   light = 0;
float temp = 0, humidity = 0, water = 0;
bool pumpA_on = false, pumpA_auto = true;
bool pumpB_on = false, pumpB_auto = true;
bool fan_on = false, fan_auto = true;
int led_brightness = 0;
bool led_auto = true;

bool esp32Reachable = false;
unsigned long lastSuccessfulPoll = 0;
unsigned long lastPoll = 0;
const unsigned long POLL_INTERVAL = 2000;
const unsigned long OFFLINE_TIMEOUT = 8000;

struct Tile { int x, y, w, h; };

Tile sensorTiles[6] = {
  {2,   26, 104, 60}, {108, 26, 104, 60}, {214, 26, 104, 60},
  {2,   88, 104, 60}, {108, 88, 104, 60}, {214, 88, 104, 60}
};

Tile controlTiles[4] = {
  {2,   152, 78, 86}, {82, 152, 78, 86}, {162, 152, 78, 86}, {242, 152, 78, 86}
};

const int RAW_X_MIN = 25;
const int RAW_X_MAX = 285;
const int RAW_Y_MIN = 15;
const int RAW_Y_MAX = 210;

int mapTouchX(int rawX) { return constrain(map(rawX, RAW_X_MIN, RAW_X_MAX, 0, 320), 0, 320); }
int mapTouchY(int rawY) { return constrain(map(rawY, RAW_Y_MIN, RAW_Y_MAX, 0, 240), 0, 240); }

int activeTileIndex = -1;
unsigned long pressStartTime = 0;
bool longPressFired = false;
const unsigned long LONG_PRESS_MS = 600;

void setup() {
  Serial.begin(115200);
  delay(1000);

  prefs.begin("eden", false);
  esp32s3IPStr = prefs.getString("esp32ip", esp32s3IPStr);

  ts.begin();
  tft.init();
  tft.setRotation(1);

  COL_BG      = tft.color565(0x00, 0x18, 0x0b);
  COL_TILE    = tft.color565(0x16, 0x2f, 0x20);
  COL_TILE_ON = tft.color565(0x2d, 0x5a, 0x3f);
  COL_ACCENT  = tft.color565(0xa1, 0xd2, 0xaf);
  COL_TEXT    = tft.color565(0xcc, 0xea, 0xd3);
  COL_SUBTEXT = tft.color565(0xc1, 0xc9, 0xc0);
  COL_DANGER  = tft.color565(0xff, 0x6b, 0x6b);

  tft.fillScreen(COL_BG);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("EDEN");
  tft.setTextSize(1);
  tft.setCursor(10, 40);
  tft.println("Connecting to WiFi...");

  connectToWiFi();

  configServer.on("/", handleDashboard);
  configServer.on("/settings", handleConfigRoot);
  configServer.on("/setip", handleSetIP);
  configServer.on("/api/data", handleAPIData);
  configServer.on("/control", handleControlProxy);
  configServer.begin();

  drawUI();
}

void loop() {
  configServer.handleClient();
  handleTouch();

  if (millis() - lastPoll > POLL_INTERVAL) {
    pollESP32S3();
    drawUI();
    lastPoll = millis();
  }

  delay(30);
}

void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  bool connected = false;

  for (int i = 0; i < numNetworks; i++) {
    Serial.print("Trying: ");
    Serial.println(ssids[i]);
    WiFi.disconnect(true, true);
    delay(300);
    WiFi.begin(ssids[i], passwords[i]);

    for (int j = 0; j < 20; j++) {
      delay(500);
      Serial.print(".");
      if (WiFi.status() == WL_CONNECTED) {
        connected = true;
        connectedSSID = ssids[i];
        break;
      }
    }
    if (connected) break;
  }
  Serial.println();
  if (connected) {
    Serial.print("✓ Connected to: "); Serial.println(connectedSSID);
    Serial.print("CYD IP: "); Serial.println(WiFi.localIP());
    Serial.print("Target ESP32S3: "); Serial.println(esp32s3IPStr);
  } else {
    Serial.println("✗ Failed to connect to any WiFi network!");
  }
}

void handleConfigRoot() {
  bool online = esp32Reachable && (millis() - lastSuccessfulPoll < OFFLINE_TIMEOUT);

  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:sans-serif;background:#00180b;color:#ccead3;padding:20px;}";
  html += "input{padding:8px;font-size:16px;width:220px;} button{padding:8px 16px;font-size:16px;";
  html += "background:#a1d2af;color:#073820;border:none;border-radius:6px;margin-left:8px;}";
  html += ".badge{display:inline-block;padding:4px 10px;border-radius:999px;font-size:13px;font-weight:bold;}";
  html += ".online{background:#2d5a3f;color:#a1d2af;} .offline{background:#5a2d2d;color:#ff6b6b;}";
  html += "a{color:#a1d2af;}</style></head><body>";
  html += "<p><a href='/'>&larr; Back to dashboard</a></p>";
  html += "<h2>EDEN Display Settings</h2>";
  html += "<p>This screen's IP: <b>" + WiFi.localIP().toString() + "</b></p>";
  html += "<p>Connected to WiFi: <b>" + connectedSSID + "</b></p>";
  html += "<p>Currently polling ESP32S3 at:</p>";
  html += "<form action='/setip' method='GET'>";
  html += "<input name='ip' value='" + esp32s3IPStr + "'> <button type='submit'>Save</button>";
  html += "</form>";
  html += "<p style='margin-top:16px;'>Status: <span class='badge " + String(online ? "online" : "offline") +
          "'>" + String(online ? "ONLINE" : "OFFLINE") + "</span></p>";
  html += "</body></html>";
  configServer.send(200, "text/html", html);
}

void handleSetIP() {
  if (configServer.hasArg("ip")) {
    String newIP = configServer.arg("ip");
    newIP.trim();
    if (newIP.length() > 0) {
      esp32s3IPStr = newIP;
      prefs.putString("esp32ip", esp32s3IPStr);
    }
  }
  configServer.sendHeader("Location", "/settings");
  configServer.send(303);
}

void handleAPIData() {
  String json = "{";
  json += "\"soil_a\":" + String(soil_a, 1) + ",";
  json += "\"soil_b\":" + String(soil_b, 1) + ",";
  json += "\"light\":" + String(light) + ",";
  json += "\"temp\":" + String(temp, 1) + ",";
  json += "\"humidity\":" + String(humidity, 1) + ",";
  json += "\"water\":" + String(water, 1) + ",";
  json += "\"pumpA_on\":" + String(pumpA_on ? "true" : "false") + ",";
  json += "\"pumpA_auto\":" + String(pumpA_auto ? "true" : "false") + ",";
  json += "\"pumpB_on\":" + String(pumpB_on ? "true" : "false") + ",";
  json += "\"pumpB_auto\":" + String(pumpB_auto ? "true" : "false") + ",";
  json += "\"fan_on\":" + String(fan_on ? "true" : "false") + ",";
  json += "\"fan_auto\":" + String(fan_auto ? "true" : "false") + ",";
  json += "\"led_brightness\":" + String(led_brightness) + ",";
  json += "\"led_auto\":" + String(led_auto ? "true" : "false") + ",";
  json += "\"online\":" + String((esp32Reachable && (millis() - lastSuccessfulPoll < OFFLINE_TIMEOUT)) ? "true" : "false");
  json += "}";
  configServer.send(200, "application/json", json);
}

void handleControlProxy() {
  String query = "";
  for (int i = 0; i < configServer.args(); i++) {
    if (i > 0) query += "&";
    query += configServer.argName(i) + "=" + configServer.arg(i);
  }
  sendControl(query);
  configServer.send(200, "text/plain", "OK");
}

void handleDashboard() {
  String html = R"HTMLPAGE(
<!DOCTYPE html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>EDEN Smart Terrarium</title>
<link href="https://fonts.googleapis.com/css2?family=Manrope:wght@400;600;700;800&family=JetBrains+Mono:wght@700&display=swap" rel="stylesheet">
<style>
:root{
  --surface:#00180b; --surface-container:#0b2416; --surface-container-high:#162f20;
  --surface-variant:#213a2b; --on-background:#ccead3; --on-surface-variant:#c1c9c0;
  --primary:#a1d2af; --on-primary:#073820; --primary-container:#2d5a3f;
}
*{box-sizing:border-box;}
body{margin:0;background:var(--surface);color:var(--on-background);font-family:Manrope,sans-serif;padding-bottom:40px;}
header{display:flex;justify-content:space-between;align-items:center;padding:16px 20px;border-bottom:1px solid var(--surface-variant);}
.logo{font-weight:800;font-size:22px;color:var(--primary);}
.status{display:flex;align-items:center;gap:6px;background:rgba(45,90,63,0.3);padding:4px 10px;border-radius:999px;font-family:'JetBrains Mono';font-size:11px;text-transform:uppercase;color:var(--primary);}
.dot{width:8px;height:8px;border-radius:50%;background:#4ade80;}
.dot.off{background:#ff6b6b;}
main{max-width:900px;margin:0 auto;padding:16px;}
.hero{background:var(--surface-container-high);border-radius:16px;padding:20px;margin-bottom:16px;}
.hero .label{font-family:'JetBrains Mono';font-size:11px;letter-spacing:0.05em;color:var(--on-surface-variant);text-transform:uppercase;}
.hero h1{font-size:26px;margin:6px 0;}
.hero p{color:var(--on-surface-variant);font-size:14px;margin:0;}
.mood{display:inline-block;margin-top:12px;background:var(--primary-container);color:var(--primary);padding:6px 14px;border-radius:999px;font-weight:700;}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px;margin-bottom:20px;}
.card{background:var(--surface-container-high);border:1px solid var(--surface-variant);border-radius:14px;padding:16px;}
.card .label{font-family:'JetBrains Mono';font-size:10px;letter-spacing:0.05em;color:var(--on-surface-variant);text-transform:uppercase;}
.card .value{font-size:32px;font-weight:800;margin:8px 0 4px;}
.card .unit{font-size:14px;color:var(--on-surface-variant);}
h2{font-size:18px;color:var(--primary);}
.controls{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:12px;}
.ctrl-card{background:rgba(33,58,43,0.4);border:1px solid rgba(161,210,175,0.15);border-radius:14px;padding:16px;}
.ctrl-row{display:flex;justify-content:space-between;align-items:center;margin-bottom:8px;}
.ctrl-title{font-weight:700;}
.ctrl-sub{font-family:'JetBrains Mono';font-size:10px;color:var(--on-surface-variant);text-transform:uppercase;}
.switch{position:relative;width:52px;height:28px;background:var(--surface-container);border-radius:999px;cursor:pointer;flex-shrink:0;}
.switch.on{background:var(--primary-container);}
.switch .knob{position:absolute;top:3px;left:3px;width:22px;height:22px;background:#fff;border-radius:50%;transition:transform 0.2s;}
.switch.on .knob{transform:translateX(24px);}
.mode-toggle{font-family:'JetBrains Mono';font-size:10px;color:var(--on-surface-variant);cursor:pointer;text-decoration:underline;}
input[type=range]{width:100%;margin-top:8px;}
footer{text-align:center;font-size:12px;color:var(--on-surface-variant);margin-top:30px;}
footer a{color:var(--primary);}
</style>
</head>
<body>
<header>
  <div class="logo">EDEN</div>
  <div class="status"><div class="dot" id="statusDot"></div><span id="statusText">SYSTEM: ONLINE</span></div>
</header>
<main>
  <div class="hero">
    <div class="label">Overall Environment</div>
    <h1>Ecosystem Health</h1>
    <p id="heroText">Reading live sensor data...</p>
    <div class="mood" id="moodBadge">--</div>
  </div>

  <div class="grid">
    <div class="card"><div class="label">Soil A</div><div class="value" id="soil_a">--</div><div class="unit">%</div></div>
    <div class="card"><div class="label">Soil B</div><div class="value" id="soil_b">--</div><div class="unit">%</div></div>
    <div class="card"><div class="label">Temperature</div><div class="value" id="temp">--</div><div class="unit">&deg;C</div></div>
    <div class="card"><div class="label">Humidity</div><div class="value" id="humidity">--</div><div class="unit">%</div></div>
    <div class="card"><div class="label">Light</div><div class="value" id="light">--</div><div class="unit">/ 3000</div></div>
    <div class="card"><div class="label">Water Level</div><div class="value" id="water">--</div><div class="unit">cm</div></div>
  </div>

  <h2>Manual System Overrides</h2>
  <div class="controls">

    <div class="ctrl-card">
      <div class="ctrl-row">
        <div><div class="ctrl-title">Grow Light</div><div class="ctrl-sub" id="ledSub">AUTO</div></div>
        <div class="switch" id="ledSwitch" onclick="toggleLED()"><div class="knob"></div></div>
      </div>
      <input type="range" min="0" max="255" id="ledSlider" oninput="setBrightness(this.value)">
      <div class="mode-toggle" onclick="setLedMode('auto')">Reset to Auto (LDR)</div>
    </div>

    <div class="ctrl-card">
      <div class="ctrl-row">
        <div><div class="ctrl-title">Circulation Fan</div><div class="ctrl-sub" id="fanSub">AUTO</div></div>
        <div class="switch" id="fanSwitch" onclick="toggleDevice('fan','fanSwitch')"><div class="knob"></div></div>
      </div>
      <div class="mode-toggle" onclick="setAuto('fan')">Reset to Auto</div>
    </div>

    <div class="ctrl-card">
      <div class="ctrl-row">
        <div><div class="ctrl-title">Pump A</div><div class="ctrl-sub" id="pumpASub">AUTO</div></div>
        <div class="switch" id="pumpASwitch" onclick="toggleDevice('pumpA','pumpASwitch')"><div class="knob"></div></div>
      </div>
      <div class="mode-toggle" onclick="setAuto('pumpA')">Reset to Auto</div>
    </div>

    <div class="ctrl-card">
      <div class="ctrl-row">
        <div><div class="ctrl-title">Pump B</div><div class="ctrl-sub" id="pumpBSub">AUTO</div></div>
        <div class="switch" id="pumpBSwitch" onclick="toggleDevice('pumpB','pumpBSwitch')"><div class="knob"></div></div>
      </div>
      <div class="mode-toggle" onclick="setAuto('pumpB')">Reset to Auto</div>
    </div>

  </div>

  <footer><a href="/settings">Display settings (change ESP32S3 address)</a></footer>
</main>

<script>
let state = {};

function refresh() {
  fetch('/api/data').then(r => r.json()).then(d => {
    state = d;
    document.getElementById('soil_a').textContent  = parseFloat(d.soil_a).toFixed(1);
    document.getElementById('soil_b').textContent  = parseFloat(d.soil_b).toFixed(1);
    document.getElementById('temp').textContent     = parseFloat(d.temp).toFixed(1);
    document.getElementById('humidity').textContent = parseFloat(d.humidity).toFixed(1);
    document.getElementById('light').textContent    = d.light;
    document.getElementById('water').textContent    = parseFloat(d.water).toFixed(1);

    setSwitch('fanSwitch',   d.fan_on);
    setSwitch('pumpASwitch', d.pumpA_on);
    setSwitch('pumpBSwitch', d.pumpB_on);
    setSwitch('ledSwitch',   d.led_brightness > 0);

    document.getElementById('fanSub').textContent   = d.fan_auto   ? 'AUTO' : (d.fan_on   ? 'MANUAL - ON' : 'MANUAL - OFF');
    document.getElementById('pumpASub').textContent = d.pumpA_auto ? 'AUTO' : (d.pumpA_on ? 'MANUAL - ON' : 'MANUAL - OFF');
    document.getElementById('pumpBSub').textContent = d.pumpB_auto ? 'AUTO' : (d.pumpB_on ? 'MANUAL - ON' : 'MANUAL - OFF');
    document.getElementById('ledSub').textContent   = d.led_auto   ? 'AUTO (LDR)' : ('MANUAL - ' + d.led_brightness + '/255');

    if (!document.getElementById('ledSlider').matches(':active')) {
      document.getElementById('ledSlider').value = d.led_brightness;
    }

    const dot = document.getElementById('statusDot');
    const txt = document.getElementById('statusText');
    if (d.online) { dot.classList.remove('off'); txt.textContent = 'SYSTEM: ONLINE'; }
    else          { dot.classList.add('off');    txt.textContent = 'ESP32S3: OFFLINE'; }

    // ===== MOOD SCORE (based on SI units) =====
    // soil_a/b now in % — ideal is 50-70%
    const soilScore  = Math.max(0, 100 - Math.abs(d.soil_a - 60) / 0.6);
    const humidScore = Math.max(0, 100 - Math.abs(d.humidity - 60) / 3.5);
    const tempScore  = Math.max(0, 100 - Math.abs(d.temp - 23) / 5);
    const score = Math.round(soilScore * 0.5 + humidScore * 0.3 + tempScore * 0.2);
    let mood = score >= 80 ? '🌿 Happy' : score >= 60 ? '😐 OK' : score >= 40 ? '😟 Struggling' : '💀 Dying';
    document.getElementById('moodBadge').textContent = score + ' - ' + mood;
    document.getElementById('heroText').textContent = d.online
      ? 'Live readings updating every few seconds from your ESP32S3 sensor hub.'
      : 'Not receiving data from the ESP32S3 — check power/WiFi or the saved IP in Settings.';
  }).catch(() => {});
}

function setSwitch(id, on) {
  const el = document.getElementById(id);
  if (on) el.classList.add('on'); else el.classList.remove('on');
}

function toggleDevice(device, switchId) {
  const isOn = document.getElementById(switchId).classList.contains('on');
  fetch(`/control?device=${device}&mode=manual&state=${!isOn ? 'on' : 'off'}`).then(refresh);
}

function setAuto(device) {
  fetch(`/control?device=${device}&mode=auto`).then(refresh);
}

function toggleLED() {
  const isOn = state.led_brightness > 0;
  fetch(`/control?device=led&mode=manual&brightness=${isOn ? 0 : 200}`).then(refresh);
}

function setBrightness(val) {
  fetch(`/control?device=led&mode=manual&brightness=${val}`).then(refresh);
}

function setLedMode(mode) {
  fetch(`/control?device=led&mode=${mode}`).then(refresh);
}

refresh();
setInterval(refresh, 3000);
</script>
</body></html>
  )HTMLPAGE";
  configServer.send(200, "text/html", html);
}

float extractFloat(String json, String key) {
  int idx = json.indexOf("\"" + key + "\":");
  if (idx == -1) return 0;
  int start = idx + key.length() + 3;
  int end = json.indexOf(",", start);
  int endBrace = json.indexOf("}", start);
  if (end == -1 || (endBrace != -1 && endBrace < end)) end = endBrace;
  return json.substring(start, end).toFloat();
}

bool extractBool(String json, String key) {
  int idx = json.indexOf("\"" + key + "\":");
  if (idx == -1) return false;
  int start = idx + key.length() + 3;
  return json.substring(start, start + 4) == "true";
}

void pollESP32S3() {
  if (WiFi.status() != WL_CONNECTED) { esp32Reachable = false; return; }

  HTTPClient http;
  String url = "http://" + esp32s3IPStr + "/api/data";
  http.begin(url);
  http.setConnectTimeout(1500);
  http.setTimeout(1500);
  int code = http.GET();
  Serial.print("Poll -> HTTP "); Serial.println(code);

  if (code == 200) {
    String body = http.getString();
    soil_a     = extractFloat(body, "soil_a");   // now %
    soil_b     = extractFloat(body, "soil_b");   // now %
    light      = (int)extractFloat(body, "light"); // raw 0-3000
    temp       = extractFloat(body, "temp");
    humidity   = extractFloat(body, "humidity");
    water      = extractFloat(body, "water");
    pumpA_on   = extractBool(body, "pumpA_on");
    pumpA_auto = extractBool(body, "pumpA_auto");
    pumpB_on   = extractBool(body, "pumpB_on");
    pumpB_auto = extractBool(body, "pumpB_auto");
    fan_on     = extractBool(body, "fan_on");
    fan_auto   = extractBool(body, "fan_auto");
    led_brightness = (int)extractFloat(body, "led_brightness");
    led_auto   = extractBool(body, "led_auto");

    esp32Reachable = true;
    lastSuccessfulPoll = millis();
  } else {
    esp32Reachable = false;
  }
  http.end();
}

void sendControl(String query) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = "http://" + esp32s3IPStr + "/control?" + query;
  http.begin(url);
  http.setConnectTimeout(1500);
  http.setTimeout(1500);
  int code = http.GET();
  Serial.print("Control -> HTTP "); Serial.println(code);
  http.end();
}

int findControlTile(int sx, int sy) {
  for (int i = 0; i < 4; i++) {
    Tile t = controlTiles[i];
    if (sx >= t.x && sx <= t.x + t.w && sy >= t.y && sy <= t.y + t.h) return i;
  }
  return -1;
}

void handleTouch() {
  TouchPoint p = ts.getTouch();
  bool pressed = (p.zRaw > 0);

  if (pressed) {
    int sx = mapTouchX(p.x);
    int sy = mapTouchY(p.y);
    int tileIdx = findControlTile(sx, sy);

    if (tileIdx == -1) { activeTileIndex = -1; longPressFired = false; return; }

    if (activeTileIndex != tileIdx) {
      activeTileIndex = tileIdx;
      pressStartTime = millis();
      longPressFired = false;
      showPressIndicator(tileIdx, false);
    } else if (!longPressFired && millis() - pressStartTime >= LONG_PRESS_MS) {
      longPressFired = true;
      showPressIndicator(tileIdx, true);
      performReset(tileIdx);
    }
  } else {
    if (activeTileIndex != -1) {
      if (!longPressFired) performToggle(activeTileIndex);
      activeTileIndex = -1;
      longPressFired = false;
    }
  }
}

void showPressIndicator(int index, bool held) {
  Tile t = controlTiles[index];
  tft.fillRect(t.x, t.y, t.w, t.h, COL_ACCENT);
  tft.setTextColor(COL_BG, COL_ACCENT);
  tft.setTextSize(1);
  tft.setCursor(t.x + 8, t.y + 34);
  tft.print(held ? "AUTO SET" : "...");
}

void performReset(int index) {
  switch (index) {
    case 0: pumpA_auto = true; sendControl("device=pumpA&mode=auto"); break;
    case 1: pumpB_auto = true; sendControl("device=pumpB&mode=auto"); break;
    case 2: fan_auto   = true; sendControl("device=fan&mode=auto");   break;
    case 3: led_auto   = true; sendControl("device=led&mode=auto");   break;
  }
  delay(400);
  pollESP32S3();
  drawUI();
}

void performToggle(int index) {
  switch (index) {
    case 0:
      pumpA_auto = false; pumpA_on = !pumpA_on;
      sendControl(String("device=pumpA&mode=manual&state=") + (pumpA_on ? "on" : "off"));
      break;
    case 1:
      pumpB_auto = false; pumpB_on = !pumpB_on;
      sendControl(String("device=pumpB&mode=manual&state=") + (pumpB_on ? "on" : "off"));
      break;
    case 2:
      fan_auto = false; fan_on = !fan_on;
      sendControl(String("device=fan&mode=manual&state=") + (fan_on ? "on" : "off"));
      break;
    case 3:
      if (led_auto) {
        led_auto = false; led_brightness = 90;
        sendControl("device=led&mode=manual&brightness=90");
      } else if (led_brightness == 0) {
        led_brightness = 90;
        sendControl("device=led&mode=manual&brightness=90");
      } else if (led_brightness < 150) {
        led_brightness = 200;
        sendControl("device=led&mode=manual&brightness=200");
      } else if (led_brightness < 250) {
        led_brightness = 255;
        sendControl("device=led&mode=manual&brightness=255");
      } else {
        led_brightness = 0;
        sendControl("device=led&mode=manual&brightness=0");
      }
      break;
  }
  drawUI();
  pollESP32S3();
  drawUI();
}

void drawUI() {
  tft.fillScreen(COL_BG);

  bool online = esp32Reachable && (millis() - lastSuccessfulPoll < OFFLINE_TIMEOUT);

  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.setTextSize(2);
  tft.setCursor(6, 4);
  tft.print("EDEN");

  tft.setTextSize(1);
  tft.setTextColor(online ? COL_ACCENT : COL_DANGER, COL_BG);
  tft.setCursor(60, 10);
  tft.print(online ? "ONLINE" : "OFFLINE");

  String ipStr = WiFi.localIP().toString();
  tft.setTextColor(COL_SUBTEXT, COL_BG);
  int ipX = 320 - (ipStr.length() * 6) - 6;
  tft.setCursor(ipX, 10);
  tft.print(ipStr);

  tft.drawFastHLine(0, 24, 320, COL_TILE);

  // Soil A/B now show % with 1 decimal
  drawSensorTile(0, "SOIL A", String(soil_a, 1), "%");
  drawSensorTile(1, "SOIL B", String(soil_b, 1), "%");
  drawSensorTile(2, "TEMP",   String(temp, 1),    "C");
  drawSensorTile(3, "HUMID",  String(humidity, 1), "%");
  drawSensorTile(4, "LIGHT",  String(light),       "raw");
  drawSensorTile(5, "WATER",  String(water, 1),    "cm");

  drawControlTile(0, "PUMP A", pumpA_on, pumpA_auto);
  drawControlTile(1, "PUMP B", pumpB_on, pumpB_auto);
  drawControlTile(2, "FAN",    fan_on,   fan_auto);
  drawLedTile(3);

  tft.setTextSize(1);
  tft.setTextColor(COL_SUBTEXT, COL_BG);
  tft.setCursor(6, 225);
  tft.print("ESP32S3: " + esp32s3IPStr);
}

void drawSensorTile(int i, String label, String value, String unit) {
  Tile t = sensorTiles[i];
  tft.fillRect(t.x, t.y, t.w, t.h, COL_TILE);
  tft.setTextColor(COL_SUBTEXT, COL_TILE);
  tft.setTextSize(1);
  tft.setCursor(t.x + 6, t.y + 5);
  tft.print(label);
  tft.setTextColor(COL_TEXT, COL_TILE);
  tft.setTextSize(2);
  tft.setCursor(t.x + 6, t.y + 22);
  tft.print(value);
  if (unit.length() > 0) {
    tft.setTextSize(1);
    tft.setCursor(t.x + 6, t.y + 44);
    tft.print(unit);
  }
}

void drawControlTile(int i, String label, bool on, bool autoMode) {
  Tile t = controlTiles[i];
  uint16_t bg = on ? COL_TILE_ON : COL_TILE;
  tft.fillRect(t.x, t.y, t.w, t.h, bg);
  tft.drawRect(t.x, t.y, t.w, t.h, COL_ACCENT);

  tft.setTextColor(COL_TEXT, bg);
  tft.setTextSize(1);
  tft.setCursor(t.x + 6, t.y + 6);
  tft.print(label);

  tft.setTextColor(on ? COL_ACCENT : COL_SUBTEXT, bg);
  tft.setTextSize(2);
  tft.setCursor(t.x + 6, t.y + 26);
  tft.print(on ? "ON" : "OFF");

  tft.setTextSize(1);
  tft.setTextColor(COL_SUBTEXT, bg);
  tft.setCursor(t.x + 6, t.y + 68);
  tft.print(autoMode ? "AUTO" : "MANUAL");
}

void drawLedTile(int i) {
  Tile t = controlTiles[i];
  bool on = led_brightness > 0;
  uint16_t bg = (on || led_auto) ? COL_TILE_ON : COL_TILE;
  tft.fillRect(t.x, t.y, t.w, t.h, bg);
  tft.drawRect(t.x, t.y, t.w, t.h, COL_ACCENT);

  tft.setTextColor(COL_TEXT, bg);
  tft.setTextSize(1);
  tft.setCursor(t.x + 6, t.y + 6);
  tft.print("LED");

  tft.setTextColor(on ? COL_ACCENT : COL_SUBTEXT, bg);
  tft.setTextSize(2);
  tft.setCursor(t.x + 6, t.y + 26);
  tft.print(led_brightness);

  tft.setTextSize(1);
  tft.setTextColor(COL_SUBTEXT, COL_BG);
  tft.setCursor(t.x + 6, t.y + 68);
  tft.print(led_auto ? "AUTO" : "MANUAL");
}