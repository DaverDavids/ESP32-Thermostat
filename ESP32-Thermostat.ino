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
#define MOSFET_PIN  3
#define INA219_SDA  8
#define INA219_SCL  9
#define HOSTNAME    "thermostat"
#define WIFI_TIMEOUT_MS   20000   // ms to wait for STA connect on boot
#define WIFI_RETRY_MS    300000   // ms between background STA reconnect attempts (5 min)

// ─── Globals ──────────────────────────────────────────────────────────────────
Preferences     prefs;
WebServer       server(80);
DNSServer       dns;
Adafruit_INA219 ina219;

float    setpoint    = 500.0;
float    currentTemp = 0.0;
bool     outputOn    = false;
bool     apMode      = false;

float    probeOffset  = 0.0;
float    hysteresis   = 5.0;
int      probeType    = 0;      // 0=K, 1=J
String   savedSSID    = MYSSID;
String   savedPSK     = MYPSK;

const float PROBE_UV_PER_C[] = { 41.0f, 52.0f };

#define HIST_SIZE 720
float    tempHistory[HIST_SIZE];
uint16_t histHead  = 0;
uint16_t histCount = 0;

unsigned long lastSample   = 0;
unsigned long lastWifiRetry = 0;
#define SAMPLE_MS 5000

// ─── Forward declarations ─────────────────────────────────────────────────────
void loadPrefs(); void savePrefs();
void startSTA();  void startAP();
void onWifiConnect();
void setupOTA();  void setupRoutes();
float readTempC();
void controlLoop();

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);  // let serial settle
  pinMode(MOSFET_PIN, OUTPUT);
  digitalWrite(MOSFET_PIN, LOW);

  Wire.begin(INA219_SDA, INA219_SCL);
  if (!ina219.begin()) { DBGLN("INA219 not found"); }
  else { ina219.setCalibration_32V_2A(); DBGLN("INA219 ready"); }

  loadPrefs();

  // Reset WiFi state cleanly before doing anything
  WiFi.persistent(false);       // don't let SDK auto-save creds (we handle it)
  WiFi.disconnect(true, true);  // disconnect + erase SDK stored creds
  delay(100);
  WiFi.setTxPower(WIFI_POWER_15dBm);

  // Try STA first; fall back to AP immediately if timeout
  startSTA();
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < WIFI_TIMEOUT_MS) {
    delay(250); DBG(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    onWifiConnect();
  } else {
    DBGLN("\nSTA timeout – AP mode");
    startAP();
  }

  setupOTA();
  setupRoutes();
  server.begin();
  DBGLN("Ready");
}

// ─── Loop ─────────────────────────────────────────────────────────────────────
void loop() {
  if (apMode) dns.processNextRequest();
  ArduinoOTA.handle();
  server.handleClient();

  // Background STA reconnect: if in AP mode, periodically try to rejoin STA
  if (apMode && millis() - lastWifiRetry > WIFI_RETRY_MS) {
    lastWifiRetry = millis();
    DBGLN("Retrying STA...");
    WiFi.disconnect(true);
    delay(100);
    startSTA();
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < WIFI_TIMEOUT_MS) {
      delay(250);
    }
    if (WiFi.status() == WL_CONNECTED) {
      dns.stop();
      apMode = false;
      onWifiConnect();
    } else {
      // Back to AP — restart softAP so clients can still connect
      WiFi.mode(WIFI_AP);
      WiFi.softAP("Thermostat-Setup", "configure");
      dns.start(53, "*", WiFi.softAPIP());
    }
  }

  if (millis() - lastSample >= SAMPLE_MS) {
    lastSample = millis();
    currentTemp = readTempC();
    DBG("T: "); DBGLN(currentTemp);
    tempHistory[histHead] = currentTemp;
    histHead = (histHead + 1) % HIST_SIZE;
    if (histCount < HIST_SIZE) histCount++;
    controlLoop();
  }
}

// ─── WiFi helpers ───────────────────────────────────────────────────────────────
void startSTA() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSSID.c_str(), savedPSK.c_str());
  DBG("Connecting to "); DBGLN(savedSSID);
}

