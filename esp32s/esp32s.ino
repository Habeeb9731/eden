#include "DHT.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include "esp_system.h"

// ===== MULTIPLE WIFI SETTINGS =====
struct WiFiNetwork {
  const char* ssid;
  const char* password;
};

const int NUM_NETWORKS = 2;
WiFiNetwork networks[NUM_NETWORKS] = {
  {"Najeeb_Home", "meowmeow@1234"},
  {"Najeeb_iPhone", "najeeb1999"}
};

// ===== WEB SERVER =====
WebServer server(80);

// ===== PIN CONFIGURATION =====
const int ULTRASONIC_TRIG = D1;
const int ULTRASONIC_ECHO = D0;
const int DHT_PIN = D6;
#define DHTTYPE DHT22

const int LIGHT_SENSOR = D8;
const int SOIL_A  = D10;
const int SOIL_B  = D9;

const int RELAY_IN1 = D4;   // Pump A
const int RELAY_IN2 = D3;   // Pump B
const int RELAY_IN3 = D2;   // Fan
const int RELAY_IN4 = D5;   // Unused

const int MOSFET_PIN = D7;
const int PWM_FREQ   = 5000;
const int PWM_RES    = 8;

DHT dht(DHT_PIN, DHTTYPE);

// ===== CALIBRATION (measured from your hardware) =====
// Soil: 2700 = 0% (dry), 1150 = 100% (wet)
const int SOIL_RAW_DRY = 2700;
const int SOIL_RAW_WET = 1150;

// Light: 0 = dark, 4-15 = normal room bulb, 3000 = direct sun/torch
const int LIGHT_RAW_MIN    = 0;
const int LIGHT_RAW_MAX    = 3000;

// LED auto brightness thresholds (raw LDR values)
// Normal room (4-15) → LED nearly full ON
// Above 500 raw → start dimming
// At 3000 → fully OFF
const int LED_RAW_FULL_ON  = 20;    // below this → LED = 255 (full)
const int LED_RAW_FULL_OFF = 3000;  // above this → LED = 0 (off)

// ===== SI UNIT THRESHOLDS =====
const float SOIL_PUMP_ON_PCT  = 40.0;  // pump ON  when soil drops below 40%
const float SOIL_PUMP_OFF_PCT = 60.0;  // pump OFF when soil rises above 60%
const float TEMP_MIN     = 18.0;
const float TEMP_MAX     = 28.0;
const float HUMIDITY_MIN = 40.0;
const float HUMIDITY_MAX = 80.0;
// Measured: this reservoir reads ~5.3cm when completely empty (shallow tank,
// sensor mounted close). Trigger "low" before hitting that true-empty ceiling.
const float WATER_LOW_CM = 4.5;   // block pumps if water level > 4.5cm from sensor

const unsigned long MIN_RUN_TIME = 8000;

// ===== LIVE SENSOR STATE =====
float live_soil_a_pct = 0;   // %
float live_soil_b_pct = 0;   // %
int   live_light_raw  = 0;   // raw (0-3000)
float live_temp       = 0;   // °C
float live_humidity   = 0;   // %
float live_water      = 0;   // cm

int raw_soil_a = 0;
int raw_soil_b = 0;

// Soil hysteresis
bool zoneA_wasDry = false;
bool zoneB_wasDry = false;

// Actuator state
unsigned long pumpA_lastChange = 0;
unsigned long pumpB_lastChange = 0;
unsigned long fan_lastChange   = 0;
bool pumpA_state = false;
bool pumpB_state = false;
bool fan_state   = false;

// Manual override
bool pumpA_auto = true;
bool pumpB_auto = true;
bool fan_auto   = true;
bool led_auto   = true;
int  led_brightness_override = 0;

// Timing
unsigned long lastSensorRead = 0;
const unsigned long SENSOR_READ_INTERVAL = 1000;
unsigned long lastPrintTime = 0;
const unsigned long PRINT_INTERVAL = 5000;

