<img src="https://r2cdn.perplexity.ai/pplx-full-logo-primary-dark%402x.png" style="height:64px;margin-right:32px"/>

# You pushed some code but then had an issue... please check what still needs to be added, and describe all code changes precisely but concisely, so a third party can implement.

Good — now I have the full picture. The `.ino` has all the firmware changes. Let me check what's missing in `html.h`:

The firmware (`ESP32-Thermostat.ino`) is complete with all the fixes. The `html.h` file was updated in the *previous* commit (Apr 2) but that commit only fixed the UI bugs — **the duty cycle UI was never added to `html.h`**. The `/dutycycle` GET/POST routes exist in the firmware, the `dcPct`/`dcPeriodMs` fields are already in the `/status` JSON, but there's no UI section to display or configure them.

Here is a precise, concise description of every change still needed, all of which are in **`html.h`** only:

***

## What Was Pushed (Complete — No Changes Needed)

**`ESP32-Thermostat.ino`** — all firmware changes are in:

- `resetStabilityBuf()` now calls `memset(stabilityBuf, 0, sizeof(stabilityBuf))` so old temperatures never bleed into fresh stability checks
- `isStable()` uses `stabilityFilled < STABILITY_WINDOW` guard correctly
- `RS_OVERSHOOT_WAIT` exit now requires `currentTemp <= stepTarget + 5.0f` **and** `isStable()` — the `resetStabilityBuf()` already called on entry to this state ensures the buffer is clean
- `rampOvershootAmt` is recorded at coasting exit after `coastingDropCount >= 3` confirms the true peak, not at an early sample
- `appendRunLog()` — `flush()` is now throttled to `LOG_FLUSH_INTERVAL_MS = 5000ms` instead of every 500ms sample; `closeRunLog()` still does a final flush
- `applyHeater()` — single chokepoint that calls `dutyCycleGate()` before writing the MOSFET; all code paths (manual, bang-bang, ramp) are protected
- `dutyCycleGate()` — rolling window duty-cycle enforcer; `dutyCyclePct` (1–100%) and `dutyCyclePeriodMs` (1000–3600000ms) are both persisted via Preferences keys `dc_pct` / `dc_period`
- `/dutycycle` GET route returns `{pct, period_ms, onTimeMs, remainMs, periodElapsedMs, forceOff}`
- `/dutycycle` POST route accepts `pct=` and `period_ms=`, constrains them, resets the period window, saves prefs
- `/status` JSON now includes `dcPct`, `dcPeriodMs`, `dcOnTimeMs`, `dcRemainingMs`, `dcForceOff`
- `poll()` fetch in the UI uses `setInterval(poll, 1000)` — **variable poll rate is NOT yet fixed** (see below)

***

## What Still Needs to Be Added — `html.h` Only

### 1. Duty Cycle Settings Card

Add a new card to the configuration row (the 3-column row with "Configuration", "Web Password", "WiFi"). This card should:

- Display the current `dcPct` and `dcPeriodMs` values, pre-populated from `/status` on page load
- Have a number input `id="dcPct"` (label: "Duty Cycle %", step 1, min 1, max 100)
- Have a number input `id="dcPeriodMs"` (label: "Period (ms)", step 1000, min 1000, max 3600000) — consider also a friendly label showing seconds
- Have a Save button that POSTs to `/dutycycle` with `pct=` and `period_ms=` params
- Show a live status line: "Used: Xms / Yms this period" and a "DC LIMIT" badge (red, visible when `dcForceOff === 1`) — sourced from `/status` fields already polled each second

**Where the row-3 is in html.h:**

```html
<!-- ── Row 4: Config + Web Password + WiFi ───────────────────────────────── -->
<div class="row row-3">
```

Change `row-3` → `row-4` and add a 4th `.card` div after the WiFi card. Or keep it `row-3` and add the card as a 4th child (it will wrap on mobile automatically since `row-3` uses `repeat(3,1fr)` — change the grid to `repeat(auto-fill, minmax(220px, 1fr))` so it wraps cleanly).

**The new card HTML (insert after the WiFi card `</div>`):**

