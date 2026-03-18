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

// ─── Pin assignments ──────────────────────────────────────────────────────────
const uint8_t PIN_MOSFET  =  3;
const uint8_t PIN_SDA     =  8;
const uint8_t PIN_SCL     =  9;
const uint8_t PIN_BTN_UP  =  4;
const uint8_t PIN_BTN_DN  =  6;
const uint8_t PIN_BTN_CTR =  5;

// outputOn=true  -> PIN_MOSFET LOW  (active-low load)
// outputOn=false -> PIN_MOSFET HIGH
#define MOSFET_WRITE(on) digitalWrite(PIN_MOSFET, (on) ? LOW : HIGH)

// ─── OLED ─────────────────────────────────────────────────────────────────────
const uint8_t  OLED_W    = 128;
const uint8_t  OLED_H    =  32;
const uint8_t  OLED_ADDR = 0x3C;
const uint32_t IP_SPLASH_MS = 2000;

// ─── WiFi ─────────────────────────────────────────────────────────────────────
const char*    HOSTNAME        = "thermostat";
const uint32_t WIFI_TIMEOUT_MS =  20000;
const uint32_t WIFI_RETRY_MS   = 300000;
#define        WIFI_TX_POWER     WIFI_POWER_8_5dBm

// ─── Timing ───────────────────────────────────────────────────────────────────
const uint32_t SAMPLE_MS  = 1000;
const uint32_t DISPLAY_MS =   10;

// ─── Button debounce / long-press ──────────────────────────────────────────────
const uint32_t DEBOUNCE_MS      =   30;
const uint32_t CTR_LONGPRESS_MS = 2000;  // hold time to change state

// ─── Setpoint ramp (hold-to-accelerate) ──────────────────────────────────────
const uint32_t RAMP_DELAY_MS        =  200;
const uint32_t RAMP_RATE_INITIAL_MS =  100;
const uint32_t RAMP_RATE_MIN_MS     =    1;
const float    RAMP_ACCEL           = 1.0f;
const float    SP_STEP_INITIAL      =  1.0f;
const float    SP_STEP_MAX          =  1.0f;
const float    SP_STEP_ACCEL        = 1.15f;

// ─── Globals ──────────────────────────────────────────────────────────────────
Preferences      prefs;
WebServer        server(80);
DNSServer        dns;
Adafruit_INA219  ina219;
Adafruit_SSD1306 display(OLED_W, OLED_H, &Wire, -1);

float    setpoint    = 500.0f;
float    currentTemp =   0.0f;
bool     outputOn       = false;  // relay off at boot
bool     apMode         = false;
bool     manualOverride = true;   // boot into manual OFF

float    probeOffset     =  0.0f;
float    hysteresis      =  5.0f;
int      probeType       =  0;
float    customUvPerC    =  0.0f; // overrides probe type calibration when >0
float    calMv1          =  0.0f;   // raw mV point 1 for calibration
float    calTemp1        =  0.0f;   // raw temp point 1 for calibration
float    calMv2          =  0.0f;   // raw mV point 2 for calibration
float    calTemp2        =  0.0f;   // raw temp point 2 for calibration
String   savedSSID       = MYSSID;
String   savedPSK        = MYPSK;

const float PROBE_UV_PER_C[] = { 41.0f, 52.0f };

#define HIST_SIZE 720
float    tempHistory[HIST_SIZE];
uint16_t histHead  = 0;
uint16_t histCount = 0;

unsigned long lastSample        = 0;
unsigned long lastWifiRetry     = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long bootTime          = 0;

// ─── Button state ─────────────────────────────────────────────────────────────
enum BtnPhase { BTN_IDLE, BTN_PENDING, BTN_HELD };

struct BtnState {
  uint8_t   pin;
  BtnPhase  phase;
  unsigned long pendingSince;
  unsigned long nextFire;
  float     currentInterval;
  float     currentStep;
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
  MOSFET_WRITE(false);

  uint8_t btnPins[] = { PIN_BTN_UP, PIN_BTN_DN, PIN_BTN_CTR };
  for (int i = 0; i < 3; i++) {
    btns[i] = { btnPins[i], BTN_IDLE, 0, 0, (float)RAMP_RATE_INITIAL_MS, SP_STEP_INITIAL };
    pinMode(btnPins[i], INPUT_PULLUP);
  }

  Wire.begin(PIN_SDA, PIN_SCL);

  if (!ina219.begin()) { DBGLN("INA219 not found"); }
  else { ina219.setCalibration_16V_400mA(); DBGLN("INA219 ready"); }

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

  if (btns[0].phase == BTN_HELD && now >= btns[0].nextFire) {
    setpoint = min(setpoint + btns[0].currentStep, 1200.0f);
    btns[0].currentInterval = max((float)RAMP_RATE_MIN_MS, btns[0].currentInterval * RAMP_ACCEL);
    btns[0].currentStep     = min(SP_STEP_MAX, btns[0].currentStep * SP_STEP_ACCEL);
    btns[0].nextFire        = now + (unsigned long)btns[0].currentInterval;
    savePrefs();
  }

