<img src="https://r2cdn.perplexity.ai/pplx-full-logo-primary-dark%402x.png" style="height:64px;margin-right:32px"/>

# I have this thermostat/furnace controller in my github repository called ESP32-Thermostat.. load the v2.0 branch and check the arduino sketch there and help me debug the button inputs. I need a small thing on the web page that shows every input and output pin state.

Here's my full analysis and the code you need to add.

***

## Button Bugs Found

### Bug 1 — `registerPress()` fires on EVERY center button short-press (including start/stop)

In `BTN_PENDING` when the button is released (`!low`), the code does this:

```cpp
if (i == 2) registerPress();         // ← always fires
if (i == 2 && !stopLatched) {        // ← then also fires start/stop
```

`registerPress()` increments the E-stop counter **on every normal run start/stop press**. After 3 short presses to start/stop a run, you get an **unintended E-stop latch**. The fix is to separate E-stop intent from normal press intent — only call `registerPress()` when the press is clearly a dedicated E-stop gesture (e.g., rapid triple-tap without `modeRunning` transitioning):

```cpp
case BTN_PENDING:
  if (!low) {
    btns[i].phase = BTN_IDLE;
    if (i == 2) {
      if (!stopLatched) {
        // Toggle run first; if nothing toggled, count toward e-stop
        bool handled = false;
        if (modeRunning) {
          modeRunning = false; applyHeater(false);
          runActive = false; closeRunLog();
          DBGLN("Mode stopped"); handled = true;
        } else {
          modeRunning = true;
          // ... ramp/bangbang init ...
          DBGLN("Mode started"); handled = true;
        }
        if (!handled) registerPress();  // only count if no run action taken
      } else {
        registerPress();  // latched state: all presses count toward unlatch
      }
    }
  }
```


### Bug 2 — MOSFET comment contradicts the macro

The comment says `outputOn=true → PIN_MOSFET LOW` (active-low), but the macro says:

```cpp
#define MOSFET_WRITE(on) digitalWrite(PIN_MOSFET, (on) ? HIGH : LOW)
```

That's `outputOn=true → HIGH`. **One of them is wrong.** If your load is active-high (MOSFET gate driven HIGH to conduct), the code is correct and the comment is wrong. If your circuit is genuinely active-low, the macro is backwards and the heater fires when it shouldn't. Check your MOSFET gate circuit and fix whichever is wrong — this could explain unexpected heater behavior.

### Bug 3 — UP/DN have no short-press action (silent swallow)

In `BTN_PENDING`, when UP (i=0) or DN (i=1) are released quickly before the debounce threshold, the code falls to `btns[i].phase = BTN_IDLE` and does **nothing** — no setpoint change, no feedback. The setpoint only changes on the HELD path. A very quick tap is completely ignored with no indication. This is probably intentional (prevent accidental taps) but worth knowing.

### Bug 4 — Center button long-press arms at `RAMP_DELAY_MS` (200ms), not `DEBOUNCE_MS`

```cpp
} else if (now - btns[i].pendingSince >= (i == 2 ? RAMP_DELAY_MS : DEBOUNCE_MS)) {
  btns[i].phase = BTN_HELD;
  btns[i].nextFire = now + (i == 2 ? CTR_LONGPRESS_MS : RAMP_DELAY_MS);
```

After 200ms held, center transitions to HELD and sets `nextFire` 2000ms later. But at that exact moment UP/DN would also fire a first setpoint step. If you hold center slightly while also bumping UP/DN, you can accidentally start a mode-change. Consider bumping `RAMP_DELAY_MS` for center or adding a flag.

***

## Add to `ESP32-Thermostat.ino` — New `/pinstatus` Endpoint

Insert this inside `setupRoutes()`, after the existing `/status` handler:

```cpp
  // ── Pin/GPIO debug status ─────────────────────────────────────────────────
  server.on("/pinstatus", HTTP_GET, []() {
    bool btnUp  = !digitalRead(PIN_BTN_UP);   // true = pressed (active-low pullup)
    bool btnDn  = !digitalRead(PIN_BTN_DN);
    bool btnCtr = !digitalRead(PIN_BTN_CTR);
    bool mosfetPin = digitalRead(PIN_MOSFET);  // HIGH when outputOn=true
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
```


