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

// ─── Pin assignments — change here if you rewire ──────────────────────────────────────
const uint8_t PIN_MOSFET  =  3;
const uint8_t PIN_SDA     =  8;
const uint8_t PIN_SCL     =  9;
const uint8_t PIN_BTN_UP  =  4;
const uint8_t PIN_BTN_DN  =  5;
const uint8_t PIN_BTN_CTR =  6;

// ─── OLED ───────────────────────────────────────────────────────────────────────
const uint8_t  OLED_W    = 128;
const uint8_t  OLED_H    =  32;
const uint8_t  OLED_ADDR = 0x3C;
const uint32_t IP_SPLASH_MS = 4000;

// ─── WiFi ───────────────────────────────────────────────────────────────────────
const char*    HOSTNAME        = "thermostat";
const uint32_t WIFI_TIMEOUT_MS =  20000;
const uint32_t WIFI_RETRY_MS   = 300000;
#define        WIFI_TX_POWER     WIFI_POWER_8_5dBm

// ─── Timing ─────────────────────────────────────────────────────────────────────
const uint32_t SAMPLE_MS  = 5000;
const uint32_t DISPLAY_MS =  200;

// ─── Button debounce ───────────────────────────────────────────────────────────────
// Pin must read LOW continuously for DEBOUNCE_MS before a press is accepted.
// Raise this if you still get false triggers; lower it if response feels sluggish.
const uint32_t DEBOUNCE_MS = 30;

// ─── Setpoint ramp (hold-to-accelerate) ───────────────────────────────────────────
const uint32_t RAMP_DELAY_MS        =  400;
const uint32_t RAMP_RATE_INITIAL_MS =  200;
const uint32_t RAMP_RATE_MIN_MS     =   30;
const float    RAMP_ACCEL           = 0.85f;
const float    SP_STEP_INITIAL      =  5.0f;
const float    SP_STEP_MAX          = 50.0f;
const float    SP_STEP_ACCEL        = 1.15f;

// ─── Globals ──────────────────────────────────────────────────────────────────
Preferences      prefs;
WebServer        server(80);
DNSServer        dns;
Adafruit_INA219  ina219;
Adafruit_SSD1306 display(OLED_W, OLED_H, &Wire, -1);

float    setpoint    = 500.0f;
float    currentTemp =   0.0f;
bool     outputOn    = false;
bool     apMode      = false;

float    probeOffset =  0.0f;
float    hysteresis  =  5.0f;
int      probeType   =  0;
String   savedSSID   = MYSSID;
String   savedPSK    = MYPSK;

const float PROBE_UV_PER_C[] = { 41.0f, 52.0f };

#define HIST_SIZE 720
float    tempHistory[HIST_SIZE];
uint16_t histHead  = 0;
uint16_t histCount = 0;

unsigned long lastSample        = 0;
unsigned long lastWifiRetry     = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long bootTime          = 0;

// ─── Button state ─────────────────────────────────────────────────────────────────
//
// State machine per button:
//   IDLE     -> pin high (released)
//   PENDING  -> pin went low, waiting for DEBOUNCE_MS of stable LOW
//   HELD     -> debounce confirmed, button is genuinely pressed
//
// A press only registers once debounce is confirmed (IDLE->PENDING->HELD).
// Any HIGH reading during PENDING cancels back to IDLE (noise rejection).
//
enum BtnPhase { IDLE, PENDING, HELD };

struct BtnState {
  uint8_t   pin;
  BtnPhase  phase;
  unsigned long pendingSince;   // when we first saw LOW
  unsigned long nextFire;       // next ramp tick timestamp
  float     currentInterval;   // ms between ramp ticks, shrinks over time
  float     currentStep;       // deg C per tick, grows over time
} btns[3];