  if (btns[1].phase == BTN_HELD && now >= btns[1].nextFire) {
    setpoint = max(setpoint - btns[1].currentStep, 0.0f);
    btns[1].currentInterval = max((float)RAMP_RATE_MIN_MS, btns[1].currentInterval * RAMP_ACCEL);
    btns[1].currentStep     = min(SP_STEP_MAX, btns[1].currentStep * SP_STEP_ACCEL);
    btns[1].nextFire        = now + (unsigned long)btns[1].currentInterval;
    savePrefs();
  }

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
    if (!manualOverride) controlLoop();
  }

  if (now - lastDisplayUpdate >= DISPLAY_MS) {
    lastDisplayUpdate = now;
    if (now - bootTime >= IP_SPLASH_MS) {
      updateDisplay();
    }
  }
}

// ─── Display ──────────────────────────────────────────────────────────────────
//
// Numbers: setTextSize(3) = 18px wide x 24px tall, y=4  (fills 28/32px)
// Label:   setTextSize(1) = 6px wide  x 8px  tall, y=12 (vertically centered)
//
// Pixel budget (128px wide):
//   Temp  right-edge  x=54  (3 digits x 18 = 54px, starts x=0)
//   Gap                     20px  (x=54..74)
//   Label left-edge   x=55  ON=12px, OFF=18px  ✓
//   Setpoint left     x=74  (3 digits x 18 = 54px, ends x=128)  ✓
//
void updateDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  const int NUM_Y   =  4;   // size-3 numbers top
  const int LBL_Y   = 12;   // size-1 label top (vertically centered in 32px)
  const int NUM_CW  = 18;   // size-3 char width
  const int T_REDGE = 54;   // right edge of temp field
  const int LBL_X   = 55;   // left edge of label
  const int SP_X    = 74;   // left edge of setpoint

  // ─ Temp: size 3, right-aligned
  display.setTextSize(3);
  String tempStr = String((int)round(currentTemp));
  int tempX = T_REDGE - (int)tempStr.length() * NUM_CW;
  if (tempX < 0) tempX = 0;
  display.setCursor(tempX, NUM_Y);
  display.print(tempStr);

  // ─ Center label: size 1, only in manual override
  if (manualOverride) {
    display.setTextSize(1);
    display.setCursor(LBL_X, LBL_Y);
    display.print(outputOn ? "ON" : "OFF");
  }

  // ─ Setpoint: size 3, left-aligned
  display.setTextSize(3);
  display.setCursor(SP_X, NUM_Y);
  display.print((int)round(setpoint));

  display.display();
}

// ─── Button handling ──────────────────────────────────────────────────────────
//
// UP / DN: short press + hold ramps setpoint (unchanged)
// CENTER:  must hold CTR_LONGPRESS_MS (2s) to change state
//   manual OFF -> manual ON
//   manual ON  -> manual OFF
//   manual OFF -> auto
//
void updateButtons() {
  unsigned long now = millis();
  for (int i = 0; i < 3; i++) {
    bool low = !digitalRead(btns[i].pin);

    switch (btns[i].phase) {

      case BTN_IDLE:
        if (low) {
          btns[i].phase        = BTN_PENDING;
          btns[i].pendingSince = now;
        }
        break;

      case BTN_PENDING:
        if (!low) {
          btns[i].phase = BTN_IDLE;
        } else if (now - btns[i].pendingSince >= DEBOUNCE_MS) {
          btns[i].phase           = BTN_HELD;
          btns[i].currentInterval = RAMP_RATE_INITIAL_MS;
          btns[i].currentStep     = SP_STEP_INITIAL;
          // UP/DN fire immediately after ramp delay; CENTER waits for long press
          btns[i].nextFire = now + (i == 2 ? CTR_LONGPRESS_MS : RAMP_DELAY_MS);

          // UP / DN: first step on confirm
          if (i == 0) { setpoint = min(setpoint + SP_STEP_INITIAL, 1200.0f); savePrefs(); }
          if (i == 1) { setpoint = max(setpoint - SP_STEP_INITIAL,    0.0f); savePrefs(); }
          // CENTER: action deferred until nextFire (long press)
        }
        break;

      case BTN_HELD:
        if (!low) {
          // released before long press fired for center
          btns[i].phase           = BTN_IDLE;
          btns[i].currentInterval = RAMP_RATE_INITIAL_MS;
          btns[i].currentStep     = SP_STEP_INITIAL;
        } else if (i == 2 && now >= btns[i].nextFire) {
          // CENTER long press fired
          btns[i].nextFire = ULONG_MAX;  // prevent re-fire while still held
          if (!manualOverride) {
            manualOverride = true;
            outputOn       = true;
            MOSFET_WRITE(true);
            DBGLN("Manual ON");
          } else if (outputOn) {
            outputOn = false;
            MOSFET_WRITE(false);
            DBGLN("Manual OFF");
          } else {
            manualOverride = false;
            DBGLN("Auto mode");
          }
        }
        break;
    }
  }
}

