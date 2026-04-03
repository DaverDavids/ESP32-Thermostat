#include <Arduino.h>
#include <math.h>   // for isnan, fabsf - explicit for clarity
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <Wire.h>
#include <SPIFFS.h>
#include <max6675.h>
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
const uint8_t PIN_MOSFET  =  0;
const uint8_t PIN_SDA     =  10;
const uint8_t PIN_SCL     =  8;
const uint8_t PIN_BTN_UP  =  5;
const uint8_t PIN_BTN_DN  =  7;
const uint8_t PIN_BTN_CTR =  6;
const uint8_t MAX_SCK =  4;
const uint8_t MAX_CS =  3;
const uint8_t MAX_SO =  2;

// outputOn=true  -> PIN_MOSFET HIGH
// outputOn=false -> PIN_MOSFET LOW
#define MOSFET_WRITE(on) digitalWrite(PIN_MOSFET, (on) ? HIGH : LOW)

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
const uint32_t FAST_SAMPLE_MS = 250;  // MAX6675 min conversion time ~170ms; use 250ms
const uint32_t REPORT_MS      = 500;  // log/control/history rate (2Hz)
const uint32_t DISPLAY_MS     = 100;  // OLED update rate (10Hz)
const float    OUTLIER_MAX_JUMP = 150.0f;
uint8_t        jumpRejectCount  = 0;
const uint8_t  JUMP_REJECT_MAX  = 10;

// ─── Fast sampling / median filter ───────────────────────────────────────────
#define MEDIAN_N 9
float   medBuf[MEDIAN_N];
uint8_t medCount = 0;
unsigned long lastFastSample = 0;

// ─── Button debounce / long-press ────────────────────────────────────────────
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

// ─── Run mode / control state ────────────────────────────────────────────────
enum RunMode { MODE_MANUAL = 0, MODE_BANG_BANG = 1, MODE_AUTO_RAMP = 2 };
RunMode selectedMode = MODE_MANUAL;
bool    modeRunning  = false;
bool    heatRequested = false;
bool    stopLatched  = false;

unsigned long modeOverlayUntil = 0;
#define MODE_OVERLAY_MS 1500

uint8_t  estopPressCount = 0;
unsigned long estopWindowStart = 0;
#define ESTOP_PRESSES   3
#define ESTOP_WINDOW_MS 1000

// ─── Ramp support (Stage 2) ───────────────────────────────────────────
#define RAMP_MAX_STEPS 8

struct RampProfile {
  char    name[32];
  float   stepTargets[RAMP_MAX_STEPS]; // °C, last entry = finalTarget
  uint8_t stepCount;                   // total steps including final
  float   soakMinutes;                 // hold time at final temp
  float   stabilityThreshC;            // °C change over window to call stable
  float   coastBase;                   // coast ratio at 0°C (y-intercept of linear model)
  float   coastSlope;                  // change in coast ratio per 100°C (negative)
  bool    complete;                    // was this profile learned on a full run?
};

// Default active profile
RampProfile activeProfile = {
  "default",
  {150.0f, 280.0f, 380.0f, 440.0f, 470.0f, 490.0f, 500.0f},
  7,
  30.0f,
  2.0f,
  0.40f,
  0.03f,
  false
};

// RampState machine
enum RampState {
  RS_IDLE            = 0,
  RS_HEATING         = 1,
  RS_COASTING        = 2,
  RS_SOAKING         = 3,
  RS_OVERSHOOT_WAIT  = 4,
  RS_FINAL_SOAK      = 5,
  RS_DONE            = 6
};

#define MAX_LEARNED_STEPS (RAMP_MAX_STEPS - 1)

float learnedFireStartTemp[MAX_LEARNED_STEPS];
float learnedCutoffTemp[MAX_LEARNED_STEPS];
float learnedPeakTemp[MAX_LEARNED_STEPS];
float learnedCoastRatio[MAX_LEARNED_STEPS];
uint8_t learnedCount = 0;

// Ramp working variables
RampState rampState    = RS_IDLE;
uint8_t   rampStep     = 0;
float     rampFireStartTemp = 0.0f;
float     rampCutoffTemp    = 0.0f;
float     rampPeakTemp      = 0.0f;
float     rampOvershootAmt  = 0.0f;
unsigned long rampStateEnteredMs = 0;
unsigned long finalSoakStartMs   = 0;

// ─── Stability window ─────────────────────────────────────────────────────────
// STABILITY_WINDOW samples × REPORT_MS ms/sample = total observation window.
// At 500ms/sample: 60 samples = 30s, 120 samples = 60s.
#define STABILITY_WINDOW 60
float    stabilityBuf[STABILITY_WINDOW];
uint16_t stabilityIdx    = 0;
uint16_t stabilityFilled = 0;

uint8_t coastingDropCount = 0;

// Helpers (forward declarations)
float effectiveCoastRatio(float tempC);
void refitCoastModel();
bool isStable();
void resetStabilityBuf();

// ─── Duty-cycle limiter ───────────────────────────────────────────────────────
// Limits the output to at most (dutyCyclePct/100) ON-time within every
// dutyCyclePeriodMs rolling window.  Set dutyCyclePct=100 to disable.
// Both values are persisted in Preferences under "dc_pct" / "dc_period".
float    dutyCyclePct      = 100.0f;  // 0–100 %
uint32_t dutyCyclePeriodMs = 60000UL; // 60 s default period

// Rolling accounting: track when the current ON-burst started and how much
// ON-time has already been consumed inside the current period window.
unsigned long dcPeriodStartMs  = 0;   // start of the current period window
uint32_t      dcOnTimeThisPeriod = 0; // ms the output has been ON so far this period
unsigned long dcOnStartMs      = 0;   // when the current ON-burst began (0 = not ON)
bool          dcForceOff       = false; // true when duty cap is exhausted for this period

// Called every time MOSFET state changes or at each control tick to update
// the rolling ON-time bookkeeping and enforce the duty cap.
// Returns the gated value: true = heater should be physically on, false = off.
bool dutyCycleGate(bool requested) {
  if (dutyCyclePct >= 100.0f) {
    // Feature disabled – pass through, no tracking needed.
    dcForceOff = false;
    return requested;
  }

  unsigned long now = millis();

  // Roll the period window forward when it expires.
  if ((now - dcPeriodStartMs) >= dutyCyclePeriodMs) {
    dcPeriodStartMs    = now;
    dcOnTimeThisPeriod = 0;
    dcForceOff         = false;
    dcOnStartMs        = 0;
  }

  // Accumulate ON-time: if the output is currently ON, add the elapsed time
  // since the last call into dcOnTimeThisPeriod.
  if (dcOnStartMs != 0) {
    uint32_t burst = (uint32_t)(now - dcOnStartMs);
    dcOnTimeThisPeriod += burst;
    dcOnStartMs = now; // reset reference so we don't double-count
  }

  // Compute how many ms are allowed in this period.
  uint32_t allowedMs = (uint32_t)((dutyCyclePct / 100.0f) * (float)dutyCyclePeriodMs);

  if (dcOnTimeThisPeriod >= allowedMs) {
    dcForceOff = true;
  }

  if (dcForceOff) {
    dcOnStartMs = 0;   // don't track further ON-time until next period
    return false;
  }

  if (requested) {
    if (dcOnStartMs == 0) dcOnStartMs = now; // start tracking this burst
    return true;
  } else {
    dcOnStartMs = 0;
    return false;
  }
}

// ─── Ramp support functions ───────────────────────────────────────────
float effectiveCoastRatio(float tempC) {
  float r = activeProfile.coastBase - activeProfile.coastSlope * (tempC / 100.0f);
  return max(r, 0.05f);
}