// ─── Forward declarations ─────────────────────────────────────────────────────
void loadPrefs(); void savePrefs();
void startSTA();  void startAP(); void onWifiConnect();
void setupOTA();  void setupRoutes();
float readTempC(); void controlLoop();
void updateButtons(); void updateDisplay();

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_MOSFET, OUTPUT);
  digitalWrite(PIN_MOSFET, LOW);

  uint8_t btnPins[] = { PIN_BTN_UP, PIN_BTN_DN, PIN_BTN_CTR };
  for (int i = 0; i < 3; i++) {
    btns[i] = { btnPins[i], IDLE, 0, 0, (float)RAMP_RATE_INITIAL_MS, SP_STEP_INITIAL };
    pinMode(btnPins[i], INPUT_PULLUP);
  }

  Wire.begin(PIN_SDA, PIN_SCL);

  if (!ina219.begin()) { DBGLN("INA219 not found"); }
  else { ina219.setCalibration_32V_2A(); DBGLN("INA219 ready"); }

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    DBGLN("SSD1306 not found");
  } else {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 12);
    display.print("  Thermostat...");
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

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 4);
  if (apMode) {
    display.print(" AP: Thermostat-Setup");
    display.setCursor(0, 16);
    display.print(" pw: configure");
  } else {
    display.print(" IP: ");
    display.print(WiFi.localIP());
    display.setCursor(0, 16);
    display.print(" thermostat.local");
  }
  display.display();
  bootTime = millis();

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

  unsigned long now = millis();

  // UP: ramp while held
  if (btns[0].phase == HELD && now >= btns[0].nextFire) {
    setpoint = min(setpoint + btns[0].currentStep, 1200.0f);
    btns[0].currentInterval = max((float)RAMP_RATE_MIN_MS, btns[0].currentInterval * RAMP_ACCEL);
    btns[0].currentStep     = min(SP_STEP_MAX, btns[0].currentStep * SP_STEP_ACCEL);
    btns[0].nextFire        = now + (unsigned long)btns[0].currentInterval;
    savePrefs();
  }

  // DOWN: ramp while held
  if (btns[1].phase == HELD && now >= btns[1].nextFire) {
    setpoint = max(setpoint - btns[1].currentStep, 0.0f);
    btns[1].currentInterval = max((float)RAMP_RATE_MIN_MS, btns[1].currentInterval * RAMP_ACCEL);
    btns[1].currentStep     = min(SP_STEP_MAX, btns[1].currentStep * SP_STEP_ACCEL);
    btns[1].nextFire        = now + (unsigned long)btns[1].currentInterval;
    savePrefs();
  }

  // CENTER: toggle on confirmed press (rising edge of HELD)
  // handled inside updateButtons() via a one-shot flag — see below

  if (apMode && now - lastWifiRetry > WIFI_RETRY_MS) {
    lastWifiRetry = now;
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

  if (now - lastSample >= SAMPLE_MS) {
    lastSample = now;
    currentTemp = readTempC();
    DBG("T: "); DBGLN(currentTemp);
    tempHistory[histHead] = currentTemp;
    histHead = (histHead + 1) % HIST_SIZE;
    if (histCount < HIST_SIZE) histCount++;
    controlLoop();
  }

  if (now - lastDisplayUpdate >= DISPLAY_MS) {
    lastDisplayUpdate = now;
    if (now - bootTime >= IP_SPLASH_MS) {
      updateDisplay();
    }
  }
}

// ─── Display ────────────────────────────────────────────────────────────────────
void updateDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(3);

  const int Y       = 4;
  const int CHAR_W  = 18;
  const int R_EDGE  = 56;
  const int ARROW_X = 60;
  const int SP_X    = 78;

  String tempStr = String((int)round(currentTemp));
  int tempX = R_EDGE - (tempStr.length() * CHAR_W);
  if (tempX < 0) tempX = 0;
  display.setCursor(tempX, Y);
  display.print(tempStr);

  if (outputOn) {
    display.setCursor(ARROW_X, Y);
    display.print(">");
  }

  display.setCursor(SP_X, Y);
  display.print((int)round(setpoint));

  display.display();
}

// ─── Button handling ──────────────────────────────────────────────────────────────
//
// State machine:
//   IDLE:    pin HIGH  -> stay IDLE
//            pin LOW   -> record pendingSince, go PENDING
//   PENDING: pin HIGH  -> noise, back to IDLE  (this is the key rejection step)
//            pin LOW and elapsed < DEBOUNCE_MS -> wait
//            pin LOW and elapsed >= DEBOUNCE_MS -> confirmed! go HELD, fire first tick
//   HELD:    pin HIGH  -> release, back to IDLE, reset ramp
//            pin LOW   -> stay HELD (ramp ticks handled in loop())
//
void updateButtons() {
  unsigned long now = millis();
  for (int i = 0; i < 3; i++) {
    bool low = !digitalRead(btns[i].pin);  // true = pressed (active low)

    switch (btns[i].phase) {

      case IDLE:
        if (low) {
          btns[i].phase        = PENDING;
          btns[i].pendingSince = now;
        }
        break;

      case PENDING:
        if (!low) {
          // Glitch — wasn't held long enough, ignore
          btns[i].phase = IDLE;
        } else if (now - btns[i].pendingSince >= DEBOUNCE_MS) {
          // Stable LOW for DEBOUNCE_MS: it's a real press
          btns[i].phase           = HELD;
          btns[i].currentInterval = RAMP_RATE_INITIAL_MS;
          btns[i].currentStep     = SP_STEP_INITIAL;
          btns[i].nextFire        = now + RAMP_DELAY_MS;
          // Fire first tick immediately for UP/DOWN
          if (i == 0) { setpoint = min(setpoint + SP_STEP_INITIAL, 1200.0f); savePrefs(); }
          if (i == 1) { setpoint = max(setpoint - SP_STEP_INITIAL,    0.0f); savePrefs(); }
          // CENTER: toggle output on confirmed press
          if (i == 2) {
            outputOn = !outputOn;
            digitalWrite(PIN_MOSFET, outputOn ? HIGH : LOW);
            DBGLN("Manual toggle");
          }
        }
        break;

      case HELD:
        if (!low) {
          // Released — reset ramp state
          btns[i].phase           = IDLE;
          btns[i].currentInterval = RAMP_RATE_INITIAL_MS;
          btns[i].currentStep     = SP_STEP_INITIAL;
        }
        break;
    }
  }
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
  digitalWrite(PIN_MOSFET, outputOn ? HIGH : LOW);
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
  if (MDNS.begin(HOSTNAME)) {
    DBGLN(String("mDNS: ") + HOSTNAME + ".local");
  }
}

// ─── OTA ──────────────────────────────────────────────────────────────────────
void setupOTA() {
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.onStart([]()  { DBGLN("OTA start"); });
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
