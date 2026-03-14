#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include "html.h"
#include <Secrets.h>

// ─── Debug ────────────────────────────────────────────────────────────────────
#define DEBUG 1
#if DEBUG
  #define DBG(x)   Serial.print(x)
  #define DBGLN(x) Serial.println(x)
#else
  #define DBG(x)
  #define DBGLN(x)
#endif

// ─── Hardware ─────────────────────────────────────────────────────────────────
#define MOSFET_PIN     3    // GPIO driving the MOSFET gate
#define INA219_SDA     8
#define INA219_SCL     9
#define HOSTNAME       "thermostat"

// ─── Globals ──────────────────────────────────────────────────────────────────
Preferences prefs;
WebServer   server(80);
DNSServer   dns;
Adafruit_INA219 ina219;

float   setpoint        = 500.0;
float   currentTemp     = 0.0;
bool    outputOn        = false;
bool    apMode          = false;

// Config (saved to flash)
float   probeOffset     = 0.0;   // calibration offset in °C
float   hysteresis      = 5.0;   // dead-band around setpoint
int     probeType       = 0;     // 0=K, 1=J  (µV/°C coefficient index)
String  savedSSID       = MYSSID;
String  savedPSK        = MYPSK;

// Probe coefficients µV/°C (linear approx, good 200–700°C)
const float PROBE_UV_PER_C[] = { 41.0f, 52.0f };  // K, J

// Temperature history (circular buffer, 1 sample / 5 s → ~1h @ 720 points)
#define HIST_SIZE 720
float   tempHistory[HIST_SIZE];
uint16_t histHead = 0;
uint16_t histCount = 0;

unsigned long lastSample = 0;
#define SAMPLE_MS 5000

// ─── Forward declarations ─────────────────────────────────────────────────────
void loadPrefs();
void savePrefs();
void connectWifi();
void startAP();
void setupOTA();
void setupRoutes();
float readTempC();
void controlLoop();

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(MOSFET_PIN, OUTPUT);
  digitalWrite(MOSFET_PIN, LOW);

  Wire.begin(INA219_SDA, INA219_SCL);
  if (!ina219.begin()) {
    DBGLN("INA219 not found – check wiring");
  } else {
    ina219.setCalibration_32V_32mA();  // lowest range, best resolution
    DBGLN("INA219 ready");
  }

  loadPrefs();
  connectWifi();
  setupOTA();
  setupRoutes();
  server.begin();
  DBGLN("HTTP server started");
}

// ─── Loop ─────────────────────────────────────────────────────────────────────
void loop() {
  if (apMode) dns.processNextRequest();
  ArduinoOTA.handle();
  server.handleClient();

  if (millis() - lastSample >= SAMPLE_MS) {
    lastSample = millis();
    currentTemp = readTempC();
    DBG("Temp: "); DBGLN(currentTemp);
    tempHistory[histHead] = currentTemp;
    histHead = (histHead + 1) % HIST_SIZE;
    if (histCount < HIST_SIZE) histCount++;
    controlLoop();
  }
}

// ─── Temperature ──────────────────────────────────────────────────────────────
float readTempC() {
  // INA219 shunt voltage in mV (shunt resistor removed; direct thermocouple)
  float shuntmV = ina219.getShuntVoltage_mV();

  // Cold junction: use internal ESP32 temperature sensor
  float cjc_C = temperatureRead();  // built-in, ±5°C
  float uv_per_c = PROBE_UV_PER_C[constrain(probeType, 0, 1)];
  float cjc_mV = (cjc_C * uv_per_c) / 1000.0f;

  float total_mV = shuntmV + cjc_mV;
  float tempC = (total_mV * 1000.0f) / uv_per_c;  // mV → µV → °C
  return tempC + probeOffset;
}

// ─── Bang-bang control ────────────────────────────────────────────────────────
void controlLoop() {
  if (currentTemp < (setpoint - hysteresis)) {
    outputOn = true;
  } else if (currentTemp > (setpoint + hysteresis)) {
    outputOn = false;
  }
  digitalWrite(MOSFET_PIN, outputOn ? HIGH : LOW);
}

