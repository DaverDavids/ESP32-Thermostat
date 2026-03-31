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
const uint8_t PIN_BTN_UP  =  6;
const uint8_t PIN_BTN_DN  =  4;
const uint8_t PIN_BTN_CTR =  5;

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
const uint32_t FAST_SAMPLE_MS = 27;   // INA219 poll rate (~37Hz)
const uint32_t REPORT_MS      = 500;  // log/control/history rate (2Hz)
const uint32_t DISPLAY_MS     = 100;  // OLED update rate (10Hz)
const float    OUTLIER_MAX_JUMP = 150.0f;
const float    SHUNT_MIN_MV     =  -5.0f;
uint8_t        jumpRejectCount  = 0;
const uint8_t  JUMP_REJECT_MAX  = 10;

// ─── Fast sampling / median filter ───────────────────────────────────────────
#define MEDIAN_N 9
float   medBuf[MEDIAN_N];
uint8_t medCount = 0;
unsigned long lastFastSample = 0;

// ─── Button timing / debounce ────────────────────────────────────────────────
const uint32_t DEBOUNCE_MS          =   30;
const uint32_t BTN_SETTLE_MS        =   10;
const uint32_t CTR_LONGPRESS_MS     = 2000;
const uint32_t CTR_ESTOP_WINDOW_MS  = 1000;
const uint8_t  CTR_ESTOP_PRESSES    =    3;

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

// ─── Center button gesture tracking ──────────────────────────────────────────
uint8_t  estopPressCount = 0;
unsigned long estopWindowStart = 0;
bool centerLongPressHandled = false;

// ─── Ramp support (Stage 2) ───────────────────────────────────────────
#define RAMP_MAX_STEPS 8

struct RampProfile {
  char    name[32];
  float   stepTargets[RAMP_MAX_STEPS]; // °C, last entry = finalTarget
  uint8_t stepCount;                   // total steps including final
  float   soakMinutes;                 // hold time at final temp
  float   stabilityThreshC;            // °C change over 30s to call stable
  float   coastBase;                   // coast ratio at 0°C (y-intercept of linear model)
  float   coastSlope;                  // change in coast ratio per 100°C (negative)
  bool    complete;                    // was this profile learned on a full run?
};

// Default active profile (Stage 2 boot defaults; Stage 4 will persist but today we boot with defaults)
RampProfile activeProfile = {
  "default",
  {150.0f, 280.0f, 380.0f, 440.0f, 470.0f, 490.0f, 500.0f},
  7,
  30.0f,
  2.0f,
  0.40f,   // coastBase: initial guess, will be updated each step
  0.03f,   // coastSlope: ratio drops ~0.03 per 100°C
  false
};

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

RampState rampState    = RS_IDLE;
uint8_t   rampStep     = 0;
float     rampFireStartTemp = 0.0f;
float     rampCutoffTemp    = 0.0f;
float     rampPeakTemp      = 0.0f;
float     rampOvershootAmt  = 0.0f;
unsigned long rampStateEnteredMs = 0;
unsigned long finalSoakStartMs   = 0;
#define STABILITY_WINDOW 60
float stabilityBuf[STABILITY_WINDOW];
uint16_t stabilityIdx   = 0;
uint16_t stabilityFilled = 0;
uint8_t coastingDropCount = 0;

float effectiveCoastRatio(float tempC);
void refitCoastModel();
bool isStable();
void resetStabilityBuf();

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

static const char* LOG_PATH = "/runlog.csv";
bool     spiffsOk   = false;
bool     runActive  = false;
unsigned long runStartMs = 0;
File     runLogFile;

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
  if (runLogFile) { runLogFile.close(); }
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
  runLogFile.flush();
}

Preferences      prefs;
bool             prefsDirty = false;
unsigned long    prefsDirtyMs = 0;
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

String webUser = "admin";
String webPass = "thermostat";

const float PROBE_UV_PER_C[] = { 41.0f, 52.0f };

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

bool isStable() {
  stabilityBuf[stabilityIdx % STABILITY_WINDOW] = currentTemp;
  stabilityIdx++;
  if (stabilityFilled < STABILITY_WINDOW) stabilityFilled++;
  if (stabilityFilled < STABILITY_WINDOW) return false;
  float mn = stabilityBuf[0], mx = stabilityBuf[0];
  for (uint8_t i = 1; i < STABILITY_WINDOW; i++) {
    if (stabilityBuf[i] < mn) mn = stabilityBuf[i];
    if (stabilityBuf[i] > mx) mx = stabilityBuf[i];
  }
  return (mx - mn) < activeProfile.stabilityThreshC;
}

void resetStabilityBuf() {
  stabilityIdx    = 0;
  stabilityFilled = 0;
}