void startAP() {
  apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Thermostat-Setup", "configure");
  delay(100);  // softAP needs a moment before DNS
  dns.start(53, "*", WiFi.softAPIP());
  DBG("AP IP: "); DBGLN(WiFi.softAPIP());
}

void onWifiConnect() {
  apMode = false;
  DBG("\nIP: "); DBGLN(WiFi.localIP());
  MDNS.end();  // restart cleanly in case of reconnect
  if (MDNS.begin(HOSTNAME)) DBGLN("mDNS: " HOSTNAME ".local");
}

// ─── OTA ──────────────────────────────────────────────────────────────────────
void setupOTA() {
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.onStart([]()  { DBGLN("OTA"); });
  ArduinoOTA.onError([](ota_error_t e) { DBG("OTA err "); DBGLN(e); });
  ArduinoOTA.begin();
}

// ─── Temperature ──────────────────────────────────────────────────────────────
float readTempC() {
  float shuntmV  = ina219.getShuntVoltage_mV();
  float cjc_C    = temperatureRead();
  float uv_per_c = PROBE_UV_PER_C[constrain(probeType, 0, 1)];
  float cjc_mV   = (cjc_C * uv_per_c) / 1000.0f;
  float total_mV = shuntmV + cjc_mV;
  return (total_mV * 1000.0f / uv_per_c) + probeOffset;
}

// ─── Bang-bang control ────────────────────────────────────────────────────────
void controlLoop() {
  if      (currentTemp < setpoint - hysteresis) outputOn = true;
  else if (currentTemp > setpoint + hysteresis) outputOn = false;
  digitalWrite(MOSFET_PIN, outputOn ? HIGH : LOW);
}

// ─── Prefs ────────────────────────────────────────────────────────────────────
void loadPrefs() {
  prefs.begin("therm", true);
  setpoint    = prefs.getFloat ("sp",    500.0);
  hysteresis  = prefs.getFloat ("hyst",    5.0);
  probeOffset = prefs.getFloat ("off",     0.0);
  probeType   = prefs.getInt   ("ptype",     0);
  savedSSID   = prefs.getString("ssid", MYSSID);
  savedPSK    = prefs.getString("psk",   MYPSK);
  prefs.end();
}

void savePrefs() {
  prefs.begin("therm", false);
  prefs.putFloat ("sp",    setpoint);
  prefs.putFloat ("hyst",  hysteresis);
  prefs.putFloat ("off",   probeOffset);
  prefs.putInt   ("ptype", probeType);
  prefs.putString("ssid",  savedSSID);
  prefs.putString("psk",   savedPSK);
  prefs.end();
}

// ─── Web routes ───────────────────────────────────────────────────────────────
void setupRoutes() {
  server.onNotFound([]() {
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
    server.send(302);
  });

  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", HTML_INDEX);
  });

  server.on("/status", HTTP_GET, []() {
    String j = "{\"temp\":"      + String(currentTemp,  1)
             + ",\"setpoint\":" + String(setpoint,      1)
             + ",\"output\":"   + String(outputOn ? 1 : 0)
             + ",\"hysteresis\":" + String(hysteresis,  1)
             + ",\"offset\":"   + String(probeOffset,   1)
             + ",\"probeType\":" + String(probeType)
             + "}";
    server.send(200, "application/json", j);
  });

  server.on("/history", HTTP_GET, []() {
    String j = "[";
    uint16_t start = (histCount < HIST_SIZE) ? 0 : histHead;
    for (uint16_t i = 0; i < histCount; i++) {
      if (i) j += ",";
      j += String(tempHistory[(start + i) % HIST_SIZE], 1);
    }
    j += "]";
    server.send(200, "application/json", j);
  });

  server.on("/config", HTTP_POST, []() {
    if (server.hasArg("sp"))    setpoint    = server.arg("sp").toFloat();
    if (server.hasArg("hyst"))  hysteresis  = server.arg("hyst").toFloat();
    if (server.hasArg("off"))   probeOffset = server.arg("off").toFloat();
    if (server.hasArg("ptype")) probeType   = server.arg("ptype").toInt();
    savePrefs();
    server.send(200, "text/plain", "OK");
  });

  server.on("/wifi", HTTP_POST, []() {
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