// ─── WiFi / AP ────────────────────────────────────────────────────────────────
void connectWifi() {
  WiFi.setTxPower(WIFI_POWER_15dBm);
  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSSID.c_str(), savedPSK.c_str());
  DBGLN("Connecting to WiFi...");
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 12000) {
    delay(300); DBG(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    DBG("\nIP: "); DBGLN(WiFi.localIP());
    if (MDNS.begin(HOSTNAME)) DBGLN("mDNS: " HOSTNAME ".local");
    apMode = false;
  } else {
    DBGLN("\nWiFi failed – starting AP");
    startAP();
  }
}

void startAP() {
  apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Thermostat-Setup", "configure");
  dns.start(53, "*", WiFi.softAPIP());
  DBGLN("AP started: Thermostat-Setup / configure");
}

// ─── OTA ──────────────────────────────────────────────────────────────────────
void setupOTA() {
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.onStart([]()  { DBGLN("OTA start"); });
  ArduinoOTA.onError([](ota_error_t e) { DBG("OTA error "); DBGLN(e); });
  ArduinoOTA.begin();
}

// ─── Prefs ────────────────────────────────────────────────────────────────────
void loadPrefs() {
  prefs.begin("therm", true);
  setpoint   = prefs.getFloat("sp",   500.0);
  hysteresis = prefs.getFloat("hyst",   5.0);
  probeOffset= prefs.getFloat("off",    0.0);
  probeType  = prefs.getInt  ("ptype",    0);
  savedSSID  = prefs.getString("ssid", MYSSID);
  savedPSK   = prefs.getString("psk",  MYPSK);
  prefs.end();
}

void savePrefs() {
  prefs.begin("therm", false);
  prefs.putFloat("sp",    setpoint);
  prefs.putFloat("hyst",  hysteresis);
  prefs.putFloat("off",   probeOffset);
  prefs.putInt  ("ptype", probeType);
  prefs.putString("ssid", savedSSID);
  prefs.putString("psk",  savedPSK);
  prefs.end();
}

// ─── Web routes ───────────────────────────────────────────────────────────────
void setupRoutes() {
  // Captive portal redirect
  server.onNotFound([](){ server.sendHeader("Location","http://192.168.4.1/"); server.send(302); });

  server.on("/", HTTP_GET, [](){
    server.send(200, "text/html", HTML_INDEX);
  });

  // JSON status for live polling
  server.on("/status", HTTP_GET, [](){
    String j = "{\"temp\":" + String(currentTemp,1)
             + ",\"setpoint\":" + String(setpoint,1)
             + ",\"output\":" + String(outputOn ? 1 : 0)
             + ",\"hysteresis\":" + String(hysteresis,1)
             + ",\"offset\":" + String(probeOffset,1)
             + ",\"probeType\":" + String(probeType)
             + "}";
    server.send(200, "application/json", j);
  });

  // JSON history array for graph
  server.on("/history", HTTP_GET, [](){
    String j = "[";
    uint16_t start = (histCount < HIST_SIZE) ? 0 : histHead;
    for (uint16_t i = 0; i < histCount; i++) {
      if (i) j += ",";
      j += String(tempHistory[(start + i) % HIST_SIZE], 1);
    }
    j += "]";
    server.send(200, "application/json", j);
  });

  // Save config
  server.on("/config", HTTP_POST, [](){
    if (server.hasArg("sp"))    setpoint    = server.arg("sp").toFloat();
    if (server.hasArg("hyst"))  hysteresis  = server.arg("hyst").toFloat();
    if (server.hasArg("off"))   probeOffset = server.arg("off").toFloat();
    if (server.hasArg("ptype")) probeType   = server.arg("ptype").toInt();
    savePrefs();
    server.send(200, "text/plain", "OK");
  });

  // Save WiFi creds (captive portal)
  server.on("/wifi", HTTP_POST, [](){
    if (server.hasArg("ssid") && server.hasArg("psk")) {
      savedSSID = server.arg("ssid");
      savedPSK  = server.arg("psk");
      savePrefs();
      server.send(200, "text/plain", "Saved. Rebooting...");
      delay(1000);
      ESP.restart();
    } else {
      server.send(400, "text/plain", "Missing ssid or psk");
    }
  });
}