enum BtnPhase { BTN_IDLE, BTN_PENDING, BTN_HELD };
struct BtnState {
  uint8_t   pin;
  BtnPhase  phase;
  bool      stablePressed;
  bool      lastRawPressed;
  unsigned long lastRawChangeMs;
  unsigned long pressedAt;
  unsigned long nextFire;
  float     currentInterval;
  float     currentStep;
} btns[3];

void applyHeater(bool requestOn) {
  heatRequested = requestOn;
  if (stopLatched || !requestOn) {
    outputOn = false;
    MOSFET_WRITE(false);
  } else {
    outputOn = true;
    MOSFET_WRITE(true);
  }
}

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
void onUpPress(unsigned long now);
void onDnPress(unsigned long now);
void onCenterShortPress(unsigned long now);
void onCenterLongPress(unsigned long now);
void registerCenterEstopPress(unsigned long now);

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_MOSFET, OUTPUT);
  applyHeater(false);

  uint8_t btnPins[] = { PIN_BTN_UP, PIN_BTN_DN, PIN_BTN_CTR };
  for (int i = 0; i < 3; i++) {
    btns[i] = { btnPins[i], BTN_IDLE, false, false, 0, 0, 0, (float)RAMP_RATE_INITIAL_MS, SP_STEP_INITIAL };
    pinMode(btnPins[i], INPUT_PULLUP);
    bool rawPressed = !digitalRead(btnPins[i]);
    btns[i].stablePressed = rawPressed;
    btns[i].lastRawPressed = rawPressed;
    btns[i].lastRawChangeMs = millis();
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
      lastShuntMV = ina219.getShuntVoltage_mV();
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

void onUpPress(unsigned long now) {
  setpoint = min(setpoint + SP_STEP_INITIAL, 1200.0f);
  prefsDirty = true;
  prefsDirtyMs = now;
}

void onDnPress(unsigned long now) {
  setpoint = max(setpoint - SP_STEP_INITIAL, 0.0f);
  prefsDirty = true;
  prefsDirtyMs = now;
}

void registerCenterEstopPress(unsigned long now) {
  if (now - estopWindowStart > CTR_ESTOP_WINDOW_MS) {
    estopPressCount = 1;
    estopWindowStart = now;
  } else {
    estopPressCount++;
  }

  DBG("Center tap count: "); DBGLN(estopPressCount);

  if (estopPressCount >= CTR_ESTOP_PRESSES) {
    estopPressCount = 0;
    estopWindowStart = now;
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
}

void onCenterShortPress(unsigned long now) {
  registerCenterEstopPress(now);

  if (stopLatched) return;

  if (modeRunning) {
    modeRunning = false;
    runActive   = false;
    applyHeater(false);
    closeRunLog();
    DBGLN("Mode stopped");
    return;
  }

  modeRunning = true;
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

void onCenterLongPress(unsigned long now) {
  centerLongPressHandled = true;
  estopPressCount = 0;
  estopWindowStart = now;
  modeRunning = false;
  runActive = false;
  applyHeater(false);
  closeRunLog();
  selectedMode = (RunMode)((selectedMode + 1) % 3);
  modeOverlayUntil = now + MODE_OVERLAY_MS;
  DBG("Mode selected: "); DBGLN(selectedMode);
}

void updateButtons() {
  unsigned long now = millis();

  for (int i = 0; i < 3; i++) {
    BtnState& b = btns[i];
    bool rawPressed = !digitalRead(b.pin);

    if (rawPressed != b.lastRawPressed) {
      b.lastRawPressed = rawPressed;
      b.lastRawChangeMs = now;
    }

    if ((now - b.lastRawChangeMs) < DEBOUNCE_MS) {
      continue;
    }

    if (rawPressed != b.stablePressed) {
      b.stablePressed = rawPressed;

      if (rawPressed) {
        b.phase = BTN_PENDING;
        b.pressedAt = now;
        b.nextFire = now + RAMP_DELAY_MS;
        b.currentInterval = RAMP_RATE_INITIAL_MS;
        b.currentStep = SP_STEP_INITIAL;
        if (i == 2) centerLongPressHandled = false;
      } else {
        unsigned long heldMs = now - b.pressedAt;
        bool wasHeld = (b.phase == BTN_HELD);
        b.phase = BTN_IDLE;
        b.nextFire = 0;
        b.currentInterval = RAMP_RATE_INITIAL_MS;
        b.currentStep = SP_STEP_INITIAL;

        if (i == 2) {
          if (!centerLongPressHandled && heldMs >= BTN_SETTLE_MS) {
            onCenterShortPress(now);
          }
        } else {
          if (!wasHeld && heldMs >= BTN_SETTLE_MS) {
            if (i == 0) onUpPress(now);
            if (i == 1) onDnPress(now);
          }
          if (prefsDirty) { savePrefs(); prefsDirty = false; }
        }
      }
      continue;
    }

    if (!b.stablePressed) continue;

    if (i == 2) {
      if (!centerLongPressHandled && (now - b.pressedAt) >= CTR_LONGPRESS_MS) {
        b.phase = BTN_HELD;
        onCenterLongPress(now);
      }
    } else {
      if ((now - b.pressedAt) >= RAMP_DELAY_MS) {
        b.phase = BTN_HELD;
      }
      if (b.phase == BTN_HELD && now >= b.nextFire) {
        if (i == 0) setpoint = min(setpoint + b.currentStep, 1200.0f);
        if (i == 1) setpoint = max(setpoint - b.currentStep, 0.0f);
        b.currentInterval = max((float)RAMP_RATE_MIN_MS, b.currentInterval * RAMP_ACCEL);
        b.currentStep     = min(SP_STEP_MAX, b.currentStep * SP_STEP_ACCEL);
        b.nextFire        = now + (unsigned long)b.currentInterval;
        prefsDirty = true;
        prefsDirtyMs = now;
      }
    }
  }
}

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
    jumpRejectCount++;
    if (jumpRejectCount >= JUMP_REJECT_MAX) {
      lastGoodTemp = NAN;
      jumpRejectCount = 0;
      DBGLN("Jump lock cleared");
    }
    return isnan(lastGoodTemp) ? candidate : lastGoodTemp;
  }
  jumpRejectCount = 0;
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

void controlLoop() {
  if      (currentTemp < setpoint - hysteresis) applyHeater(true);
  else if (currentTemp > setpoint + hysteresis) applyHeater(false);
}

void rampControlLoop() {
  if (rampStep >= activeProfile.stepCount) {
    rampStep = activeProfile.stepCount > 0 ? activeProfile.stepCount - 1 : 0;
  }

  bool  isFinalStep = (rampStep == activeProfile.stepCount - 1);
  float stepTarget  = activeProfile.stepTargets[rampStep];

  switch (rampState) {
    case RS_IDLE:
      rampStep           = 0;
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
        if (currentTemp <= stepTarget + 5.0f && isStable()) {
          if (isFinalStep) {
            rampState        = RS_FINAL_SOAK;
            finalSoakStartMs = millis();
            rampStateEnteredMs = millis();
            resetStabilityBuf();
            DBGLN("Ramp: FINAL_SOAK started");
          } else {
            rampStep++;
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
            finalSoakStartMs = millis();
            rampStateEnteredMs = millis();
            resetStabilityBuf();
            DBGLN("Ramp: FINAL_SOAK started");
          } else {
            rampStep++;
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

void setupOTA() {
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.onStart([]()  { DBGLN("OTA start"); });
  ArduinoOTA.onError([](ota_error_t e) { DBG("OTA err "); DBGLN(e); });
  ArduinoOTA.begin();
}

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
  savedPSK      = prefs.getString("psk",  MYPSK);
  cjcOffset     = prefs.getFloat ("cjco", -12.0);
  webUser = prefs.getString("wuser", "admin");
  webPass = prefs.getString("wpass", "thermostat");
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
  prefs.putString("wuser", webUser);
  prefs.putString("wpass", webPass);
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
    float uvPerC = (customUvPerC > 0.0f)
                 ? customUvPerC
                 : PROBE_UV_PER_C[constrain(probeType, 0, 1)];
    float cjc_C   = temperatureRead() + cjcOffset;
    float cjc_mV  = (cjc_C * uvPerC) / 1000.0f;
    float totalMV = lastShuntMV + cjc_mV;
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
             + ",\"probeType\":"    + String(probeType)
             + ",\"customUvPerC\":" + String(customUvPerC,  4)
             + ",\"shuntMV\":"      + String(lastShuntMV,   4)
             + ",\"totalMV\":"      + String(totalMV,       4)
             + ",\"cjcC\":"         + String(cjc_C,         2)
             + ",\"uvPerC\":"       + String(uvPerC,        4)
             + ",\"cjcOffset\":"    + String(cjcOffset,     1)
             + ",\"calMv1\":"       + String(calMv1,        4)
             + ",\"calTemp1\":"     + String(calTemp1,      1)
             + ",\"calCjc1\":"      + String(calCjc1,       2)
             + ",\"calMv2\":"       + String(calMv2,        4)
             + ",\"calTemp2\":"     + String(calTemp2,      1)
             + ",\"calCjc2\":"      + String(calCjc2,       2)
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
      + "}";
    server.send(200, "application/json", j);
  });
}