// ===== CONVERSION FUNCTIONS =====
float soilRawToPercent(int raw) {
  // 2700 = 0% (dry), 1150 = 100% (wet)
  float pct = ((float)(SOIL_RAW_DRY - raw) / (float)(SOIL_RAW_DRY - SOIL_RAW_WET)) * 100.0;
  return constrain(pct, 0.0, 100.0);
}

int rawToLEDBrightness(int raw) {
  // 0-20   → 255 (full ON, dark room)
  // 20-3000 → linear fade from 255 to 0
  // 3000+  → 0 (fully OFF, bright sun)
  if (raw <= LED_RAW_FULL_ON)  return 255;
  if (raw >= LED_RAW_FULL_OFF) return 0;
  return map(raw, LED_RAW_FULL_ON, LED_RAW_FULL_OFF, 255, 0);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  esp_reset_reason_t reason = esp_reset_reason();
  Serial.print("Reset reason: ");
  Serial.println(reason);
  delay(1500);

  Serial.println("\n========================================");
  Serial.println("  EDEN SMART TERRARIUM - ESP32S3");
  Serial.println("  Soil: % moisture | Light: raw 0-3000");
  Serial.println("  Temp: C | Humidity: % | Water: cm");
  Serial.println("========================================\n");

  pinMode(SOIL_A, INPUT);
  pinMode(SOIL_B, INPUT);
  pinMode(LIGHT_SENSOR, INPUT);
  pinMode(ULTRASONIC_TRIG, OUTPUT);
  pinMode(ULTRASONIC_ECHO, INPUT);

  pinMode(RELAY_IN1, OUTPUT);
  pinMode(RELAY_IN2, OUTPUT);
  pinMode(RELAY_IN3, OUTPUT);
  pinMode(RELAY_IN4, OUTPUT);

  digitalWrite(RELAY_IN1, HIGH);
  digitalWrite(RELAY_IN2, HIGH);
  digitalWrite(RELAY_IN3, HIGH);
  digitalWrite(RELAY_IN4, HIGH);
  Serial.println("✓ All relays OFF");

  ledcAttach(MOSFET_PIN, PWM_FREQ, PWM_RES);
  ledcWrite(MOSFET_PIN, 0);
  Serial.println("✓ MOSFET PWM ready");

  dht.begin();
  Serial.println("✓ Sensors initialized");

  Serial.println("Warming up DHT22...");
  delay(3000);

  float testTemp  = dht.readTemperature();
  float testHumid = dht.readHumidity();
  Serial.print("DHT22: ");
  Serial.print(testTemp); Serial.print("°C, ");
  Serial.print(testHumid); Serial.println("%");
  if (isnan(testTemp) || isnan(testHumid)) {
    Serial.println("⚠️  DHT22 NOT RESPONDING!");
  } else {
    Serial.println("✓ DHT22 OK");
  }

  WiFi.mode(WIFI_STA);
  delay(100);
  connectToMultipleWiFi();

  server.on("/api/data", handleAPIData);
  server.on("/control",  handleControl);
  server.begin();
  Serial.println("✓ Web server on port 80");

  Serial.println("\n========================================");
  Serial.println("  THRESHOLDS:");
  Serial.println("  Pump ON  when soil < 40%");
  Serial.println("  Pump OFF when soil > 60%");
  Serial.println("  Fan ON outside 18-28C or 40-80% RH");
  Serial.println("  LED FULL when light raw < 20 (dark room)");
  Serial.println("  LED OFF  when light raw = 3000 (sun)");
  Serial.println("========================================\n");
}

void loop() {
  server.handleClient();

  unsigned long now = millis();

  if (now - lastSensorRead > SENSOR_READ_INTERVAL) {
    raw_soil_a       = analogRead(SOIL_A);
    raw_soil_b       = analogRead(SOIL_B);
    live_light_raw   = analogRead(LIGHT_SENSOR);
    live_temp        = dht.readTemperature();
    live_humidity    = dht.readHumidity();
    live_water       = getUltrasonicDistance();

    live_soil_a_pct  = soilRawToPercent(raw_soil_a);
    live_soil_b_pct  = soilRawToPercent(raw_soil_b);

    controlSystem();

    if (now - lastPrintTime > PRINT_INTERVAL) {
      printSensorData();
      lastPrintTime = now;
    }

    lastSensorRead = now;
  }

  delay(10);
}

