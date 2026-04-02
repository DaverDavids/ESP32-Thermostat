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
#include <esp_task_wdt.h>

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
const uint8_t PIN_BTN_UP  =  6;
const uint8_t PIN_BTN_DN  =  4;
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
const uint32_t FAST_SAMPLE_MS = 27;   // INA219 poll rate (~37Hz)
const uint32_t REPORT_MS      = 500;  // log/control/history rate (2Hz)
const uint32_t DISPLAY_MS     = 100;  // OLED update rate (10Hz)
// Outlier rejection and dropout thresholds
const float OUTLIER_MAX_JUMP = 150.0f;
const float SHUNT_MIN_MV      = -5.0f;

// ─── Fast sampling / median filter ───────────────────────────────────────────
#define MEDIAN_N 9
float   medBuf[MEDIAN_N];
uint8_t medCount = 0;
unsigned long lastFastSample = 0;

// ─── Button debounce / long-press ──────────────────────────────────────────────
const uint32_t DEBOUNCE_MS      =   30;
const uint32_t CTR_LONGPRESS_MS = 2000;

// ─── Setpoint ramp (hold-to-accelerate) ──────────────────────────────────────
const uint32_t RAMP_DELAY_MS        =  200;
const uint32_t RAMP_RATE_INITIAL_MS =  100;
const uint32_t RAMP_RATE_MIN_MS     =    1;
const float    RAMP_ACCEL           = 0.75f;
const float    SP_STEP_INITIAL      =  1.0f;
const float    SP_STEP_MAX          =  1.0f;
const float    SP_STEP_ACCEL        = 1.15f;
// Only save setpoint to NVS this often during rapid button ramp (ms)
const uint32_t PREFS_SAVE_DEBOUNCE_MS = 2000;

// ─── Auto-ramp: stability / overshoot tracking ───────────────────────────────
// Stability is declared when the temperature has not changed by more than
// STABLE_BAND over STABLE_WINDOW_REPORTS consecutive 500ms reports.
// The baseline for comparison is reset every STABLE_WINDOW_REPORTS samples
// so a slow continuing rise doesn't sneak past the check.
const uint8_t STABLE_WINDOW_REPORTS = 10;  // 5 seconds of reports
const float   STABLE_BAND           =  2.0f; // °C change allowed across window

// Peak tracking: after heater turns off we keep watching until temp starts
// falling, then record the true peak for overshoot calculation.
enum RampPhase {
  RAMP_IDLE,          // not in auto or not waiting
  RAMP_HEATING,       // heater on, climbing toward setpoint
  RAMP_COASTING,      // heater just turned off, watching for peak
  RAMP_WAIT_STABLE,   // past peak, waiting for temp to stabilise
};

RampPhase rampPhase       = RAMP_IDLE;
float     rampPeakTemp    = NAN;    // true peak seen after heater off
float     rampStableBase  = NAN;    // baseline temp at start of stability window
uint8_t   rampStableCount = 0;      // reports since last baseline reset
float     lastOvershoot   = NAN;    // most recent overshoot (°C above setpoint)

// ─── Globals ──────────────────────────────────────────────────────────────────
Preferences      prefs;
WebServer        server(80);
DNSServer        dns;
Adafruit_INA219  ina219;
Adafruit_SSD1306 display(OLED_W, OLED_H, &Wire, -1);

float    setpoint    = 500.0f;
float    currentTemp =   0.0f;
float    lastShuntMV =   0.0f;
float    lastGoodTemp   = NAN;
bool     outputOn       = false;
bool     apMode         = false;
bool     manualOverride = true;

float    probeOffset     =  0.0f;
float    hysteresis      =  5.0f;
int      probeType       =  0;
float    customUvPerC    =  0.0f;
float    cjcOffset       = -12.0f;
float    calMv1          =  0.0f;
float    calTemp1        =  0.0f;
float    calMv2          =  0.0f;
float    calTemp2        =  0.0f;
float    calCjc1         =  0.0f;
float    calCjc2         =  0.0f;
String   savedSSID       = MYSSID;
String   savedPSK        = MYPSK;

