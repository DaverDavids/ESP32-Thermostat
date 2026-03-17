#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <Adafruit_SSD1306.h>
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

// ─── Hardware pins ────────────────────────────────────────────────────────────────
#define MOSFET_PIN      3
#define INA219_SDA      8
#define INA219_SCL      9
#define BTN_UP          4
#define BTN_DOWN        5
#define BTN_CENTER      6

// OLED (I2C shared with INA219)
#define OLED_WIDTH      128
#define OLED_HEIGHT      64
#define OLED_ADDR      0x3C

// WiFi
#define HOSTNAME        "thermostat"
#define WIFI_TIMEOUT_MS  20000
#define WIFI_RETRY_MS   300000
#define WIFI_TX_POWER   WIFI_POWER_8_5dBm  // C3 Supermini antenna fix

// Button debounce / repeat
#define DEBOUNCE_MS      50
#define REPEAT_DELAY_MS 500   // hold before auto-repeat starts
#define REPEAT_RATE_MS  150   // auto-repeat interval
#define SP_STEP          5.0f // setpoint change per click (deg C)

// ─── Globals ──────────────────────────────────────────────────────────────────
Preferences     prefs;
WebServer       server(80);
DNSServer       dns;
Adafruit_INA219 ina219;
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

float    setpoint    = 500.0;
float    currentTemp = 0.0;
bool     outputOn    = false;
bool     apMode      = false;

float    probeOffset = 0.0;
float    hysteresis  = 5.0;
int      probeType   = 0;      // 0=K, 1=J
String   savedSSID   = MYSSID;
String   savedPSK    = MYPSK;

const float PROBE_UV_PER_C[] = { 41.0f, 52.0f };

#define HIST_SIZE 720
float    tempHistory[HIST_SIZE];
uint16_t histHead  = 0;
uint16_t histCount = 0;

unsigned long lastSample    = 0;
unsigned long lastWifiRetry = 0;
unsigned long lastDisplayUpdate = 0;
#define SAMPLE_MS        5000
#define DISPLAY_MS        500

// Button state tracking for debounce + hold-repeat
struct BtnState {
  uint8_t pin;
  bool    lastRaw;
  bool    pressed;       // debounced press event (single shot)
  unsigned long downAt;
  unsigned long repeatAt;
} btns[3];

// ─── Forward declarations ─────────────────────────────────────────────────────
void loadPrefs(); void savePrefs();
void startSTA();  void startAP(); void onWifiConnect();
void setupOTA();  void setupRoutes();
float readTempC();
void controlLoop();
void updateButtons();
bool btnFired(uint8_t idx);
void updateDisplay();

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(MOSFET_PIN, OUTPUT);
  digitalWrite(MOSFET_PIN, LOW);

  // Buttons: active-low with internal pullup
  btns[0] = { BTN_UP,     true, false, 0, 0 };
  btns[1] = { BTN_DOWN,   true, false, 0, 0 };
  btns[2] = { BTN_CENTER, true, false, 0, 0 };
  for (auto &b : btns) pinMode(b.pin, INPUT_PULLUP);

  Wire.begin(INA219_SDA, INA219_SCL);

  if (!ina219.begin()) { DBGLN("INA219 not found"); }
  else { ina219.setCalibration_32V_2A(); DBGLN("INA219 ready"); }

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    DBGLN("SSD1306 not found");
  } else {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Thermostat");
    display.println("Starting...");
    display.display();
    DBGLN("OLED ready");
  }

  loadPrefs();

  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  delay(100);

  startSTA();
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < WIFI_TIMEOUT_MS) {
    delay(250); DBG(".");
  }
  WiFi.status() == WL_CONNECTED ? onWifiConnect() : (DBGLN("\nSTA timeout"), startAP());

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
  updateButtons();

  // UP: raise setpoint
  if (btnFired(0)) {
    setpoint += SP_STEP;
    savePrefs();
    DBGLN("SP+");
  }
  // DOWN: lower setpoint
  if (btnFired(1)) {
    setpoint -= SP_STEP;
    savePrefs();
    DBGLN("SP-");
  }
  // CENTER: toggle output lock (manual override off)
  if (btnFired(2)) {
    outputOn = !outputOn;
    digitalWrite(MOSFET_PIN, outputOn ? HIGH : LOW);
    DBGLN("Manual toggle");
  }

  // Background WiFi retry
  if (apMode && millis() - lastWifiRetry > WIFI_RETRY_MS) {
    lastWifiRetry = millis();
    WiFi.disconnect(true); delay(100);
    startSTA();
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < WIFI_TIMEOUT_MS) delay(250);
    if (WiFi.status() == WL_CONNECTED) {
      dns.stop(); apMode = false; onWifiConnect();
    } else {
      WiFi.mode(WIFI_AP);
      WiFi.softAP("Thermostat-Setup", "configure");
      WiFi.setTxPower(WIFI_TX_POWER);
      delay(100);
      dns.start(53, "*", WiFi.softAPIP());
    }
  }

  // Temperature sample
  if (millis() - lastSample >= SAMPLE_MS) {
    lastSample = millis();
    currentTemp = readTempC();
    DBG("T: "); DBGLN(currentTemp);
    tempHistory[histHead] = currentTemp;
    histHead = (histHead + 1) % HIST_SIZE;
    if (histCount < HIST_SIZE) histCount++;
    controlLoop();
  }

  // Display refresh
  if (millis() - lastDisplayUpdate >= DISPLAY_MS) {
    lastDisplayUpdate = millis();
    updateDisplay();
  }
}