void refitCoastModel() {
  if (learnedCount < 2) return;
  float sumX=0, sumY=0, sumXY=0, sumX2=0;
  float n = (float)learnedCount;
  for (uint8_t i = 0; i < learnedCount; i++) {
    float x = learnedCutoffTemp[i];
    float y = learnedCoastRatio[i];
    sumX  += x; sumY  += y;
    sumXY += x * y; sumX2 += x * x;
  }
  float denom = n * sumX2 - sumX * sumX;
  if (fabsf(denom) < 1e-6f) return;
  float slopePerC = (n * sumXY - sumX * sumY) / denom;
  float intercept  = (sumY - slopePerC * sumX) / n;
  activeProfile.coastSlope = -slopePerC * 100.0f;
  activeProfile.coastBase  = intercept;
}

static const char* PROFILE_DIR = "/profiles";

String profileToJson(const RampProfile& p) {
  String steps = "[";
  for (uint8_t i = 0; i < p.stepCount; i++) {
    if (i) steps += ",";
    steps += String(p.stepTargets[i], 1);
  }
  steps += "]";
  return String("{\"name\":\"")        + p.name
       + "\",\"stepTargets\":"         + steps
       + ",\"stepCount\":"             + String(p.stepCount)
       + ",\"soakMinutes\":"           + String(p.soakMinutes, 2)
       + ",\"stabilityThreshC\":"      + String(p.stabilityThreshC, 2)
       + ",\"coastBase\":"             + String(p.coastBase, 6)
       + ",\"coastSlope\":"            + String(p.coastSlope, 6)
       + ",\"complete\":"               + String(p.complete ? "true" : "false")
       + "}";
}

bool profileFromJson(const String& json, RampProfile& p) {
  auto strVal = [&](const char* key) -> String {
    String k = String("\"") + key + "\":\"";
    int idx = json.indexOf(k);
    if (idx < 0) return "";
    idx += k.length();
    int end = json.indexOf("\"", idx);
    return (end > idx) ? json.substring(idx, end) : "";
  };
  auto numVal = [&](const char* key) -> float {
    String k = String("\"") + key + "\":";
    int idx = json.indexOf(k);
    if (idx < 0) return 0.0f;
    idx += k.length();
    while (idx < (int)json.length() && json[idx] == ' ') idx++;
    int end = idx;
    while (end < (int)json.length() && (isDigit(json[end]) || json[end]=='.' || json[end]=='-')) end++;
    return json.substring(idx, end).toFloat();
  };

  String name = strVal("name");
  if (!name.length()) return false;
  strncpy(p.name, name.c_str(), 31);
  p.name[31] = '\0';
  p.soakMinutes      = numVal("soakMinutes");
  p.stabilityThreshC = numVal("stabilityThreshC");
  p.coastBase        = numVal("coastBase");
  p.coastSlope       = numVal("coastSlope");
  p.stepCount        = (uint8_t)numVal("stepCount");
  p.stepCount        = constrain(p.stepCount, 1, RAMP_MAX_STEPS);

  {
    String k = "\"complete\":";
    int idx = json.indexOf(k);
    p.complete = (idx >= 0 && json.indexOf("true", idx + k.length()) == idx + k.length());
  }

  {
    int arr = json.indexOf("\"stepTargets\":");
    if (arr < 0) return false;
    arr += 14;
    while (arr < (int)json.length() && (json[arr] == ' ' || json[arr] == '\t')) arr++;
    if (arr >= (int)json.length() || json[arr] != '[') return false;
    arr++;
    uint8_t n = 0;
    while (n < p.stepCount && arr < (int)json.length() && json[arr] != ']') {
      while (arr < (int)json.length() && (json[arr] == ' ' || json[arr] == ',')) arr++;
      if (json[arr] == ']') break;
      int end = arr;
      while (end < (int)json.length() && (isDigit(json[end]) || json[end]=='.' || json[end]=='-')) end++;
      p.stepTargets[n++] = json.substring(arr, end).toFloat();
      arr = end;
    }
    if (n != p.stepCount) return false;
  }
  return true;
}

extern bool spiffsOk;

void ensureProfileDir() {
  if (!spiffsOk) return;
  if (!SPIFFS.exists(PROFILE_DIR)) {
    File f = SPIFFS.open(String(PROFILE_DIR) + "/.keep", FILE_WRITE);
    if (f) f.close();
  }
}

// ─── SPIFFS run log ───────────────────────────────────────────────────────────
static const char* LOG_PATH = "/runlog.csv";
bool     spiffsOk   = false;
bool     runActive  = false;
unsigned long runStartMs = 0;
File     runLogFile;

// Flush log at most once every FLUSH_INTERVAL_MS to avoid blocking the
// main loop on every sample.  A final flush is always done on closeRunLog().
#define LOG_FLUSH_INTERVAL_MS 5000UL
static unsigned long lastLogFlushMs = 0;

#define CACHE_SIZE 64
struct CacheSample {
  uint32_t t_s;
  float    tC;
  float    sp;
  uint8_t  output;
  uint8_t  mode;
  uint8_t  ramp_state;
  uint8_t  step_idx;
  float    coast_ratio;
};
CacheSample sampleCache[CACHE_SIZE];
uint16_t cacheHead  = 0;
uint16_t cacheCount = 0;

void openRunLog() {
  if (!spiffsOk) return;
  runLogFile = SPIFFS.open(LOG_PATH, FILE_WRITE);
  if (!runLogFile) { DBGLN("SPIFFS open failed"); return; }
  runLogFile.println("t_s,tempC,setpoint,output,mode,ramp_state,step_idx,coast_ratio_est");
  DBGLN("Run log opened");
}

void closeRunLog() {
  if (runLogFile) {
    runLogFile.flush();
    runLogFile.close();
  }
}

void appendRunLog(uint32_t t_s, float tC, float sp, uint8_t output,
                  uint8_t mode, uint8_t ramp_state, uint8_t step_idx, float coast_ratio) {
  sampleCache[cacheHead % CACHE_SIZE] = { t_s, tC, sp, output, mode, ramp_state, step_idx, coast_ratio };
  cacheHead++;
  if (cacheCount < CACHE_SIZE) cacheCount++;

  if (!spiffsOk || !runLogFile) return;
  runLogFile.print(t_s);          runLogFile.print(',');
  runLogFile.print(tC, 2);       runLogFile.print(',');
  runLogFile.print(sp, 1);       runLogFile.print(',');
  runLogFile.print(output);       runLogFile.print(',');
  runLogFile.print(mode);         runLogFile.print(',');
  runLogFile.print(ramp_state);   runLogFile.print(',');
  runLogFile.print(step_idx);     runLogFile.print(',');
  runLogFile.println(coast_ratio, 4);
  // Throttled flush: avoids a blocking SPIFFS write on every 500ms sample.
  unsigned long now = millis();
  if (now - lastLogFlushMs >= LOG_FLUSH_INTERVAL_MS) {
    runLogFile.flush();
    lastLogFlushMs = now;
  }
}

// ─── Globals ──────────────────────────────────────────────────────────────────
Preferences      prefs;
bool             prefsDirty = false;
unsigned long    prefsDirtyMs = 0;
WebServer        server(80);
DNSServer        dns;
MAX6675          thermocouple(MAX_SCK, MAX_CS, MAX_SO);
Adafruit_SSD1306 display(OLED_W, OLED_H, &Wire, -1);

float    setpoint    = 500.0f;
float    currentTemp =   0.0f;
float    lastGoodTemp   = NAN;
bool     outputOn       = false;
bool     apMode         = false;

float    probeOffset     =  0.0f;
float    hysteresis      =  5.0f;
String   savedSSID       = MYSSID;
String   savedPSK        = MYPSK;

// ─── Web auth ─────────────────────────────────────────────────────────────────
String webUser = "admin";
String webPass = "thermostat";