const float PROBE_UV_PER_C[] = { 41.0f, 52.0f };

#define HIST_SIZE 720
float    tempHistory[HIST_SIZE];
uint16_t histHead  = 0;
uint16_t histCount = 0;

struct SampleLog { float tC; float shuntmV; };
#define LOG_SIZE 256
SampleLog sampleLog[LOG_SIZE];
uint16_t logHead  = 0;
uint16_t logCount = 0;

unsigned long lastReport        = 0;
unsigned long lastWifiRetry     = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long bootTime          = 0;
unsigned long lastPrefsSave     = 0;   // debounce NVS writes from button ramp
bool otaStarted    = false;
bool serverStarted = false;

// ─── Button state ─────────────────────────────────────────────────────────────
enum BtnPhase { BTN_IDLE, BTN_PENDING, BTN_HELD };

struct BtnState {
  uint8_t   pin;
  BtnPhase  phase;
  unsigned long pendingSince;
  unsigned long nextFire;
  float     currentInterval;
  float     currentStep;
  bool      dirty;   // setpoint changed since last NVS save
} btns[3];

// ─── Forward declarations ─────────────────────────────────────────────────────
void loadPrefs(); void savePrefs();
void startSTA();  void startAP(); void onWifiConnect();
void setupOTA();  void setupRoutes();
float rawTempC(); float medianOf(float* arr, uint8_t n);
void controlLoop(); void updateRampPhase();
void updateButtons(); void updateDisplay();

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_MOSFET, OUTPUT);
  MOSFET_WRITE(false);

  uint8_t btnPins[] = { PIN_BTN_UP, PIN_BTN_DN, PIN_BTN_CTR };
  for (int i = 0; i < 3; i++) {
    btns[i] = { btnPins[i], BTN_IDLE, 0, 0,
                (float)RAMP_RATE_INITIAL_MS, SP_STEP_INITIAL, false };
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
  DBGLN("Ready");
}