float getUltrasonicDistance() {
  digitalWrite(ULTRASONIC_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(ULTRASONIC_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(ULTRASONIC_TRIG, LOW);
  long duration = pulseIn(ULTRASONIC_ECHO, HIGH, 30000);
  return (duration * 0.034) / 2;
}

void handleAPIData() {
  String json = "{";
  json += "\"soil_a\":"  + String(live_soil_a_pct, 1) + ",";  // %
  json += "\"soil_b\":"  + String(live_soil_b_pct, 1) + ",";  // %
  json += "\"light\":"   + String(live_light_raw)      + ",";  // raw 0-3000
  json += "\"temp\":"    + String(live_temp, 1)         + ",";  // °C
  json += "\"humidity\":" + String(live_humidity, 1)   + ",";  // %
  json += "\"water\":"   + String(live_water, 1)        + ",";  // cm
  json += "\"pumpA_on\":" + String(pumpA_state  ? "true" : "false") + ",";
  json += "\"pumpA_auto\":" + String(pumpA_auto ? "true" : "false") + ",";
  json += "\"pumpB_on\":" + String(pumpB_state  ? "true" : "false") + ",";
  json += "\"pumpB_auto\":" + String(pumpB_auto ? "true" : "false") + ",";
  json += "\"fan_on\":"  + String(fan_state      ? "true" : "false") + ",";
  json += "\"fan_auto\":" + String(fan_auto      ? "true" : "false") + ",";
  json += "\"led_brightness\":" + String(led_brightness_override) + ",";
  json += "\"led_auto\":" + String(led_auto      ? "true" : "false");
  json += "}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

void handleControl() {
  String device = server.arg("device");
  String mode   = server.arg("mode");

  if (device == "pumpA") {
    if (mode == "auto") { pumpA_auto = true; }
    else if (mode == "manual") {
      pumpA_auto  = false;
      pumpA_state = (server.arg("state") == "on");
      pumpA_lastChange = millis();
      digitalWrite(RELAY_IN1, pumpA_state ? LOW : HIGH);
    }
  }
  else if (device == "pumpB") {
    if (mode == "auto") { pumpB_auto = true; }
    else if (mode == "manual") {
      pumpB_auto  = false;
      pumpB_state = (server.arg("state") == "on");
      pumpB_lastChange = millis();
      digitalWrite(RELAY_IN2, pumpB_state ? LOW : HIGH);
    }
  }
  else if (device == "fan") {
    if (mode == "auto") { fan_auto = true; }
    else if (mode == "manual") {
      fan_auto  = false;
      fan_state = (server.arg("state") == "on");
      fan_lastChange = millis();
      digitalWrite(RELAY_IN3, fan_state ? LOW : HIGH);
    }
  }
  else if (device == "led") {
    if (mode == "auto") {
      led_auto = true;
      led_brightness_override = 0;
    } else if (mode == "manual") {
      led_auto = false;
      led_brightness_override = server.arg("brightness").toInt();
    }
  }

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", "OK");
}

void controlSystem() {
  bool water_low = (live_water > WATER_LOW_CM);

  // ===== PUMP A =====
  if (pumpA_auto) {
    if (live_soil_a_pct < SOIL_PUMP_ON_PCT)  zoneA_wasDry = true;
    if (live_soil_a_pct > SOIL_PUMP_OFF_PCT) zoneA_wasDry = false;
    bool wantPumpA = (zoneA_wasDry && !water_low);
    if (wantPumpA != pumpA_state && millis() - pumpA_lastChange > MIN_RUN_TIME) {
      pumpA_state = wantPumpA;
      pumpA_lastChange = millis();
      digitalWrite(RELAY_IN1, pumpA_state ? LOW : HIGH);
    }
  }

  // ===== PUMP B =====
  if (pumpB_auto) {
    if (live_soil_b_pct < SOIL_PUMP_ON_PCT)  zoneB_wasDry = true;
    if (live_soil_b_pct > SOIL_PUMP_OFF_PCT) zoneB_wasDry = false;
    bool wantPumpB = (zoneB_wasDry && !water_low);
    if (wantPumpB != pumpB_state && millis() - pumpB_lastChange > MIN_RUN_TIME) {
      pumpB_state = wantPumpB;
      pumpB_lastChange = millis();
      digitalWrite(RELAY_IN2, pumpB_state ? LOW : HIGH);
    }
  }

  // ===== FAN =====
  if (fan_auto) {
    bool wantFan = (live_temp < TEMP_MIN || live_temp > TEMP_MAX ||
                    live_humidity < HUMIDITY_MIN || live_humidity > HUMIDITY_MAX);
    if (wantFan != fan_state && millis() - fan_lastChange > MIN_RUN_TIME) {
      fan_state = wantFan;
      fan_lastChange = millis();
      digitalWrite(RELAY_IN3, fan_state ? LOW : HIGH);
    }
  }

  // ===== LED (LDR-linked, calibrated to your sensor) =====
  if (led_auto) {
    led_brightness_override = rawToLEDBrightness(live_light_raw);
    ledcWrite(MOSFET_PIN, led_brightness_override);
  } else {
    ledcWrite(MOSFET_PIN, led_brightness_override);
  }
}

void connectToMultipleWiFi() {
  Serial.println("\n📡 Connecting to WiFi...");
  for (int i = 0; i < NUM_NETWORKS; i++) {
    Serial.print("Trying: ");
    Serial.println(networks[i].ssid);
    WiFi.begin(networks[i].ssid, networks[i].password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("✅ Connected!");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      return;
    }
    Serial.println("❌ Failed");
  }
  Serial.println("⚠️  No WiFi connection!");
}

void printSensorData() {
  Serial.println("\n========================================");
  Serial.println("  SENSOR READINGS");
  Serial.println("========================================");
  Serial.print("🌱 Soil A: "); Serial.print(live_soil_a_pct, 1);
  Serial.print("%  (raw: "); Serial.print(raw_soil_a); Serial.println(")");
  Serial.print("🌿 Soil B: "); Serial.print(live_soil_b_pct, 1);
  Serial.print("%  (raw: "); Serial.print(raw_soil_b); Serial.println(")");
  Serial.print("💡 Light:  "); Serial.print(live_light_raw);
  Serial.print(" raw  → LED: "); Serial.print(rawToLEDBrightness(live_light_raw));
  Serial.println("/255");
  Serial.print("🌡️  Temp:   "); Serial.print(live_temp, 1); Serial.println(" °C");
  Serial.print("💧 Humid:  "); Serial.print(live_humidity, 1); Serial.println(" %");
  Serial.print("💧 Water:  "); Serial.print(live_water, 1); Serial.println(" cm");
  Serial.println("--- ACTUATORS ---");
  Serial.print("PUMP A: "); Serial.print(pumpA_state ? "ON" : "OFF");
  Serial.println(pumpA_auto ? " (AUTO)" : " (MANUAL)");
  Serial.print("PUMP B: "); Serial.print(pumpB_state ? "ON" : "OFF");
  Serial.println(pumpB_auto ? " (AUTO)" : " (MANUAL)");
  Serial.print("FAN:    "); Serial.print(fan_state ? "ON" : "OFF");
  Serial.println(fan_auto ? " (AUTO)" : " (MANUAL)");
  Serial.print("LED:    "); Serial.print(led_brightness_override);
  Serial.println(led_auto ? "/255 (AUTO)" : "/255 (MANUAL)");
  Serial.println("========================================");
}