***

## Add to `html.h` — Pin State Panel

Insert this HTML block **before the closing `</body>` tag** (around line 925):

```html
<!-- ── GPIO / Pin Debug Panel ─────────────────────────────────────── -->
<div class="card" id="pinDebugPanel">
  <h2 style="cursor:pointer;user-select:none;" onclick="togglePinPanel()">
    &#x1F527; GPIO Pin State
    <span id="pinPanelToggle" style="font-size:.75rem;color:#aaa;margin-left:.5rem;">[collapse]</span>
  </h2>
  <div id="pinPanelBody">
    <div style="display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:.5rem;margin-bottom:.75rem;">

      <!-- Input pins -->
      <div class="pin-cell" id="pc-btnUp">
        <div class="pin-label">BTN UP &nbsp;<span class="pin-num">GPIO6</span></div>
        <div class="pin-val" id="pv-btnUp">--</div>
        <div class="pin-phase" id="pp-btnUp">--</div>
      </div>
      <div class="pin-cell" id="pc-btnDn">
        <div class="pin-label">BTN DN &nbsp;<span class="pin-num">GPIO4</span></div>
        <div class="pin-val" id="pv-btnDn">--</div>
        <div class="pin-phase" id="pp-btnDn">--</div>
      </div>
      <div class="pin-cell" id="pc-btnCtr">
        <div class="pin-label">BTN CTR &nbsp;<span class="pin-num">GPIO5</span></div>
        <div class="pin-val" id="pv-btnCtr">--</div>
        <div class="pin-phase" id="pp-btnCtr">--</div>
      </div>

      <!-- Output pin -->
      <div class="pin-cell" id="pc-mosfet">
        <div class="pin-label">MOSFET &nbsp;<span class="pin-num">GPIO3</span></div>
        <div class="pin-val" id="pv-mosfet">--</div>
        <div class="pin-phase" id="pp-mosfet" style="font-size:.7rem;color:#aaa;">OUTPUT</div>
      </div>

      <!-- I2C (info only, not read) -->
      <div class="pin-cell" style="opacity:.55;">
        <div class="pin-label">I2C SDA &nbsp;<span class="pin-num">GPIO8</span></div>
        <div class="pin-val" style="color:#555;">I2C</div>
        <div class="pin-phase" style="font-size:.7rem;color:#555;">OLED/INA219</div>
      </div>
      <div class="pin-cell" style="opacity:.55;">
        <div class="pin-label">I2C SCL &nbsp;<span class="pin-num">GPIO9</span></div>
        <div class="pin-val" style="color:#555;">I2C</div>
        <div class="pin-phase" style="font-size:.7rem;color:#555;">OLED/INA219</div>
      </div>
    </div>

    <!-- State flags row -->
    <div style="display:flex;flex-wrap:wrap;gap:.4rem;align-items:center;margin-bottom:.5rem;">
      <span class="badge" id="flag-outputOn">outputOn: --</span>
      <span class="badge" id="flag-stop">stopLatched: --</span>
      <span class="badge" id="flag-mode">modeRunning: --</span>
      <span class="badge" id="flag-estop" style="background:#555;">E-stop count: --</span>
    </div>
    <div style="font-size:.75rem;color:#555;margin-top:.25rem;">
      Polls /pinstatus every 200 ms &nbsp;&#x2022;&nbsp; Buttons are INPUT_PULLUP (LOW = pressed)
    </div>
  </div>
</div>

<style>
  .pin-cell{background:#0f3460;border-radius:6px;padding:.5rem .65rem;border:1px solid #1a1a3e;}
  .pin-cell.pressed{border-color:#e94560;background:#2a1030;}
  .pin-cell.active{border-color:#2ecc71;background:#0a2510;}
  .pin-label{font-size:.72rem;color:#aaa;margin-bottom:.15rem;}
  .pin-num{font-size:.65rem;color:#555;}
  .pin-val{font-size:1.35rem;font-weight:bold;line-height:1.1;}
  .pin-phase{font-size:.72rem;color:#aaa;margin-top:.1rem;}
</style>

<script>
(function(){
  let pinPanelOpen = true;

  window.togglePinPanel = function() {
    pinPanelOpen = !pinPanelOpen;
    document.getElementById('pinPanelBody').style.display = pinPanelOpen ? '' : 'none';
    document.getElementById('pinPanelToggle').textContent = pinPanelOpen ? '[collapse]' : '[expand]';
  };

  function setPinCell(id, isActive, valText, phaseText, activeClass) {
    const cell  = document.getElementById('pc-' + id);
    const val   = document.getElementById('pv-' + id);
    const phase = document.getElementById('pp-' + id);
    if (!cell) return;
    cell.className = 'pin-cell ' + (isActive ? activeClass : '');
    val.textContent   = valText;
    val.style.color   = isActive ? (activeClass === 'pressed' ? '#e94560' : '#2ecc71') : '#555';
    if (phase && phaseText !== undefined) phase.textContent = phaseText;
  }

  async function pollPins() {
    try {
      const p = await fetch('/pinstatus').then(r => r.json());

      // Buttons (INPUT_PULLUP: pressed = true)
      setPinCell('btnUp',  p.btnUp,  p.btnUp  ? 'PRESSED' : 'OPEN', p.btnUpPhase,  'pressed');
      setPinCell('btnDn',  p.btnDn,  p.btnDn  ? 'PRESSED' : 'OPEN', p.btnDnPhase,  'pressed');
      setPinCell('btnCtr', p.btnCtr, p.btnCtr ? 'PRESSED' : 'OPEN', p.btnCtrPhase, 'pressed');

      // MOSFET output
      setPinCell('mosfet', p.outputOn, p.outputOn ? 'HIGH / ON' : 'LOW / OFF', 'OUTPUT', 'active');

      // Flags
      const fOut  = document.getElementById('flag-outputOn');
      const fStop = document.getElementById('flag-stop');
      const fMode = document.getElementById('flag-mode');
      const fEs   = document.getElementById('flag-estop');

      fOut.textContent  = 'outputOn: ' + (p.outputOn ? 'YES' : 'NO');
      fOut.className    = 'badge ' + (p.outputOn  ? 'on' : 'off');

      fStop.textContent = 'E-STOP: '   + (p.stopLatched ? 'LATCHED' : 'clear');
      fStop.className   = 'badge '     + (p.stopLatched ? 'off' : 'on');

      fMode.textContent = 'running: '  + (p.modeRunning ? 'YES' : 'NO');
      fMode.className   = 'badge '     + (p.modeRunning ? 'auto' : 'off');

      fEs.textContent   = 'E-stop presses: ' + p.estopCount + '/3';
      fEs.style.background = p.estopCount > 0 ? '#e67e22' : '#555';

    } catch(e) { /* device unreachable during reboot */ }
  }

  setInterval(pollPins, 200);
  pollPins();
})();
</script>
```


***

## Summary

| Issue | Severity | Location |
| :-- | :-- | :-- |
| `registerPress()` called on every start/stop press → accidental E-stop after 3 presses | 🔴 High | `updateButtons()`, `BTN_PENDING` release |
| MOSFET comment says active-low but macro drives HIGH for `outputOn=true` | 🔴 High | Line 34–36 — verify your circuit |
| UP/DN quick taps silently discarded (no feedback) | 🟡 Low/intentional | `BTN_PENDING`, `i != 2` release path |
| Center 200ms arm threshold same as UP/DN ramp delay — simultaneous press risk | 🟡 Low | Timing constants |

The `/pinstatus` endpoint + widget polls at 200ms so you can **watch buttons turn red in real time** while pressing them — very useful for verifying the debounce state machine and confirming which phase (`IDLE/PENDING/HELD`) each button is stuck in.