// ─── Loop ─────────────────────────────────────────────────────────────────────
void loop() {
  // Feed the watchdog so long control-loop waits don't trigger a reset
  esp_task_wdt_reset();

  if (apMode) dns.processNextRequest();
  updateButtons();

  // Yield to lwIP / BT stack before heavy handlers to keep TCP alive
  yield();
  if (otaStarted)    ArduinoOTA.handle();
  yield();
  if (serverStarted) server.handleClient();
  yield();

  unsigned long now = millis();

  // Fast INA219 sample into median buffer
  if (now - lastFastSample >= FAST_SAMPLE_MS) {
    lastFastSample = now;
    medBuf[medCount % MEDIAN_N] = rawTempC();
    if (medCount < MEDIAN_N) medCount++;
  }

  // Periodic report
  if (now - lastReport >= REPORT_MS) {
    lastReport = now;
    if (medCount > 0) {
      currentTemp = medianOf(medBuf, medCount);
      lastShuntMV = ina219.getShuntVoltage_mV();
    }
    DBG("T: "); DBGLN(currentTemp);
    tempHistory[histHead] = currentTemp;
    histHead = (histHead + 1) % HIST_SIZE;
    if (histCount < HIST_SIZE) histCount++;
    sampleLog[logHead] = { currentTemp, lastShuntMV };
    logHead  = (logHead + 1) % LOG_SIZE;
    if (logCount < LOG_SIZE) logCount++;
    medCount = 0;
    if (!manualOverride) {
      controlLoop();
      updateRampPhase();
    }
  }

  // Button ramp: fire setpoint increments, defer NVS save
  if (btns[0].phase == BTN_HELD && now >= btns[0].nextFire) {
    setpoint = min(setpoint + btns[0].currentStep, 1200.0f);
    btns[0].currentInterval = max((float)RAMP_RATE_MIN_MS, btns[0].currentInterval * RAMP_ACCEL);
    btns[0].currentStep     = min(SP_STEP_MAX, btns[0].currentStep * SP_STEP_ACCEL);
    btns[0].nextFire        = now + (unsigned long)btns[0].currentInterval;
    btns[0].dirty           = true;
  }
  if (btns[1].phase == BTN_HELD && now >= btns[1].nextFire) {
    setpoint = max(setpoint - btns[1].currentStep, 0.0f);
    btns[1].currentInterval = max((float)RAMP_RATE_MIN_MS, btns[1].currentInterval * RAMP_ACCEL);
    btns[1].currentStep     = min(SP_STEP_MAX, btns[1].currentStep * SP_STEP_ACCEL);
    btns[1].nextFire        = now + (unsigned long)btns[1].currentInterval;
    btns[1].dirty           = true;
  }
  // Flush NVS write at most once per PREFS_SAVE_DEBOUNCE_MS
  bool anyDirty = btns[0].dirty || btns[1].dirty;
  if (anyDirty && now - lastPrefsSave >= PREFS_SAVE_DEBOUNCE_MS) {
    savePrefs();
    lastPrefsSave    = now;
    btns[0].dirty    = false;
    btns[1].dirty    = false;
  }

  if (apMode && now - lastWifiRetry > WIFI_RETRY_MS) {
    lastWifiRetry = now;
    WiFi.disconnect(true); delay(100);
    startSTA();
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < WIFI_TIMEOUT_MS) {
      delay(250); yield();
    }
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

  if (now - lastDisplayUpdate >= DISPLAY_MS) {
    lastDisplayUpdate = now;
    if (now - bootTime >= IP_SPLASH_MS) {
      updateDisplay();
    }
  }
}

// ─── Display ──────────────────────────────────────────────────────────────────
void updateDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  const int NUM_Y   =  4;
  const int LBL_Y   = 12;
  const int NUM_CW  = 18;
  const int T_REDGE = 54;
  const int LBL_X   = 55;
  const int SP_X    = 74;

  display.setTextSize(3);
  String tempStr = String((int)round(currentTemp));
  int tempX = T_REDGE - (int)tempStr.length() * NUM_CW;
  if (tempX < 0) tempX = 0;
  display.setCursor(tempX, NUM_Y);
  display.print(tempStr);

  if (manualOverride) {
    display.setTextSize(1);
    display.setCursor(LBL_X, LBL_Y);
    display.print(outputOn ? "ON" : "OFF");
  }

  display.setTextSize(3);
  display.setCursor(SP_X, NUM_Y);
  display.print((int)round(setpoint));

  display.display();
}

// ─── Button handling ──────────────────────────────────────────────────────────
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
          btns[i].nextFire = now + (i == 2 ? CTR_LONGPRESS_MS : RAMP_DELAY_MS);
          if (i == 0) { setpoint = min(setpoint + SP_STEP_INITIAL, 1200.0f); btns[0].dirty = true; }
          if (i == 1) { setpoint = max(setpoint - SP_STEP_INITIAL,    0.0f); btns[1].dirty = true; }
        }
        break;

      case BTN_HELD:
        if (!low) {
          btns[i].phase           = BTN_IDLE;
          btns[i].currentInterval = RAMP_RATE_INITIAL_MS;
          btns[i].currentStep     = SP_STEP_INITIAL;
          // Flush any pending NVS write on button release
          if (btns[i].dirty) {
            savePrefs();
            btns[i].dirty = false;
            lastPrefsSave = now;
          }
        } else if (i == 2 && now >= btns[i].nextFire) {
          btns[i].nextFire = ULONG_MAX;
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
            // Reset ramp tracking when entering auto
            rampPhase       = RAMP_HEATING;
            rampPeakTemp    = NAN;
            rampStableBase  = NAN;
            rampStableCount = 0;
            lastOvershoot   = NAN;
            DBGLN("Auto mode");
          }
        }
        break;
    }
  }
}