#define HIST_SIZE 720
float    tempHistory[HIST_SIZE];
uint16_t histHead  = 0;
uint16_t histCount = 0;

unsigned long lastReport        = 0;
unsigned long lastWifiRetry     = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long bootTime          = 0;
bool otaStarted    = false;
bool serverStarted = false;

// ─── Ramp stability helpers ───────────────────────────────────────────
// FIX: resetStabilityBuf() now zeroes the buffer contents so that no
//      stale temperature values bleed into fresh stability checks.
void resetStabilityBuf() {
  stabilityIdx    = 0;
  stabilityFilled = 0;
  memset(stabilityBuf, 0, sizeof(stabilityBuf));
}

// isStable() appends currentTemp into the circular buffer, then returns
// true only once STABILITY_WINDOW samples have been collected AND the
// spread (max-min) across all those samples is within stabilityThreshC.
bool isStable() {
  stabilityBuf[stabilityIdx % STABILITY_WINDOW] = currentTemp;
  stabilityIdx++;
  if (stabilityFilled < STABILITY_WINDOW) stabilityFilled++;
  if (stabilityFilled < STABILITY_WINDOW) return false;
  float mn = stabilityBuf[0], mx = stabilityBuf[0];
  for (uint16_t i = 1; i < STABILITY_WINDOW; i++) {
    if (stabilityBuf[i] < mn) mn = stabilityBuf[i];
    if (stabilityBuf[i] > mx) mx = stabilityBuf[i];
  }
  return (mx - mn) < activeProfile.stabilityThreshC;
}

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

// ─── Heater gatekeeper ─────────────────────────────────────────────────────────
// FIX: applyHeater() now runs the duty-cycle gate before writing to the MOSFET.
//      The duty cycle is enforced here (single chokepoint) so all code paths
//      (manual, bang-bang, ramp) are equally protected.
void applyHeater(bool requestOn) {
  heatRequested = requestOn;
  if (stopLatched || !requestOn) {
    dutyCycleGate(false);
    outputOn = false;
    MOSFET_WRITE(false);
  } else {
    bool gated = dutyCycleGate(true);
    outputOn = gated;
    MOSFET_WRITE(gated);
    if (!gated) {
      DBG("DC limit: output suppressed (");
      DBG(dcOnTimeThisPeriod);
      DBGLN("ms used this period)");
    }
  }
}

// ─── Forward declarations ─────────────────────────────────────────────────────
void loadPrefs(); void savePrefs();
void startSTA();  void startAP(); void onWifiConnect();
void setupOTA();  void setupRoutes();
float rawTempC(); float medianOf(float* arr, uint8_t n);
void applyHeater(bool);
void controlLoop();
void rampControlLoop();
void updateButtons(); void updateDisplay();
void openRunLog();
void appendRunLog(uint32_t, float, float, uint8_t, uint8_t, uint8_t, uint8_t, float);

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_MOSFET, OUTPUT);
  applyHeater(false);
  dcPeriodStartMs = millis();

  uint8_t btnPins[] = { PIN_BTN_UP, PIN_BTN_DN, PIN_BTN_CTR };
  for (int i = 0; i < 3; i++) {
    btns[i] = { btnPins[i], BTN_IDLE, 0, 0, (float)RAMP_RATE_INITIAL_MS, SP_STEP_INITIAL };
    pinMode(btnPins[i], INPUT_PULLUP);
  }

  Wire.begin(PIN_SDA, PIN_SCL);

  delay(500);
  DBGLN("MAX6675 ready");

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

  spiffsOk = SPIFFS.begin(true);
  if (spiffsOk) { DBGLN("SPIFFS ready"); ensureProfileDir(); }
  else          { DBGLN("SPIFFS failed - logging to RAM cache only"); }

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
  if (apMode) dns.processNextRequest();
  updateButtons();
  if (otaStarted)   ArduinoOTA.handle();
  if (serverStarted) server.handleClient();

  unsigned long now = millis();

  if (now - lastFastSample >= FAST_SAMPLE_MS) {
    lastFastSample = now;
    medBuf[medCount % MEDIAN_N] = rawTempC();
    if (medCount < MEDIAN_N) medCount++;
  }

  if (now - lastReport >= REPORT_MS) {
    lastReport = now;
    if (medCount > 0) {
      currentTemp = medianOf(medBuf, medCount);
    }
    DBG("T: "); DBGLN(currentTemp);

    tempHistory[histHead] = currentTemp;
    histHead = (histHead + 1) % HIST_SIZE;
    if (histCount < HIST_SIZE) histCount++;

    medCount = 0;

    if (!stopLatched && modeRunning) {
      if (selectedMode == MODE_AUTO_RAMP && runActive) {
        rampControlLoop();
      } else if (selectedMode == MODE_BANG_BANG) {
        controlLoop();
      }
    }

    // Periodically re-check the duty-cycle gate so it can cut the output
    // mid-heating even when the control loop doesn't call applyHeater().
    // When the period resets and we still want heat, re-enable the output.
    if (heatRequested && !stopLatched) {
      bool gated = dutyCycleGate(true);
      if (gated && !outputOn) {
        outputOn = true;
        MOSFET_WRITE(true);
        DBG("DC period reset: output restored\n");
      } else if (!gated && outputOn) {
        outputOn = false;
        MOSFET_WRITE(false);
        DBG("DC limit (loop): output cut (");
        DBG(dcOnTimeThisPeriod);
        DBGLN("ms used)");
      }
    } else if (!heatRequested) {
      dutyCycleGate(false);
    }

    if (runActive) {
      uint32_t t_s = (uint32_t)((now - runStartMs) / 1000UL);
      float coastEst = (learnedCount > 0)
        ? effectiveCoastRatio(currentTemp)
        : activeProfile.coastBase;
      appendRunLog(t_s, currentTemp, setpoint, outputOn ? 1 : 0,
                   (uint8_t)selectedMode, (uint8_t)rampState, rampStep, coastEst);
    }
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

  if (now - lastDisplayUpdate >= DISPLAY_MS) {
    lastDisplayUpdate = now;
    if (now - bootTime >= IP_SPLASH_MS) updateDisplay();
  }

  if (prefsDirty && (millis() - prefsDirtyMs) > 2000UL) {
    savePrefs();
    prefsDirty = false;
  }
}