// ─── Temperature ──────────────────────────────────────────────────────────────
float readTempC() {
  const int N = 16;
  float shuntmV = 0;
  for (int i = 0; i < N; i++) shuntmV += ina219.getShuntVoltage_mV();
  shuntmV /= N;
  float cjc_C    = temperatureRead();
  float uv_per_c = (customUvPerC > 0.0f)
                 ? customUvPerC
                 : PROBE_UV_PER_C[constrain(probeType, 0, 1)];
  float cjc_mV   = (cjc_C * uv_per_c) / 1000.0f;
  float total_mV = shuntmV + cjc_mV;
  return (total_mV * 1000.0f / uv_per_c) + probeOffset;
}

// ─── Bang-bang control ────────────────────────────────────────────────────────
void controlLoop() {
  if      (currentTemp < setpoint - hysteresis) outputOn = true;
  else if (currentTemp > setpoint + hysteresis) outputOn = false;
  MOSFET_WRITE(outputOn);
}

// ─── WiFi helpers ─────────────────────────────────────────────────────────────
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
  setpoint      = prefs.getFloat ("sp",    500.0);
  hysteresis    = prefs.getFloat ("hyst",    5.0);
  probeOffset   = prefs.getFloat ("off",     0.0);
  probeType     = prefs.getInt   ("ptype",     0);
  customUvPerC  = prefs.getFloat ("uvpc",    0.0);
  calMv1        = prefs.getFloat ("mv1",     0.0);
  calTemp1      = prefs.getFloat ("t1",      0.0);
  calMv2        = prefs.getFloat ("mv2",     0.0);
  calTemp2      = prefs.getFloat ("t2",      0.0);
  savedSSID     = prefs.getString("ssid", MYSSID);
  savedPSK      = prefs.getString("psk",   MYPSK);
  prefs.end();
}

void savePrefs() {
  prefs.begin("therm", false);
  prefs.putFloat ("sp",    setpoint);
  prefs.putFloat ("hyst",  hysteresis);
  prefs.putFloat ("off",   probeOffset);
  prefs.putInt   ("ptype", probeType);
  prefs.putFloat ("uvpc",  customUvPerC);
  prefs.putFloat ("mv1",   calMv1);
  prefs.putFloat ("t1",    calTemp1);
  prefs.putFloat ("mv2",   calMv2);
  prefs.putFloat ("t2",    calTemp2);
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
    float shuntMV    = ina219.getShuntVoltage_mV();
    float uvPerC     = (customUvPerC > 0.0f)
                     ? customUvPerC
                     : PROBE_UV_PER_C[constrain(probeType, 0, 1)];

    String j = "{\"temp\":"        + String(currentTemp,    1)
             + ",\"setpoint\":"   + String(setpoint,        1)
             + ",\"output\":"     + String(outputOn ? 1 : 0)
             + ",\"manual\":"     + String(manualOverride ? 1 : 0)
             + ",\"hysteresis\":" + String(hysteresis,      1)
             + ",\"offset\":"     + String(probeOffset,     1)
             + ",\"probeType\":"  + String(probeType)
             + ",\"shuntMV\":"    + String(shuntMV,       4)
             + ",\"uvPerC\":"     + String(uvPerC,        4)
             + ",\"calMv1\":"     + String(calMv1,        4)
             + ",\"calTemp1\":"   + String(calTemp1,      1)
             + ",\"calMv2\":"     + String(calMv2,        4)
             + ",\"calTemp2\":"   + String(calTemp2,      1)
             + "}";
    server.send(200, "application/json", j);
  });

  server.on("/calibrate", HTTP_POST, []() {
    if (!server.hasArg("mv1") || !server.hasArg("temp1") || 
        !server.hasArg("mv2") || !server.hasArg("temp2")) {
      server.send(400, "text/plain", "Missing mv1, temp1, mv2, or temp2");
      return;
    }
    float mv1   = server.arg("mv1").toFloat();
    float temp1 = server.arg("temp1").toFloat();
    float mv2   = server.arg("mv2").toFloat();
    float temp2 = server.arg("temp2").toFloat();
    
    if (temp2 <= temp1) {
      server.send(400, "text/plain", "temp2 must be greater than temp1");
      return;
    }
    
    // Store raw calibration points
    calMv1 = mv1;
    calTemp1 = temp1;
    calMv2 = mv2;
    calTemp2 = temp2;
    
    // Two-point calibration
    customUvPerC = (mv2 - mv1) * 1000.0f / (temp2 - temp1);
    probeOffset = temp1 - (mv1 * 1000.0f / customUvPerC);
    savePrefs();
    
    String response = String("{\"uvPerC\":") + String(customUvPerC, 4) + 
                     ",\"offset\":" + String(probeOffset, 4) + "}";
    server.send(200, "application/json", response);
  });

  server.on("/calibrate/clear", HTTP_POST, []() {
    customUvPerC = 0.0f;
    probeOffset = 0.0f;
    savePrefs();
    server.send(200, "application/json", "{\"status\":\"cleared\"}");
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