// ─── Temperature ──────────────────────────────────────────────────────────────
float rawTempC() {
  float smV   = ina219.getShuntVoltage_mV();
  float cjc_C = temperatureRead() + cjcOffset;
  float uv_per_c = (customUvPerC > 0.0f)
                  ? customUvPerC
                  : PROBE_UV_PER_C[constrain(probeType, 0, 1)];
  float cjc_mV   = (cjc_C * uv_per_c) / 1000.0f;
  float total_mV = smV + cjc_mV;
  float candidate = (total_mV * 1000.0f / uv_per_c) + probeOffset;

  if (smV < SHUNT_MIN_MV) {
    DBG("Dropout smV="); DBGLN(smV);
    return isnan(lastGoodTemp) ? cjc_C : lastGoodTemp;
  }
  if (!isnan(lastGoodTemp) && fabsf(candidate - lastGoodTemp) > OUTLIER_MAX_JUMP) {
    DBG("Jump reject: "); DBGLN(candidate);
    return lastGoodTemp;
  }

  lastShuntMV  = smV;
  lastGoodTemp = candidate;
  return candidate;
}

float medianOf(float* arr, uint8_t n) {
  float tmp[MEDIAN_N];
  for (uint8_t i = 0; i < n; i++) tmp[i] = arr[i];
  for (uint8_t i = 1; i < n; i++) {
    float key = tmp[i]; int8_t j = i - 1;
    while (j >= 0 && tmp[j] > key) { tmp[j+1] = tmp[j]; j--; }
    tmp[j+1] = key;
  }
  return tmp[n/2];
}

// ─── Bang-bang control ────────────────────────────────────────────────────────
void controlLoop() {
  if      (currentTemp < setpoint - hysteresis) outputOn = true;
  else if (currentTemp > setpoint + hysteresis) outputOn = false;
  MOSFET_WRITE(outputOn);
}

// ─── Auto-ramp phase tracker ──────────────────────────────────────────────────
// Called once per REPORT_MS tick, only when !manualOverride.
//
// RAMP_HEATING   -> heater on, climbing. Transitions to RAMP_COASTING the
//                   first time controlLoop() turns the heater off.
// RAMP_COASTING  -> heater off, temp still rising (thermal inertia).
//                   Track the true peak. Transition to RAMP_WAIT_STABLE once
//                   temp starts falling (current < peak - 0.5).
// RAMP_WAIT_STABLE -> temp falling back. Wait for it to genuinely stabilise.
//                   Stability = the temperature change over the last
//                   STABLE_WINDOW_REPORTS reports is less than STABLE_BAND.
//                   The reference baseline is reset each window so a
//                   slow-but-continuing drift doesn't hide the instability.
void updateRampPhase() {
  switch (rampPhase) {

    case RAMP_IDLE:
      // Nothing to track; entered auto via button handler
      break;

    case RAMP_HEATING:
      // Watch for the bang-bang controller turning the heater off
      if (!outputOn) {
        rampPhase    = RAMP_COASTING;
        rampPeakTemp = currentTemp;   // provisional peak, will be updated
        DBG("Ramp: heater off at "); DBGLN(currentTemp);
      }
      break;

    case RAMP_COASTING: {
      // Update the true peak as long as temperature keeps rising
      if (currentTemp > rampPeakTemp) {
        rampPeakTemp = currentTemp;
      }
      // Transition to wait-for-stable once temp has clearly turned around
      // (dropped at least 0.5 C below the peak we've seen)
      if (rampPeakTemp - currentTemp >= 0.5f) {
        lastOvershoot   = rampPeakTemp - setpoint;
        rampPhase       = RAMP_WAIT_STABLE;
        rampStableBase  = currentTemp;   // baseline for first stability window
        rampStableCount = 0;
        DBG("Ramp: peak "); DBG(rampPeakTemp);
        DBG(" overshoot "); DBGLN(lastOvershoot);
      }
      break;
    }

    case RAMP_WAIT_STABLE: {
      rampStableCount++;
      // Reset the reference baseline every STABLE_WINDOW_REPORTS ticks.
      // This means we only declare stable if the LAST window was calm,
      // not if the temperature happened to be similar at two distant points.
      if (rampStableCount >= STABLE_WINDOW_REPORTS) {
        float delta = fabsf(currentTemp - rampStableBase);
        DBG("Stability check delta="); DBGLN(delta);
        if (delta <= STABLE_BAND) {
          DBGLN("Ramp: stable — ready for next step");
          rampPhase = RAMP_IDLE;
        } else {
          // Still moving; slide the window forward
          rampStableBase  = currentTemp;
          rampStableCount = 0;
        }
      }
      break;
    }
  }
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
  setupOTA();
  setupRoutes();
  server.begin();
  otaStarted    = true;
  serverStarted = true;
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
  calCjc1       = prefs.getFloat ("cjc1",    0.0);
  calCjc2       = prefs.getFloat ("cjc2",    0.0);
  savedSSID     = prefs.getString("ssid", MYSSID);
  savedPSK      = prefs.getString("psk",   MYPSK);
  cjcOffset     = prefs.getFloat ("cjco",  -12.0);
  prefs.end();
}