// ─── Display ──────────────────────────────────────────────────────────────────
void updateDisplay() {
  unsigned long now = millis();
  display.clearDisplay();

  bool heaterActive = outputOn && !stopLatched;

  if (now < modeOverlayUntil) {
    display.setRotation(0);
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    const char* modeNames[] = { "MANUAL", "BANG-BANG", "AUTO RAMP" };
    uint8_t len = strlen(modeNames[selectedMode]);
    display.setCursor(max(0, (128 - (int)len * 12) / 2), 8);
    display.print(modeNames[selectedMode]);
    display.display();
    return;
  }

  if (stopLatched) {
    display.invertDisplay(true);
  } else {
    display.invertDisplay(false);
  }

  display.setRotation(3);
  if (heaterActive) {
    display.fillRect(0, 0, 32, 14, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  } else {
    display.setTextColor(SSD1306_WHITE);
  }
  display.setTextSize(1);
  const char* modeLabel[] = { "MAN", "BANG", "RAMP" };
  display.setCursor(4, 3);
  display.print(modeLabel[selectedMode]);

  display.setRotation(0);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(3);
  String tempStr = String((int)round(currentTemp));
  int tempX = 16 + max(0, (66 - (int)tempStr.length() * 18));
  display.setCursor(tempX, 4);
  display.print(tempStr);

  display.setTextSize(2);
  String spStr = String((int)round(setpoint));
  int spX = 128 - (int)spStr.length() * 12;
  display.setCursor(spX, 16);
  display.print(spStr);

  display.setTextSize(1);
  const char* substate = "OFF";
  if (modeRunning) {
    if (selectedMode == MODE_MANUAL) {
      substate = outputOn ? "ON" : "OFF";
    } else if (selectedMode == MODE_BANG_BANG) {
      substate = outputOn ? "ON" : "OFF";
    } else {
      const char* rampLabels[] = { "IDLE","HEAT","COAST","SOAK","OVR","FSOAK","DONE" };
      substate = rampLabels[constrain((int)rampState, 0, 6)];
    }
  }
  int ssX = 128 - (int)strlen(substate) * 6;
  display.setCursor(ssX, 8);
  display.print(substate);

  display.display();
}

// ─── Button handling ──────────────────────────────────────────────────────────
void updateButtons() {
  unsigned long now = millis();

  // Register a center-button short press toward the e-stop counter.
  // Must be called on every short press regardless of mode/latch state.
  auto registerEstopPress = [&]() {
    if (now - estopWindowStart > ESTOP_WINDOW_MS) {
      estopPressCount  = 1;
      estopWindowStart = now;
    } else {
      estopPressCount++;
    }
    DBG("E-stop press count: "); DBGLN(estopPressCount);
    if (estopPressCount >= ESTOP_PRESSES) {
      estopPressCount = 0;
      if (stopLatched) {
        stopLatched = false;
        DBGLN("E-stop released");
      } else {
        stopLatched  = true;
        modeRunning  = false;
        runActive    = false;
        applyHeater(false);
        closeRunLog();
        DBGLN("E-stop LATCHED");
      }
    }
  };

  for (int i = 0; i < 3; i++) {
    bool low = !digitalRead(btns[i].pin);
    switch (btns[i].phase) {

      case BTN_IDLE:
        if (low) { btns[i].phase = BTN_PENDING; btns[i].pendingSince = now; }
        break;

      case BTN_PENDING:
        if (!low) {
          // Button released — short press confirmed
          btns[i].phase = BTN_IDLE;
          if (i == 2) {
            registerEstopPress();
            if (estopPressCount == 0) break;

            // Normal (non-latched) short press action
            if (!stopLatched) {
              if (selectedMode == MODE_MANUAL) {
                // ── FIX: in manual mode, toggle heater on/off ──
                if (modeRunning) {
                  applyHeater(!outputOn);
                  DBG("Manual heater toggled: "); DBGLN(outputOn ? "ON" : "OFF");
                } else {
                  // First press starts manual mode with heater off
                  modeRunning = true;
                  applyHeater(false);
                  DBGLN("Manual mode started");
                }
              } else if (modeRunning) {
                // Bang-bang or ramp: short press stops the run
                modeRunning = false;
                applyHeater(false);
                runActive   = false;
                closeRunLog();
                DBGLN("Mode stopped");
              } else {
                // Bang-bang or ramp: short press starts the run
                modeRunning = true;
                if (selectedMode == MODE_AUTO_RAMP) {
                  rampState        = RS_IDLE;
                  rampStep         = 0;
                  rampOvershootAmt = 0.0f;
                  learnedCount     = 0;
                  finalSoakStartMs = 0;
                  resetStabilityBuf();
                  coastingDropCount = 0;
                  memset(learnedFireStartTemp, 0, sizeof(learnedFireStartTemp));
                  memset(learnedCutoffTemp,    0, sizeof(learnedCutoffTemp));
                  memset(learnedPeakTemp,      0, sizeof(learnedPeakTemp));
                  memset(learnedCoastRatio,    0, sizeof(learnedCoastRatio));
                  runActive  = true;
                  runStartMs = now;
                  cacheHead  = 0;
                  cacheCount = 0;
                  openRunLog();
                } else if (selectedMode == MODE_BANG_BANG) {
                  runActive  = true;
                  runStartMs = now;
                  cacheHead  = 0;
                  cacheCount = 0;
                  openRunLog();
                }
                DBGLN("Mode started");
              }
            }
          }
        } else if (now - btns[i].pendingSince >= (i == 2 ? RAMP_DELAY_MS : DEBOUNCE_MS)) {
          btns[i].phase    = BTN_HELD;
          btns[i].nextFire = now + (i == 2 ? CTR_LONGPRESS_MS : RAMP_DELAY_MS);
          btns[i].currentInterval = RAMP_RATE_INITIAL_MS;
          btns[i].currentStep     = SP_STEP_INITIAL;
          if (i == 0) { setpoint = min(setpoint + SP_STEP_INITIAL, 1200.0f); prefsDirty = true; prefsDirtyMs = now; }
          if (i == 1) { setpoint = max(setpoint - SP_STEP_INITIAL,    0.0f); prefsDirty = true; prefsDirtyMs = now; }
        }
        break;

      case BTN_HELD:
        if (!low) {
          btns[i].phase = BTN_IDLE;
          btns[i].currentInterval = RAMP_RATE_INITIAL_MS;
          btns[i].currentStep     = SP_STEP_INITIAL;
          if (prefsDirty) { savePrefs(); prefsDirty = false; }
        } else if (i == 2 && now >= btns[i].nextFire) {
          btns[i].nextFire = ULONG_MAX;
          modeRunning = false;
          applyHeater(false);
          runActive = false;
          closeRunLog();
          selectedMode = (RunMode)((selectedMode + 1) % 3);
          modeOverlayUntil = now + MODE_OVERLAY_MS;
          DBG("Mode selected: "); DBGLN(selectedMode);
        }
        if (i != 2) {
          if (now >= btns[i].nextFire) {
            if (i == 0) setpoint = min(setpoint + btns[i].currentStep, 1200.0f);
            if (i == 1) setpoint = max(setpoint - btns[i].currentStep,    0.0f);
            btns[i].currentInterval = max((float)RAMP_RATE_MIN_MS, btns[i].currentInterval * RAMP_ACCEL);
            btns[i].currentStep     = min(SP_STEP_MAX, btns[i].currentStep * SP_STEP_ACCEL);
            btns[i].nextFire        = now + (unsigned long)btns[i].currentInterval;
            prefsDirty = true; prefsDirtyMs = now;
          }
        }
        break;
    }
  }
}

// ─── Temperature ──────────────────────────────────────────────────────────────
float rawTempC() {
  float candidate = thermocouple.readCelsius();

  if (isnan(candidate) || candidate == 0.0f) {
    DBGLN("MAX6675 read fault");
    return isnan(lastGoodTemp) ? 20.0f : lastGoodTemp;
  }

  candidate += probeOffset;

  if (!isnan(lastGoodTemp) && fabsf(candidate - lastGoodTemp) > OUTLIER_MAX_JUMP) {
    DBG("Jump reject: "); DBGLN(candidate);
    jumpRejectCount++;
    if (jumpRejectCount >= JUMP_REJECT_MAX) {
      lastGoodTemp = NAN;
      jumpRejectCount = 0;
      DBGLN("Jump lock cleared");
    }
    return isnan(lastGoodTemp) ? candidate : lastGoodTemp;
  }

  jumpRejectCount = 0;
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
  if      (currentTemp < setpoint - hysteresis) applyHeater(true);
  else if (currentTemp > setpoint + hysteresis) applyHeater(false);
}

// ─── Ramp control loop ───────────────────────────────────────────────────────
void rampControlLoop() {
  if (rampStep >= activeProfile.stepCount) {
    rampStep = activeProfile.stepCount > 0 ? activeProfile.stepCount - 1 : 0;
  }

  bool  isFinalStep = (rampStep == activeProfile.stepCount - 1);
  float stepTarget  = activeProfile.stepTargets[rampStep];

  switch (rampState) {

    case RS_IDLE:
      rampStep           = 0;
      setpoint           = activeProfile.stepTargets[0];
      rampState          = RS_HEATING;
      rampStateEnteredMs = millis();
      rampFireStartTemp  = currentTemp;
      resetStabilityBuf();
      coastingDropCount  = 0;
      applyHeater(true);
      DBGLN("Ramp: HEATING step 0");
      return;

    case RS_HEATING: {
      float rise = currentTemp - rampFireStartTemp;
      if (rise > 1.0f) {
        float ratio = effectiveCoastRatio(stepTarget);
        float predictedPeak = currentTemp + rise * ratio;
        if (predictedPeak >= stepTarget) {
          applyHeater(false);
          rampCutoffTemp = currentTemp;
          rampPeakTemp   = currentTemp;
          rampState      = RS_COASTING;
          setpoint = stepTarget;
          rampStateEnteredMs = millis();
          resetStabilityBuf();
          coastingDropCount = 0;
          DBG("Ramp: COASTING, cutoff="); DBGLN(rampCutoffTemp);
          break;
        }
      }

        if (currentTemp >= stepTarget) {
          applyHeater(false);
          rampCutoffTemp = currentTemp;
          rampPeakTemp   = currentTemp;
          rampOvershootAmt = 0.0f;
          rampState = isFinalStep ? RS_FINAL_SOAK : RS_SOAKING;
          setpoint = stepTarget;
          if (rampState == RS_FINAL_SOAK) finalSoakStartMs = millis();
          rampStateEnteredMs = millis();
          resetStabilityBuf();
          coastingDropCount = 0;
          DBG("Ramp: hard cutoff (already past target), skip coast for step "); DBGLN(rampStep);
          break;
        }
      break;
    }

    case RS_COASTING: {
      if (currentTemp > rampPeakTemp) {
        rampPeakTemp      = currentTemp;
        coastingDropCount = 0;
      }

      if (rampPeakTemp - currentTemp > 2.0f) {
        coastingDropCount++;
      } else {
        coastingDropCount = 0;
      }

      bool coastTimeout = (millis() - rampStateEnteredMs) > 90000UL;

      if (coastingDropCount >= 3 || coastTimeout) {
        if (coastTimeout && coastingDropCount < 3) {
          DBG("Ramp: COASTING timeout, forcing peak="); DBGLN(rampPeakTemp);
        }
        coastingDropCount = 0;
        float rise = rampCutoffTemp - rampFireStartTemp;

        if (rise > 1.0f && learnedCount < MAX_LEARNED_STEPS) {
          float coast = (rampPeakTemp - rampCutoffTemp) / rise;
          learnedFireStartTemp[learnedCount] = rampFireStartTemp;
          learnedCutoffTemp[learnedCount]    = rampCutoffTemp;
          learnedPeakTemp[learnedCount]      = rampPeakTemp;
          learnedCoastRatio[learnedCount]    = coast;
          learnedCount++;
          refitCoastModel();
        } else {
          DBG("Ramp: skipping coast record, rise too small: "); DBGLN(rise);
        }

        // FIX: record overshoot AFTER peak is confirmed (coastingDropCount >= 3)
        // so the full actual peak is captured, not an early sample.
        rampOvershootAmt = max(0.0f, rampPeakTemp - stepTarget);
        DBG("Ramp: peak="); DBG(rampPeakTemp);
        DBG(" overshoot="); DBGLN(rampOvershootAmt);

        resetStabilityBuf();
        rampState = (rampOvershootAmt > 10.0f) ? RS_OVERSHOOT_WAIT : RS_SOAKING;
        rampStateEnteredMs = millis();
      }
      break;
    }

    case RS_SOAKING:
    case RS_OVERSHOOT_WAIT: {
      if (currentTemp < stepTarget - 3.0f) { applyHeater(true);  }
      if (currentTemp > stepTarget + 3.0f) { applyHeater(false); }

      if (rampState == RS_OVERSHOOT_WAIT) {
        // Once the temp has plateaued and is stable (falling or holding),
        // advance to the next step — no need to wait for it to cool back
        // down to within 5°C of the target.
        if (isStable()) {
          if (isFinalStep) {
            rampState        = RS_FINAL_SOAK;
            setpoint         = stepTarget;
            finalSoakStartMs = millis();
            rampStateEnteredMs = millis();
            resetStabilityBuf();
            DBGLN("Ramp: FINAL_SOAK started");
          } else {
            rampStep++;
            setpoint          = activeProfile.stepTargets[rampStep];
            rampState         = RS_HEATING;
            rampFireStartTemp = currentTemp;
            rampStateEnteredMs = millis();
            resetStabilityBuf();
            coastingDropCount = 0;
            applyHeater(true);
            DBG("Ramp: HEATING step "); DBGLN(rampStep);
          }
        }
      } else {
        if (isStable()) {
          if (isFinalStep) {
            rampState        = RS_FINAL_SOAK;
            setpoint         = stepTarget;
            finalSoakStartMs = millis();
            rampStateEnteredMs = millis();
            resetStabilityBuf();
            DBGLN("Ramp: FINAL_SOAK started");
          } else {
            rampStep++;
            setpoint          = activeProfile.stepTargets[rampStep];
            rampState         = RS_HEATING;
            rampFireStartTemp = currentTemp;
            rampStateEnteredMs = millis();
            resetStabilityBuf();
            coastingDropCount = 0;
            applyHeater(true);
            DBG("Ramp: HEATING step "); DBGLN(rampStep);
          }
        }
      }
      break;
    }

    case RS_FINAL_SOAK: {
      if (currentTemp < stepTarget - 3.0f) { applyHeater(true);  }
      if (currentTemp > stepTarget + 3.0f) { applyHeater(false); }

      if ((millis() - finalSoakStartMs) >= (unsigned long)(activeProfile.soakMinutes * 60000.0f)) {
        applyHeater(false);
        activeProfile.complete = true;
        savePrefs();
        rampState  = RS_DONE;
        runActive  = false;
        modeRunning = false;
        rampStateEnteredMs = millis();
        closeRunLog();
        DBGLN("Ramp: DONE");
        DBGLN("Profile saved after completed run");
      }
      break;
    }

    case RS_DONE:
      applyHeater(false);
      break;
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
  if (MDNS.begin(HOSTNAME)) DBGLN(String("mDNS: ") + HOSTNAME + ".local");
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
  savedSSID     = prefs.getString("ssid", MYSSID);
  savedPSK      = prefs.getString("psk",  MYPSK);
  webUser = prefs.getString("wuser", "admin");
  webPass = prefs.getString("wpass", "thermostat");
  dutyCyclePct      = prefs.getFloat("dc_pct",    100.0f);
  dutyCyclePeriodMs = prefs.getUInt ("dc_period",  60000UL);
  dutyCyclePct      = constrain(dutyCyclePct, 1.0f, 100.0f);
  dutyCyclePeriodMs = constrain((uint32_t)dutyCyclePeriodMs, 1000UL, 3600000UL);
  strncpy(activeProfile.name, prefs.getString("rp_name", "default").c_str(), 31);
  activeProfile.name[31] = '\0';
  activeProfile.soakMinutes      = prefs.getFloat("rp_soak",      30.0f);
  activeProfile.stabilityThreshC = prefs.getFloat("rp_stab",       2.0f);
  activeProfile.coastBase        = prefs.getFloat("rp_cbase",     0.40f);
  activeProfile.coastSlope       = prefs.getFloat("rp_cslope",    0.03f);
  activeProfile.complete         = prefs.getBool ("rp_complete",  false);
  activeProfile.stepCount        = (uint8_t)prefs.getUInt("rp_stepct", 7);
  activeProfile.stepCount        = min(activeProfile.stepCount, (uint8_t)RAMP_MAX_STEPS);
  {
    size_t len = prefs.getBytesLength("rp_steps");
    if (len == activeProfile.stepCount * sizeof(float)) {
      prefs.getBytes("rp_steps", activeProfile.stepTargets,
                     activeProfile.stepCount * sizeof(float));
    }
  }
  prefs.end();
}

void savePrefs() {
  prefs.begin("therm", false);
  prefs.putFloat ("sp",    setpoint);
  prefs.putFloat ("hyst",  hysteresis);
  prefs.putFloat ("off",   probeOffset);
  prefs.putString("ssid",  savedSSID);
  prefs.putString("psk",   savedPSK);
  prefs.putString("wuser", webUser);
  prefs.putString("wpass", webPass);
  prefs.putFloat ("dc_pct",    dutyCyclePct);
  prefs.putUInt  ("dc_period", dutyCyclePeriodMs);
  prefs.putString("rp_name",     activeProfile.name);
  prefs.putFloat ("rp_soak",     activeProfile.soakMinutes);
  prefs.putFloat ("rp_stab",     activeProfile.stabilityThreshC);
  prefs.putFloat ("rp_cbase",    activeProfile.coastBase);
  prefs.putFloat ("rp_cslope",   activeProfile.coastSlope);
  prefs.putBool  ("rp_complete", activeProfile.complete);
  prefs.putUInt  ("rp_stepct",   activeProfile.stepCount);
  prefs.putBytes ("rp_steps",    activeProfile.stepTargets,
                  activeProfile.stepCount * sizeof(float));
  prefs.end();
}

bool requireAuth() {
  if (server.authenticate(webUser.c_str(), webPass.c_str())) return true;
  server.requestAuthentication(BASIC_AUTH, "Thermostat",
    "Authentication required");
  return false;
}

// ─── Web routes ───────────────────────────────────────────────────────────────
void setupRoutes() {
  server.onNotFound([]() {
    String host = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    server.sendHeader("Location", "http://" + host + "/");
    server.send(302);
  });

  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", HTML_INDEX);
  });

  server.on("/status", HTTP_GET, []() {
    unsigned long periodElapsedMs = millis() - dcPeriodStartMs;
    uint32_t dcAllowedMs = (uint32_t)((dutyCyclePct / 100.0f) * (float)dutyCyclePeriodMs);
    uint32_t dcRemainingMs = (dcAllowedMs > dcOnTimeThisPeriod)
                           ? (dcAllowedMs - dcOnTimeThisPeriod) : 0;
    String j = "{\"temp\":"         + String(currentTemp,  1)
             + ",\"setpoint\":"     + String(setpoint,      1)
             + ",\"output\":"       + String(outputOn ? 1 : 0)
             + ",\"selectedMode\":" + String((int)selectedMode)
             + ",\"modeRunning\":"  + String(modeRunning ? 1 : 0)
             + ",\"stopLatched\":"  + String(stopLatched ? 1 : 0)
             + ",\"runActive\":"    + String(runActive ? 1 : 0)
             + ",\"runElapsed\":"   + String(runActive ? (millis()-runStartMs)/1000UL : 0UL)
             + ",\"hysteresis\":"   + String(hysteresis,    1)
             + ",\"offset\":"       + String(probeOffset,   1)
             + ",\"dcPct\":"        + String(dutyCyclePct,  1)
             + ",\"dcPeriodMs\":"   + String(dutyCyclePeriodMs)
             + ",\"dcOnTimeMs\":"   + String(dcOnTimeThisPeriod)
             + ",\"dcRemainingMs\":" + String(dcRemainingMs)
             + ",\"dcForceOff\":"   + String(dcForceOff ? 1 : 0)
             + "}";
    server.send(200, "application/json", j);
  });

  server.on("/pinstatus", HTTP_GET, []() {
    bool btnUp  = !digitalRead(PIN_BTN_UP);
    bool btnDn  = !digitalRead(PIN_BTN_DN);
    bool btnCtr = !digitalRead(PIN_BTN_CTR);
    bool mosfetPin = digitalRead(PIN_MOSFET);
    const char* phaseNames[] = {"IDLE","PENDING","HELD"};
    String j = String("{")
      + "\"btnUp\":"         + String(btnUp    ? 1 : 0)
      + ",\"btnDn\":"        + String(btnDn    ? 1 : 0)
      + ",\"btnCtr\":"       + String(btnCtr   ? 1 : 0)
      + ",\"mosfet\":"       + String(mosfetPin? 1 : 0)
      + ",\"outputOn\":"     + String(outputOn     ? 1 : 0)
      + ",\"stopLatched\":"  + String(stopLatched  ? 1 : 0)
      + ",\"modeRunning\":"  + String(modeRunning  ? 1 : 0)
      + ",\"heatRequested\":" + String(heatRequested ? 1 : 0)
      + ",\"btnUpPhase\":\""  + phaseNames[btns[0].phase] + "\""
      + ",\"btnDnPhase\":\""  + phaseNames[btns[1].phase] + "\""
      + ",\"btnCtrPhase\":\"" + phaseNames[btns[2].phase] + "\""
      + ",\"estopCount\":"   + String(estopPressCount)
      + ",\"pinMosfet\":"    + String(PIN_MOSFET)
      + ",\"pinBtnUp\":"     + String(PIN_BTN_UP)
      + ",\"pinBtnDn\":"     + String(PIN_BTN_DN)
      + ",\"pinBtnCtr\":"    + String(PIN_BTN_CTR)
      + ",\"pinSDA\":"       + String(PIN_SDA)
      + ",\"pinSCL\":"       + String(PIN_SCL)
      + ",\"maxSCK\":"       + String(MAX_SCK)
      + ",\"maxCS\":"        + String(MAX_CS)
      + ",\"maxSO\":"        + String(MAX_SO)
      + "}";
    server.send(200, "application/json", j);
  });

  server.on("/rampstatus", HTTP_GET, []() {
    uint8_t safeStep = min(rampStep, (uint8_t)(activeProfile.stepCount - 1));

    String learned = "[";
    for (uint8_t i = 0; i < learnedCount; i++) {
      if (i) learned += ",";
      learned += "{\"step\":"        + String(i)
               + ",\"target\":"      + String(activeProfile.stepTargets[i], 1)
               + ",\"fireStart\":"   + String(learnedFireStartTemp[i], 1)
               + ",\"cutoff\":"      + String(learnedCutoffTemp[i],    1)
               + ",\"peak\":"        + String(learnedPeakTemp[i],      1)
               + ",\"coastRatio\":"  + String(learnedCoastRatio[i],   4)
               + "}";
    }
    learned += "]";

    float rise = currentTemp - rampFireStartTemp;
    float predPeak = (rampState == RS_HEATING)
      ? currentTemp + rise * effectiveCoastRatio(activeProfile.stepTargets[safeStep])
      : 0.0f;

    unsigned long stateAgeMs = millis() - rampStateEnteredMs;
    long soakRemain = 0;
    if (rampState == RS_FINAL_SOAK) {
      long total = (long)(activeProfile.soakMinutes * 60.0f);
      soakRemain = total - (long)((millis() - finalSoakStartMs) / 1000UL);
      if (soakRemain < 0) soakRemain = 0;
    }

    String j = "{\"rampState\":" + String((int)rampState)
             + ",\"rampStep\":" + String(rampStep)
             + ",\"stepCount\":" + String(activeProfile.stepCount)
             + ",\"stepTarget\":" + String(activeProfile.stepTargets[safeStep], 1)
             + ",\"finalTarget\":" + String(activeProfile.stepTargets[activeProfile.stepCount-1], 1)
             + ",\"predictedPeak\":" + String(predPeak, 1)
             + ",\"overshootAmt\":" + String(rampOvershootAmt, 1)
             + ",\"coastBase\":" + String(activeProfile.coastBase, 4)
             + ",\"coastSlope\":" + String(activeProfile.coastSlope, 4)
             + ",\"stateAgeMs\":" + String(stateAgeMs)
             + ",\"soakRemainS\":" + String(soakRemain)
             + ",\"learnedCount\":" + String(learnedCount)
             + ",\"learned\":" + learned
             + "}";
    server.send(200, "application/json", j);
  });

  server.on("/profile", HTTP_GET, []() {
    String steps = "[";
    for (uint8_t i = 0; i < activeProfile.stepCount; i++) {
      if (i) steps += ",";
      steps += String(activeProfile.stepTargets[i], 1);
    }
    steps += "]";
    String j = "{\"name\":\"" + String(activeProfile.name) + "\""
             + ",\"stepTargets\":" + steps
             + ",\"stepCount\":" + String(activeProfile.stepCount)
             + ",\"soakMinutes\":" + String(activeProfile.soakMinutes, 1)
             + ",\"stabilityThresh\":" + String(activeProfile.stabilityThreshC, 1)
             + ",\"coastBase\":" + String(activeProfile.coastBase, 4)
             + ",\"coastSlope\":" + String(activeProfile.coastSlope, 4)
             + ",\"complete\":" + String(activeProfile.complete ? 1 : 0)
             + "}";
    server.send(200, "application/json", j);
  });

  server.on("/profile", HTTP_POST, []() {
    if (!requireAuth()) return;
    if (server.hasArg("name")) {
      strncpy(activeProfile.name, server.arg("name").c_str(), 31);
      activeProfile.name[31] = '\0';
    }
    if (server.hasArg("soakMin")) activeProfile.soakMinutes = server.arg("soakMin").toFloat();
    if (server.hasArg("stability")) activeProfile.stabilityThreshC = server.arg("stability").toFloat();
    if (server.hasArg("coastBase")) activeProfile.coastBase = server.arg("coastBase").toFloat();
    if (server.hasArg("coastSlope")) activeProfile.coastSlope = server.arg("coastSlope").toFloat();
    if (server.hasArg("steps")) {
      String s = server.arg("steps");
      uint8_t n = 0;
      int start = 0, comma;
      while ((comma = s.indexOf(',', start)) != -1 && n < RAMP_MAX_STEPS) {
        activeProfile.stepTargets[n++] = s.substring(start, comma).toFloat();
        start = comma + 1;
      }
      if (start < (int)s.length() && n < RAMP_MAX_STEPS)
        activeProfile.stepTargets[n++] = s.substring(start).toFloat();
      activeProfile.stepCount = n;
    }
    savePrefs();
    server.send(200, "text/plain", "OK");
  });

  server.on("/profiles", HTTP_GET, []() {
    if (!spiffsOk) { server.send(503, "text/plain", "SPIFFS unavailable"); return; }
    String json = "[";
    bool first  = true;
    File root   = SPIFFS.open(PROFILE_DIR);
    if (root && root.isDirectory()) {
      File f = root.openNextFile();
      while (f) {
        String fname = String(f.name());
        if (fname.endsWith(".json")) {
          int slash = fname.lastIndexOf('/');
          String pname = fname.substring(slash + 1, fname.length() - 5);
          if (!first) json += ",";
          json += "\"" + pname + "\"";
          first = false;
        }
        f.close();
        f = root.openNextFile();
      }
      root.close();
    }
    json += "]";
    server.send(200, "application/json", json);
  });

  server.on("/profiles/save", HTTP_POST, []() {
    if (!requireAuth()) return;
    if (!spiffsOk) { server.send(503, "text/plain", "SPIFFS unavailable"); return; }
    if (server.hasArg("name") && server.arg("name").length() > 0) {
      String n = server.arg("name");
      n.trim();
      for (int i = 0; i < (int)n.length(); i++) {
        char c = n[i];
        if (!isAlphaNumeric(c) && c != '-' && c != '_') { n.setCharAt(i, '_'); }
      }
      strncpy(activeProfile.name, n.c_str(), 31);
      activeProfile.name[31] = '\0';
    }
    String path = String(PROFILE_DIR) + "/" + activeProfile.name + ".json";
    File f = SPIFFS.open(path, FILE_WRITE);
    if (!f) { server.send(500, "text/plain", "Failed to open file"); return; }
    f.print(profileToJson(activeProfile));
    f.close();
    savePrefs();
    DBG("Profile saved to SPIFFS: "); DBGLN(path);
    server.send(200, "application/json",
      "{\"saved\":\"" + String(activeProfile.name) + "\"}");
  });

  server.on("/profiles/load", HTTP_POST, []() {
    if (!requireAuth()) return;
    if (!spiffsOk) { server.send(503, "text/plain", "SPIFFS unavailable"); return; }
    if (!server.hasArg("name") || !server.arg("name").length()) {
      server.send(400, "text/plain", "Missing name"); return;
    }
    String n = server.arg("name");
    n.trim();
    String path = String(PROFILE_DIR) + "/" + n + ".json";
    if (!SPIFFS.exists(path)) {
      server.send(404, "text/plain", "Profile not found"); return;
    }
    File f = SPIFFS.open(path, FILE_READ);
    if (!f) { server.send(500, "text/plain", "Failed to open file"); return; }
    String json = f.readString();
    f.close();
    RampProfile tmp;
    if (!profileFromJson(json, tmp)) {
      server.send(500, "text/plain", "Parse error"); return;
    }
    activeProfile = tmp;
    savePrefs();
    DBG("Profile loaded from SPIFFS: "); DBGLN(path);
    server.send(200, "application/json", profileToJson(activeProfile));
  });

  server.on("/profiles/delete", HTTP_POST, []() {
    if (!requireAuth()) return;
    if (!spiffsOk) { server.send(503, "text/plain", "SPIFFS unavailable"); return; }
    if (!server.hasArg("name") || !server.arg("name").length()) {
      server.send(400, "text/plain", "Missing name"); return;
    }
    String n    = server.arg("name");
    n.trim();
    String path = String(PROFILE_DIR) + "/" + n + ".json";
    if (!SPIFFS.exists(path)) {
      server.send(404, "text/plain", "Not found"); return;
    }
    SPIFFS.remove(path);
    DBG("Profile deleted: "); DBGLN(path);
    server.send(200, "application/json", "{\"deleted\":\"" + n + "\"}");
  });

  server.on("/run", HTTP_POST, []() {
    if (!requireAuth()) return;
    String modeStr   = server.arg("mode");
    String actionStr = server.arg("action");

    if (actionStr == "start") {
      if (stopLatched) { server.send(403, "text/plain", "E-stop latched"); return; }
      selectedMode = (modeStr == "autoramp") ? MODE_AUTO_RAMP
                   : (modeStr == "manual")   ? MODE_MANUAL
                   :                           MODE_BANG_BANG;
      modeRunning  = true;
      runActive    = true;
      runStartMs = millis();
      cacheHead  = 0;
      cacheCount = 0;
      openRunLog();
      if (selectedMode == MODE_AUTO_RAMP) {
        rampState         = RS_IDLE;
        rampStep          = 0;
        rampOvershootAmt  = 0.0f;
        learnedCount      = 0;
        finalSoakStartMs  = 0;
        resetStabilityBuf();
        coastingDropCount = 0;
        memset(learnedFireStartTemp, 0, sizeof(learnedFireStartTemp));
        memset(learnedCutoffTemp,    0, sizeof(learnedCutoffTemp));
        memset(learnedPeakTemp,      0, sizeof(learnedPeakTemp));
        memset(learnedCoastRatio,    0, sizeof(learnedCoastRatio));
      }
      DBGLN("Run started mode=" + modeStr);
    } else if (actionStr == "stop") {
      modeRunning = false;
      runActive   = false;
      applyHeater(false);
      closeRunLog();
      DBGLN("Run stopped");
    } else {
      server.send(400, "text/plain", "action must be start or stop"); return;
    }
    server.send(200, "text/plain", "OK");
  });

  // ── Duty cycle config route ───────────────────────────────────────────────
  // GET  /dutycycle  → returns current settings as JSON
  // POST /dutycycle  → accepts pct= (1–100) and period_ms= (1000–3600000)
  server.on("/dutycycle", HTTP_GET, []() {
    unsigned long periodElapsedMs = millis() - dcPeriodStartMs;
    uint32_t dcAllowedMs = (uint32_t)((dutyCyclePct / 100.0f) * (float)dutyCyclePeriodMs);
    uint32_t dcRemainingMs = (dcAllowedMs > dcOnTimeThisPeriod)
                           ? (dcAllowedMs - dcOnTimeThisPeriod) : 0;
    String j = "{\"pct\":"       + String(dutyCyclePct, 1)
             + ",\"period_ms\":" + String(dutyCyclePeriodMs)
             + ",\"onTimeMs\":"  + String(dcOnTimeThisPeriod)
             + ",\"remainMs\":"  + String(dcRemainingMs)
             + ",\"periodElapsedMs\":" + String(periodElapsedMs)
             + ",\"forceOff\":"  + String(dcForceOff ? 1 : 0)
             + "}";
    server.send(200, "application/json", j);
  });

  server.on("/dutycycle", HTTP_POST, []() {
    if (!requireAuth()) return;
    bool changed = false;
    if (server.hasArg("pct")) {
      float v = server.arg("pct").toFloat();
      v = constrain(v, 1.0f, 100.0f);
      dutyCyclePct = v;
      changed = true;
    }
    if (server.hasArg("period_ms")) {
      uint32_t v = (uint32_t)server.arg("period_ms").toInt();
      v = constrain(v, 1000UL, 3600000UL);
      dutyCyclePeriodMs = v;
      changed = true;
    }
    if (changed) {
      // Reset the period window immediately on setting change.
      dcPeriodStartMs    = millis();
      dcOnTimeThisPeriod = 0;
      dcForceOff         = false;
      dcOnStartMs        = 0;
      savePrefs();
    }
    server.send(200, "text/plain", "OK");
  });

  server.on("/log", HTTP_GET, []() {
    uint32_t since = 0;
    if (server.hasArg("since")) since = (uint32_t)server.arg("since").toInt();

    if (runLogFile) runLogFile.flush();

    if (spiffsOk && SPIFFS.exists(LOG_PATH)) {
      File f = SPIFFS.open(LOG_PATH, FILE_READ);
      if (f) {
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.sendHeader("Content-Type", "text/plain");
        server.sendHeader("Connection", "close");
        server.send(200, "text/plain", "");
        server.sendContent("t_s,tempC,setpoint,output,mode,ramp_state,step_idx,coast_ratio_est\n");
        String line;
        bool firstLine = true;
        while (f.available()) {
          line = f.readStringUntil('\n');
          if (firstLine) { firstLine = false; continue; }
          if (!line.length()) continue;
          int comma = line.indexOf(',');
          if (comma < 0) continue;
          uint32_t ts = (uint32_t)line.substring(0, comma).toInt();
          if (ts > since) server.sendContent(line + "\n");
        }
        f.close();
        server.sendContent("");
        return;
      }
    }
    String out = "t_s,tempC,setpoint,output,mode,ramp_state,step_idx,coast_ratio_est\n";
    uint16_t start = (cacheCount < CACHE_SIZE) ? 0 : cacheHead % CACHE_SIZE;
    uint16_t count = min(cacheCount, (uint16_t)CACHE_SIZE);
    for (uint16_t i = 0; i < count; i++) {
      CacheSample& s = sampleCache[(start + i) % CACHE_SIZE];
      if (s.t_s > since) {
        out += String(s.t_s) + "," + String(s.tC, 2) + "," + String(s.sp, 1)
             + "," + String(s.output) + "," + String(s.mode)
             + "," + String(s.ramp_state) + "," + String(s.step_idx)
             + "," + String(s.coast_ratio, 4) + "\n";
      }
    }
    server.send(200, "text/plain", out);
  });

  server.on("/log/full", HTTP_GET, []() {
    if (runLogFile) runLogFile.flush();

    if (spiffsOk && SPIFFS.exists(LOG_PATH)) {
      File f = SPIFFS.open(LOG_PATH, FILE_READ);
      if (f) {
        server.sendHeader("Content-Disposition", "attachment; filename=\"runlog.csv\"");
        server.setContentLength(f.size());
        server.send(200, "text/csv", "");
        uint8_t buf[256];
        while (f.available()) {
          size_t n = f.read(buf, sizeof(buf));
          server.sendContent_P((const char*)buf, n);
        }
        f.close();
        server.sendContent("");
        return;
      }
    }
    String out = "t_s,tempC,setpoint,output,mode,ramp_state,step_idx,coast_ratio_est\n";
    uint16_t start = (cacheCount < CACHE_SIZE) ? 0 : cacheHead % CACHE_SIZE;
    uint16_t count = min(cacheCount, (uint16_t)CACHE_SIZE);
    for (uint16_t i = 0; i < count; i++) {
      CacheSample& s = sampleCache[(start + i) % CACHE_SIZE];
      out += String(s.t_s) + "," + String(s.tC, 2) + "," + String(s.sp, 1)
           + "," + String(s.output) + "," + String(s.mode)
           + "," + String(s.ramp_state) + "," + String(s.step_idx)
           + "," + String(s.coast_ratio, 4) + "\n";
    }
    server.sendHeader("Content-Disposition", "attachment; filename=\"runlog_cache.csv\"");
    server.send(200, "text/csv", out);
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
    if (!requireAuth()) return;
    if (server.hasArg("sp"))    setpoint    = server.arg("sp").toFloat();
    if (server.hasArg("hyst"))  hysteresis  = server.arg("hyst").toFloat();
    if (server.hasArg("off"))   probeOffset = server.arg("off").toFloat();
    savePrefs();
    server.send(200, "text/plain", "OK");
  });

  server.on("/wifi", HTTP_POST, []() {
    if (!requireAuth()) return;
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

  server.on("/stop", HTTP_POST, []() {
    if (!requireAuth()) return;
    String action = server.arg("action");
    if (action == "latch") {
      stopLatched = true;
      modeRunning = false;
      runActive   = false;
      applyHeater(false);
      closeRunLog();
    } else if (action == "release") {
      stopLatched = false;
    } else {
      server.send(400, "text/plain", "action must be latch or release"); return;
    }
    server.send(200, "text/plain", "OK");
  });

  server.on("/manual", HTTP_POST, []() {
    if (!requireAuth()) return;
    if (stopLatched)              { server.send(403, "text/plain", "E-stop latched"); return; }
    if (selectedMode != MODE_MANUAL) { server.send(400, "text/plain", "Not in manual mode"); return; }
    String action = server.arg("action");
    if      (action == "on")  applyHeater(true);
    else if (action == "off") applyHeater(false);
    else { server.send(400, "text/plain", "action must be on or off"); return; }
    server.send(200, "text/plain", "OK");
  });

  server.on("/auth", HTTP_POST, []() {
    if (!requireAuth()) return;
    if (!server.hasArg("user") || !server.hasArg("pass")) {
      server.send(400, "text/plain", "Missing user or pass"); return;
    }
    String u = server.arg("user"); u.trim();
    String p = server.arg("pass"); p.trim();
    if (!u.length() || !p.length()) {
      server.send(400, "text/plain", "user and pass must not be empty"); return;
    }
    webUser = u;
    webPass = p;
    savePrefs();
    DBGLN("Auth credentials updated");
    server.send(200, "text/plain", "OK");
  });
}