```html
<div class="card">
  <h2>&#x23F1; Duty Cycle</h2>
  <label>Max ON % per period
    <input type="number" step="1" min="1" max="100" id="dcPct" value="100">
  </label>
  <label>Period (ms)
    <input type="number" step="1000" min="1000" max="3600000" id="dcPeriodMs" value="60000">
  </label>
  <button onclick="saveDutyCycle()">Save Duty Cycle</button>
  <div id="dcStatus" style="margin-top:.4rem;font-size:.85rem;color:#aaa;"></div>
  <div id="dcMsg" style="font-size:.85rem;color:#0f9;margin-top:.2rem;"></div>
</div>
```

**The JS function (add inside the `<script>` block, near the config/auth helpers):**

```js
async function saveDutyCycle() {
  const pct = parseFloat(document.getElementById('dcPct').value);
  const ms  = parseInt(document.getElementById('dcPeriodMs').value, 10);
  if (isNaN(pct) || isNaN(ms)) { alert('Invalid values'); return; }
  try {
    const r = await fetch('/dutycycle', {
      method: 'POST',
      body: new URLSearchParams({ pct: pct.toFixed(1), period_ms: ms })
    });
    if (!r.ok) throw new Error('HTTP ' + r.status);
    document.getElementById('dcMsg').textContent = 'Saved.';
  } catch(e) {
    document.getElementById('dcMsg').textContent = 'Failed: ' + e;
  }
}
```

**In `poll()`, after the config pre-population block (the `if (!document.getElementById('cfgSp').value)` block), add:**

```js
// Update duty cycle display from status
const dcStatusEl = document.getElementById('dcStatus');
if (dcStatusEl) {
  const usedS   = (st.dcOnTimeMs  / 1000).toFixed(1);
  const limitS  = ((st.dcPct / 100) * st.dcPeriodMs / 1000).toFixed(1);
  const remS    = (st.dcRemainingMs / 1000).toFixed(1);
  dcStatusEl.innerHTML = 'Used: ' + usedS + 's / ' + limitS + 's &bull; Remaining: ' + remS + 's'
    + (st.dcForceOff ? ' <span class="badge off">DC LIMIT</span>' : '');
}
// Pre-populate inputs only if not focused
if (document.activeElement !== document.getElementById('dcPct'))
  document.getElementById('dcPct').value = st.dcPct.toFixed(0);
if (document.activeElement !== document.getElementById('dcPeriodMs'))
  document.getElementById('dcPeriodMs').value = st.dcPeriodMs;
```


***

### 2. Fix Variable Web UI Poll Rate

The current `poll()` runs unconditionally at 1Hz via `setInterval(poll, 1000)`. Since `poll()` itself calls `syncLog()` (which does a `/log` fetch) and `pollRamp()` (another `/rampstatus` fetch) conditionally — if any of those fetches stall (the device is slow during a SPIFFS flush), subsequent poll calls stack up, causing the "sometimes slow, sometimes fast" behavior.

**Fix — replace the `setInterval(poll, 1000)` line and the `poll()` function opening with:**

```js
// Guard: don't start a new poll if one is already in flight
let pollInFlight = false;
async function poll() {
  if (pollInFlight) return;
  pollInFlight = true;
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 4000); // 4s timeout
  try {
    const [st] = await Promise.all([
      fetch('/status', { signal: controller.signal }).then(r => r.json()),
      fetch('/history', { signal: controller.signal }).then(r => r.json())
    ]);
    clearTimeout(timeout);
    // ... rest of existing poll() body unchanged ...
  } catch(e) {
    clearTimeout(timeout);
    console.warn('poll error', e);
  } finally {
    pollInFlight = false;
  }
}
setInterval(poll, 1000);
```

Also add an `AbortController` + 4s timeout to `syncLog()` and `pollRamp()` in the same pattern (wrap their `fetch` calls with a signal). This prevents stalled fetches from blocking subsequent polls indefinitely.

***

### Summary Table

| File | Change | Status |
| :-- | :-- | :-- |
| `ESP32-Thermostat.ino` | All firmware fixes + duty cycle backend | ✅ Already pushed |
| `html.h` | Duty cycle settings card + live status | ❌ Not yet added |
| `html.h` | `poll()` in-flight guard + fetch timeouts | ❌ Not yet added |