void savePrefs() {
  prefs.begin("therm", false);
  prefs.putFloat ("sp",    setpoint);
  prefs.putFloat ("hyst",  hysteresis);
  prefs.putFloat ("off",   probeOffset);
  prefs.putInt   ("ptype", probeType);
  prefs.putFloat ("uvpc",  customUvPerC);
  prefs.putFloat ("cjc1",  calCjc1);
  prefs.putFloat ("cjc2",  calCjc2);
  prefs.putFloat ("mv1",   calMv1);
  prefs.putFloat ("t1",    calTemp1);
  prefs.putFloat ("mv2",   calMv2);
  prefs.putFloat ("t2",    calTemp2);
  prefs.putString("ssid",  savedSSID);
  prefs.putString("psk",   savedPSK);
  prefs.putFloat ("cjco",  cjcOffset);
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
    float uvPerC = (customUvPerC > 0.0f)
                 ? customUvPerC
                 : PROBE_UV_PER_C[constrain(probeType, 0, 1)];
    float cjc_C   = temperatureRead() + cjcOffset;
    float cjc_mV  = (cjc_C * uvPerC) / 1000.0f;
    float totalMV = lastShuntMV + cjc_mV;

    String j = "{\"temp\":"         + String(currentTemp,    1)
            + ",\"setpoint\":"     + String(setpoint,        1)
            + ",\"output\":"       + String(outputOn ? 1 : 0)
            + ",\"manual\":"       + String(manualOverride ? 1 : 0)
            + ",\"hysteresis\":"   + String(hysteresis,      1)
            + ",\"offset\":"       + String(probeOffset,     1)
            + ",\"probeType\":"    + String(probeType)
            + ",\"customUvPerC\":" + String(customUvPerC,    4)
            + ",\"shuntMV\":"      + String(lastShuntMV,     4)
            + ",\"totalMV\":"      + String(totalMV,         4)
            + ",\"cjcC\":"         + String(cjc_C,           2)
            + ",\"uvPerC\":"       + String(uvPerC,          4)
            + ",\"cjcOffset\":"    + String(cjcOffset,        1)
            + ",\"calMv1\":"       + String(calMv1,          4)
            + ",\"calTemp1\":"     + String(calTemp1,        1)
            + ",\"calCjc1\":"      + String(calCjc1,         2)
            + ",\"calMv2\":"       + String(calMv2,          4)
            + ",\"calTemp2\":"     + String(calTemp2,        1)
            + ",\"calCjc2\":"      + String(calCjc2,         2)
            + ",\"rampPhase\":"    + String((int)rampPhase)
            + ",\"overshoot\":"    + (isnan(lastOvershoot) ? String("null") : String(lastOvershoot, 1))
            + "}";
    server.send(200, "application/json", j);
  });

  server.on("/calibrate", HTTP_POST, []() {
    if (!server.hasArg("mv1") || !server.hasArg("cjc1") || !server.hasArg("temp1") ||
        !server.hasArg("mv2") || !server.hasArg("cjc2") || !server.hasArg("temp2")) {
      server.send(400, "text/plain", "Missing mv1, cjc1, temp1, mv2, cjc2, or temp2");
      return;
    }
    float mv1   = server.arg("mv1").toFloat();
    float temp1 = server.arg("temp1").toFloat();
    float cjc1  = server.arg("cjc1").toFloat();
    float mv2   = server.arg("mv2").toFloat();
    float cjc2  = server.arg("cjc2").toFloat();
    float temp2 = server.arg("temp2").toFloat();
    if (temp2 <= temp1) {
      server.send(400, "text/plain", "temp2 must be greater than temp1");
      return;
    }
    calMv1 = mv1; calTemp1 = temp1; calCjc1 = cjc1;
    calMv2 = mv2; calTemp2 = temp2; calCjc2 = cjc2;
    float dMv   = mv2 - mv1;
    float dTemp = (temp2 - temp1) - (cjc2 - cjc1);
    customUvPerC = (dMv * 1000.0f) / dTemp;
    probeOffset  = temp1 - (mv1 * 1000.0f / customUvPerC) - cjc1;
    savePrefs();
    String response = String("{\"uvPerC\":") + String(customUvPerC, 4)
                    + ",\"offset\":" + String(probeOffset, 4) + "}";
    server.send(200, "application/json", response);
  });

  server.on("/calibrate/clear", HTTP_POST, []() {
    customUvPerC = 0.0f;
    probeOffset  = 0.0f;
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

  server.on("/log", HTTP_GET, []() {
    uint16_t count = (logCount < LOG_SIZE) ? logCount : LOG_SIZE;
    uint16_t start = (logCount < LOG_SIZE) ? 0 : logHead;
    String csv = "idx,tempC,shuntmV\n";
    for (uint16_t i = 0; i < count; i++) {
      SampleLog s = sampleLog[(start + i) % LOG_SIZE];
      csv += String(i) + "," + String(s.tC, 4) + "," + String(s.shuntmV, 4) + "\n";
    }
    server.send(200, "text/plain", csv);
  });

  server.on("/config", HTTP_POST, []() {
    if (server.hasArg("sp"))    setpoint    = server.arg("sp").toFloat();
    if (server.hasArg("hyst"))  hysteresis  = server.arg("hyst").toFloat();
    if (server.hasArg("off"))   probeOffset = server.arg("off").toFloat();
    if (server.hasArg("ptype")) probeType   = server.arg("ptype").toInt();
    if (server.hasArg("cjco"))  cjcOffset   = server.arg("cjco").toFloat();
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

  server.on("/output", HTTP_POST, []() {
    String mode = server.arg("mode");
    if (mode == "on") {
      manualOverride = true; outputOn = true;  MOSFET_WRITE(true);
    } else if (mode == "off") {
      manualOverride = true; outputOn = false; MOSFET_WRITE(false);
    } else if (mode == "auto") {
      manualOverride  = false;
      rampPhase       = RAMP_HEATING;
      rampPeakTemp    = NAN;
      rampStableBase  = NAN;
      rampStableCount = 0;
      lastOvershoot   = NAN;
    } else {
      server.send(400, "text/plain", "mode must be on, off, or auto"); return;
    }
    server.send(200, "text/plain", "OK");
  });
}