// ─── Display ────────────────────────────────────────────────────────────────────
void updateDisplay() {
  display.clearDisplay();

  // Row 1: big current temp
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.print(currentTemp, 1);
  display.print((char)247);  // degree symbol
  display.println("C");

  // Row 2: setpoint + output state
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.print("SP:");
  display.print(setpoint, 0);
  display.print((char)247);
  display.print("C  ");
  display.println(outputOn ? "[ON] " : "[OFF]");

  // Row 3: WiFi status
  display.setCursor(0, 32);
  if (apMode) {
    display.print("AP: Thermostat-Setup");
  } else {
    display.print("IP:");
    display.print(WiFi.localIP());
  }

  // Row 4: mini bar graph (last 64 samples scaled to 8px height)
  display.setCursor(0, 44);
  display.print("Hist:");
  uint16_t bars = min((uint16_t)64, histCount);
  uint16_t start = (histCount <= 64) ? 0 : (histHead + HIST_SIZE - 64) % HIST_SIZE;
  float mn = currentTemp - 50, mx = currentTemp + 50;
  for (uint16_t i = 0; i < bars; i++) {
    float v = tempHistory[(start + i) % HIST_SIZE];
    int h = constrain((int)((v - mn) / (mx - mn) * 8), 0, 8);
    display.drawFastVLine(64 + i, 63 - h, h, SSD1306_WHITE);
  }
  // Setpoint line on bar graph
  int spY = constrain((int)((setpoint - mn) / (mx - mn) * 8), 0, 8);
  display.drawFastHLine(64, 63 - spY, 64, SSD1306_WHITE);

  display.display();
}

// ─── Button handling ──────────────────────────────────────────────────────────────
void updateButtons() {
  unsigned long now = millis();
  for (auto &b : btns) {
    b.pressed = false;
    bool raw = !digitalRead(b.pin);  // active low
    if (raw && !b.lastRaw) {
      // fresh press
      b.downAt   = now;
      b.repeatAt = now + REPEAT_DELAY_MS;
      b.pressed  = true;
    } else if (raw && now >= b.repeatAt) {
      // held down - auto-repeat
      b.repeatAt = now + REPEAT_RATE_MS;
      b.pressed  = true;
    }
    b.lastRaw = raw;
  }
}

bool btnFired(uint8_t idx) { return btns[idx].pressed; }

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

// ─── WiFi helpers ───────────────────────────────────────────────────────────────
void startSTA() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSSID.c_str(), savedPSK.c_str());
  WiFi.setTxPower(WIFI_TX_POWER);
  DBG("Connecting: "); DBGLN(savedSSID);
}

void startAP() {
  apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Thermostat-Setup", "configure");
  WiFi.setTxPower(WIFI_TX_POWER);
  delay(200);
  dns.start(53, "*", WiFi.softAPIP());
  DBG("AP IP: "); DBGLN(WiFi.softAPIP());
}

void onWifiConnect() {
  apMode = false;
  DBG("\nIP: "); DBGLN(WiFi.localIP());
  MDNS.end();
  if (MDNS.begin(HOSTNAME)) DBGLN("mDNS: " HOSTNAME ".local");
}

// ─── OTA ──────────────────────────────────────────────────────────────────────
void setupOTA() {
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.onStart([]()  { DBGLN("OTA"); });
  ArduinoOTA.onError([](ota_error_t e) { DBG("OTA err "); DBGLN(e); });
  ArduinoOTA.begin();
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
    String j = "{\"temp\":"        + String(currentTemp,  1)
             + ",\"setpoint\":"   + String(setpoint,      1)
             + ",\"output\":"     + String(outputOn ? 1 : 0)
             + ",\"hysteresis\":" + String(hysteresis,    1)
             + ",\"offset\":"     + String(probeOffset,   1)
             + ",\"probeType\":"  + String(probeType)
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
