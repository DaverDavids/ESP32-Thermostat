#pragma once

const char HTML_INDEX[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Thermostat</title>
<style>
  body{font-family:sans-serif;background:#1a1a2e;color:#eee;margin:0;padding:1rem}
  h2{margin:.5rem 0;color:#e94560}
  .card{background:#16213e;border-radius:8px;padding:1rem;margin-bottom:1rem}
  .big{font-size:3rem;font-weight:bold;color:#0f3460}
  .big span{color:#e94560}
  label{display:block;margin:.4rem 0 .1rem}
  input,select{width:100%;padding:.4rem;border-radius:4px;border:none;background:#0f3460;color:#eee;box-sizing:border-box}
  button{margin-top:.6rem;padding:.5rem 1.2rem;background:#e94560;color:#fff;border:none;border-radius:4px;cursor:pointer}
  canvas{width:100%!important;max-height:200px}
  .badge{display:inline-block;padding:.2rem .6rem;border-radius:4px;font-size:.85rem}
  .on{background:#2ecc71}.off{background:#e74c3c}.auto{background:#2980b9}
  #wifiSection{display:none}
  #logInfo{font-size:.85rem;color:#aaa;margin-top:.4rem}
</style>
</head>
<body>

<div class="card">
  <h2>&#x1F321; Live Temperature</h2>
  <div class="big">Temp: <span id="temp">--</span> &deg;C</div>
  <div>Setpoint: <span id="sp">--</span> &deg;C &nbsp; Output: <span id="out" class="badge">--</span></div>
  <div style="margin-top:.5rem;font-size:.9rem;color:#aaa;">
    <div>CJC (board): <span id="cjcC">--</span> &deg;C</div>
    <div>Shunt mV (raw): <span id="shuntMV">--</span> mV</div>
    <div>Total mV (with CJC): <span id="totalMV">--</span> mV</div>
  </div>
  <div style="margin-top:.8rem;display:flex;gap:.5rem;flex-wrap:wrap;">
    <button onclick="setOutput('on')">Manual ON</button>
    <button onclick="setOutput('off')" style="background:#555;">Manual OFF</button>
    <button onclick="setOutput('auto')" style="background:#2980b9;">Auto</button>
  </div>
</div>

<div class="card">
  <h2>&#x25B6; Run Control</h2>
  <div style="display:flex;gap:.5rem;align-items:flex-end;flex-wrap:wrap;">
    <div style="flex:1;min-width:140px;">
      <label style="margin-bottom:.3rem;">Mode
        <select id="runModeSelect">
          <option value="bangbang">Bang-Bang</option>
          <option value="autoramp">Auto-Ramp (Stage 2)</option>
        </select>
      </label>
    </div>
    <button onclick="startRun()" style="background:#2ecc71;color:#111;">&#x25B6; Start Run</button>
    <button onclick="stopRun()"  style="background:#e74c3c;">&#x25A0; Stop Run</button>
  </div>
  <div id="runStatus" style="margin-top:.5rem;font-size:.9rem;color:#aaa;">No run active.</div>
</div>

  <div class="card" id="rampPanel" style="display:none;">
    <h2>&#x1F525; Auto-Ramp Status</h2>
    <table style="width:100%;font-size:.9rem;border-collapse:collapse;" id="rampTable">
      <tr><td style="color:#aaa;width:45%">State</td>       <td id="rsState">--</td></tr>
      <tr><td style="color:#aaa;">Step</td>                 <td id="rsStep">--</td></tr>
      <tr><td style="color:#aaa;">Step Target</td>          <td id="rsTarget">--</td></tr>
      <tr><td style="color:#aaa;">Predicted Peak</td>       <td id="rsPredPeak">--</td></tr>
      <tr><td style="color:#aaa;">Overshoot</td>            <td id="rsOvershoot">--</td></tr>
      <tr><td style="color:#aaa;">Coast Ratio (cur est)</td><td id="rsCoastCur">--</td></tr>
      <tr><td style="color:#aaa;">Coast Model</td>          <td id="rsCoastModel">--</td></tr>
      <tr><td style="color:#aaa;">Time in State</td>        <td id="rsAge">--</td></tr>
      <tr><td style="color:#aaa;">Soak Remaining</td>       <td id="rsSoakRemain">--</td></tr>
    </table>
    <div style="margin-top:.8rem;">
      <strong style="color:#aaa;font-size:.85rem;">Learned Steps</strong>
      <table id="learnedTable" style="width:100%;font-size:.8rem;margin-top:.3rem;border-collapse:collapse;">
        <thead><tr>
          <th style="text-align:left;color:#aaa;">Step</th>
          <th style="text-align:right;color:#aaa;">FireStart</th>
          <th style="text-align:right;color:#aaa;">Cutoff</th>
          <th style="text-align:right;color:#aaa;">Peak</th>
          <th style="text-align:right;color:#aaa;">CoastR</th>
        </tr></thead>
        <tbody id="learnedBody"></tbody>
      </table>
    </div>
  </div>

  <div class="card" id="profilePanel" style="display:none;">
    <h2>&#x1F4CB; Ramp Profile</h2>
    <form id="profileForm">
      <label>Profile Name<input type="text" name="name" id="profName"></label>
      <label>Step Targets (&deg;C, comma-separated)
        <input type="text" name="steps" id="profSteps" placeholder="150,280,380,440,470,490,500">
      </label>
      <label>Final Soak (minutes)<input type="number" step="1" name="soakMin" id="profSoak"></label>
      <label>Stability Threshold (&deg;C / 30s)<input type="number" step="0.5" name="stability" id="profStability"></label>
      <label>Coast Base (ratio at 0&deg;C)<input type="number" step="0.01" name="coastBase" id="profCoastBase"></label>
      <label>Coast Slope (ratio drop per 100&deg;C)<input type="number" step="0.005" name="coastSlope" id="profCoastSlope"></label>
      <div style="display:flex;gap:.5rem;margin-top:.6rem;flex-wrap:wrap;">
        <button type="submit">Apply to Device</button>
        <button type="button" onclick="loadProfileFromDevice()" style="background:#555;">&#x21BA; Refresh from Device</button>
      </div>
    </form>
    <div id="profileMsg" style="margin-top:.4rem;font-size:.85rem;color:#0f9;"></div>
  </div>

<div class="card">
  <h2>&#x1F4CA; History</h2>
  <canvas id="chart"></canvas>
</div>

<div class="card">
  <h2>&#x1F4BE; Data Log</h2>
  <div id="logInfo">No run data in browser yet.</div>
  <div style="margin-top:.6rem;display:flex;gap:.5rem;flex-wrap:wrap;">
    <button onclick="exportCSV()" style="background:#2980b9;">&#x2B07; Export CSV</button>
    <button onclick="clearBrowserLog()" style="background:#555;">Clear Browser Log</button>
    <button onclick="rePullLog()" style="background:#555;">Re-pull from Device</button>
  </div>
</div>

<div class="card">
  <h2>&#x2699;&#xFE0F; Configuration</h2>
  <form id="cfgForm">
    <label>Setpoint (&deg;C)<input type="number" step="1" name="sp" id="cfgSp"></label>
    <label>Hysteresis (&deg;C)<input type="number" step="0.5" name="hyst" id="cfgHyst"></label>
    <label>Probe Offset (&deg;C)<input type="number" step="0.5" name="off" id="cfgOff"></label>
    <label>CJC Offset (&deg;C)<input type="number" step="0.5" name="cjco" id="cfgCjco"></label>
    <div>Current uV/&deg;C: <span id="uvpc">--</span></div>
    <button type="submit">Save Config</button>
  </form>
</div>

<div class="card">
  <h2>&#x1F527; Calibration</h2>
  <div style="margin-bottom:.5rem;">
    <label>Probe Mode
      <select id="cfgPtype" name="ptype" form="cfgForm" onchange="updateCalMode(this.value)">
        <option value="0">K-Type (~41 &micro;V/&deg;C)</option>
        <option value="1">J-Type (~52 &micro;V/&deg;C)</option>
        <option value="2">Manual calibration</option>
      </select>
    </label>
  </div>
  <div id="calInputs" style="display:none;">
  <form id="calForm">
    <div style="display:grid;grid-template-columns:1fr 1fr;gap:1rem;">
      <div>
        <label>Point 1 - mV<input type="number" step="0.0001" name="mv1" id="calMv1"></label>
        <label>Point 1 - CJC (&deg;C)<input type="number" step="0.1" name="cjc1" id="calCjc1"></label>
        <label>Point 1 - True Temp (&deg;C)<input type="number" step="0.1" name="temp1" id="calTemp1"></label>
      </div>
      <div>
        <label>Point 2 - mV<input type="number" step="0.0001" name="mv2" id="calMv2"></label>
        <label>Point 2 - CJC (&deg;C)<input type="number" step="0.1" name="cjc2" id="calCjc2"></label>
        <label>Point 2 - True Temp (&deg;C)<input type="number" step="0.1" name="temp2" id="calTemp2"></label>
      </div>
    </div>
    <div style="display:flex;gap:.5rem;margin-top:.5rem;">
      <button type="submit">Calibrate</button>
      <button type="button" onclick="clearCalibration()" style="background:#a33;">Clear Calibration</button>
    </div>
  </form>
  <div id="calResult" style="margin-top:.5rem;color:#0f9"></div>
  </div>
</div>

<div class="card">
  <h2>&#x1F4F6; WiFi</h2>
  <button onclick="document.getElementById('wifiSection').style.display='block'">Change WiFi</button>
  <div id="wifiSection">
    <form id="wifiForm">
      <label>SSID<input type="text" name="ssid"></label>
      <label>Password<input type="password" name="psk"></label>
      <button type="submit">Save &amp; Reboot</button>
    </form>
  </div>
</div>

<script>
// ── IndexedDB setup ───────────────────────────────────────────────────────────
const DB_NAME = 'thermostat_log';
const DB_VER  = 1;
const STORE   = 'samples';
let db = null;

function openDB() {
  return new Promise((res, rej) => {
    const req = indexedDB.open(DB_NAME, DB_VER);
    req.onupgradeneeded = e => {
      const d = e.target.result;
      if (!d.objectStoreNames.contains(STORE)) {
        const s = d.createObjectStore(STORE, { keyPath: 't_s' });
        s.createIndex('t_s', 't_s', { unique: true });
      }
    };
    req.onsuccess = e => { db = e.target.result; res(db); };
    req.onerror   = e => rej(e);
  });
}

function dbPutRows(rows) {
  if (!db || !rows.length) return Promise.resolve();
  return new Promise((res, rej) => {
    const tx = db.transaction(STORE, 'readwrite');
    const st = tx.objectStore(STORE);
    rows.forEach(r => st.put(r));
    tx.oncomplete = res;
    tx.onerror    = rej;
  });
}

function dbGetAll() {
  if (!db) return Promise.resolve([]);
  return new Promise((res, rej) => {
    const tx  = db.transaction(STORE, 'readonly');
    const req = tx.objectStore(STORE).getAll();
    req.onsuccess = e => res(e.target.result);
    req.onerror   = rej;
  });
}

function dbGetMaxTs() {
  if (!db) return Promise.resolve(0);
  return new Promise((res) => {
    const tx  = db.transaction(STORE, 'readonly');
    const idx = tx.objectStore(STORE).index('t_s');
    const req = idx.openCursor(null, 'prev');
    req.onsuccess = e => res(e.target.result ? e.target.result.key : 0);
    req.onerror   = () => res(0);
  });
}

function dbClear() {
  if (!db) return Promise.resolve();
  return new Promise((res, rej) => {
    const tx = db.transaction(STORE, 'readwrite');
    tx.objectStore(STORE).clear();
    tx.oncomplete = res;
    tx.onerror    = rej;
  });
}

// ── CSV parse helper ──────────────────────────────────────────────────────────
function parseLogCSV(text) {
  const lines = text.trim().split('\n');
  if (lines.length < 2) return [];
  const header = lines[0].split(',');
  return lines.slice(1).map(l => {
    const vals = l.split(',');
    const obj = {};
    header.forEach((h, i) => {
      obj[h.trim()] = isNaN(vals[i]) ? vals[i] : parseFloat(vals[i]);
    });
    return obj;
  }).filter(r => !isNaN(r.t_s));
}

// ── Sync: pull new rows from device since last known t_s ──────────────────────
let lastSyncTs = 0;
async function syncLog() {
  try {
    const since = await dbGetMaxTs();
    lastSyncTs  = since;
    const resp  = await fetch('/log?since=' + since);
    const text  = await resp.text();
    const rows  = parseLogCSV(text);
    if (rows.length) {
      await dbPutRows(rows);
      updateLogInfo();
    }
  } catch(e) { console.warn('syncLog error', e); }
}

async function rePullLog() {
  try {
    await dbClear();
    const resp = await fetch('/log?since=0');
    const text = await resp.text();
    const rows = parseLogCSV(text);
    await dbPutRows(rows);
    updateLogInfo();
    alert('Re-pulled ' + rows.length + ' rows from device.');
  } catch(e) { alert('Re-pull failed: ' + e); }
}

async function updateLogInfo() {
  const rows = await dbGetAll();
  const el   = document.getElementById('logInfo');
  if (!rows.length) { el.textContent = 'No run data in browser yet.'; return; }
  const last = rows[rows.length - 1];
  el.textContent = rows.length + ' samples stored \u2022 last t=' + last.t_s + 's';
}

async function clearBrowserLog() {
  if (!confirm('Clear all browser-stored log data?')) return;
  await dbClear();
  updateLogInfo();
}

async function exportCSV() {
  const rows = await dbGetAll();
  if (!rows.length) { alert('No data to export.'); return; }
  const header = Object.keys(rows[0]).join(',');
  const body   = rows.map(r => Object.values(r).join(',')).join('\n');
  const blob   = new Blob([header + '\n' + body], {type: 'text/csv'});
  const url    = URL.createObjectURL(blob);
  const a      = document.createElement('a');
  a.href = url; a.download = 'thermostat_run.csv'; a.click();
  URL.revokeObjectURL(url);
}

// ── Run control ───────────────────────────────────────────────────────────────
document.getElementById('runModeSelect').addEventListener('change', function() {
  const isRamp = this.value === 'autoramp';
  document.getElementById('rampPanel').style.display    = isRamp ? 'block' : 'none';
  document.getElementById('profilePanel').style.display = isRamp ? 'block' : 'none';
});

async function loadProfileFromDevice() {
  try {
    const p = await fetch('/profile').then(r => r.json());
    document.getElementById('profName').value       = p.name;
    document.getElementById('profSteps').value      = p.stepTargets.join(',');
    document.getElementById('profSoak').value       = p.soakMinutes;
    document.getElementById('profStability').value  = p.stabilityThresh;
    document.getElementById('profCoastBase').value  = p.coastBase;
    document.getElementById('profCoastSlope').value = p.coastSlope;
    document.getElementById('profileMsg').textContent = 'Loaded from device.';
  } catch(e) {
    document.getElementById('profileMsg').textContent = 'Load failed: ' + e;
  }
}

document.getElementById('profileForm').addEventListener('submit', async e => {
  e.preventDefault();
  const p = new URLSearchParams(new FormData(e.target));
  try {
    const r = await fetch('/profile', {method:'POST', body: p});
    if (!r.ok) throw new Error('HTTP ' + r.status);
    document.getElementById('profileMsg').textContent = 'Applied to device.';
  } catch(e) {
    document.getElementById('profileMsg').textContent = 'Apply failed: ' + e;
  }
});

async function startRun() {
  const mode = document.getElementById('runModeSelect').value;
  const rows = await dbGetAll();
  if (rows.length > 0) {
    if (!confirm('Starting a new run will clear ' + rows.length + ' stored log rows from the previous run. Continue?')) return;
  }
  await dbClear();
  await fetch('/run', {method:'POST', body: new URLSearchParams({mode: mode === 'autoramp' ? 'autoramp' : 'bangbang', action:'start'})});
  poll();
}

async function stopRun() {
  await fetch('/run', {method:'POST', body: new URLSearchParams({action:'stop'})});
  poll();
}

// ── Minimal canvas line chart ─────────────────────────────────────────────────
function drawChart(data, sp) {
  const c   = document.getElementById('chart');
  const w   = c.width = c.offsetWidth;
  const h   = c.height = 200;
  const ctx = c.getContext('2d');
  ctx.clearRect(0, 0, w, h);
  if (!data.length) return;
  const min = Math.min(...data) - 10;
  const max = Math.max(...data, sp) + 10;
  const sx  = w / (data.length - 1 || 1);
  const sy  = h / (max - min);
  const py  = v => h - (v - min) * sy;
  ctx.strokeStyle = '#e94560'; ctx.lineWidth = 1; ctx.setLineDash([4,4]);
  ctx.beginPath(); ctx.moveTo(0, py(sp)); ctx.lineTo(w, py(sp)); ctx.stroke();
  ctx.setLineDash([]);
  ctx.strokeStyle = '#0f9'; ctx.lineWidth = 2;
  ctx.beginPath();
  data.forEach((v, i) => i ? ctx.lineTo(i*sx, py(v)) : ctx.moveTo(0, py(v)));
  ctx.stroke();
}

let currentSetpoint = 500;
let wasRunActive = false;
const RAMP_STATE_NAMES = [ 'IDLE', 'HEATING', 'COASTING', 'SOAKING', 'OVERSHOOT WAIT', 'FINAL SOAK', 'DONE' ];

async function poll() {
  try {
    const [st, hist] = await Promise.all([
      fetch('/status').then(r=>r.json()),
      fetch('/history').then(r=>r.json())
    ]);
    document.getElementById('temp').textContent = st.temp.toFixed(1);
    document.getElementById('sp').textContent   = st.setpoint.toFixed(1);
    document.getElementById('uvpc').textContent = st.uvPerC.toFixed(4);
    const outEl = document.getElementById('out');
    if (st.manual) {
      outEl.textContent = st.output ? 'MANUAL ON' : 'MANUAL OFF';
      outEl.className   = 'badge ' + (st.output ? 'on' : 'off');
    } else {
      outEl.textContent = st.output ? 'AUTO ON' : 'AUTO OFF';
      outEl.className   = 'badge auto';
    }
    currentSetpoint = st.setpoint;

    // Run status line
    const rsEl = document.getElementById('runStatus');
    if (st.runActive) {
      const mLabel = st.runMode === 1 ? 'Auto-Ramp' : 'Bang-Bang';
      const elapsed = st.runElapsed || 0;
      const m = Math.floor(elapsed/60), s = elapsed%60;
      rsEl.textContent = '\u25CF Running \u2022 Mode: ' + mLabel +
        ' \u2022 Elapsed: ' + m + ':' + String(s).padStart(2,'0');
      rsEl.style.color = '#2ecc71';

      const isRamp = st.runMode === 1;
      document.getElementById('rampPanel').style.display    = isRamp ? 'block' : 'none';
      document.getElementById('profilePanel').style.display = isRamp ? 'block' : 'none';
      document.getElementById('runModeSelect').value = isRamp ? 'autoramp' : 'bangbang';
    } else {
      if (wasRunActive && !st.runActive) {
        const wasRamp = (document.getElementById('runModeSelect').value === 'autoramp');
        rsEl.textContent = wasRamp
          ? '\u2705 Auto-Ramp run complete.'
          : '\u25A0 Run stopped.';
        rsEl.style.color = wasRamp ? '#0f9' : '#aaa';
      } else {
        rsEl.textContent = 'No run active.';
        rsEl.style.color = '#aaa';
      }
    }
    wasRunActive = st.runActive;

    if (st.cjcC   !== undefined) document.getElementById('cjcC').textContent    = st.cjcC.toFixed(1);
    if (st.shuntMV!== undefined) document.getElementById('shuntMV').textContent = st.shuntMV.toFixed(4);
    if (st.totalMV!== undefined) document.getElementById('totalMV').textContent = st.totalMV.toFixed(4);

    if (!document.getElementById('cfgSp').value) {
      document.getElementById('cfgSp').value   = st.setpoint;
      document.getElementById('cfgHyst').value = st.hysteresis;
      document.getElementById('cfgOff').value  = st.offset;
      const pEl = document.getElementById('cfgPtype');
      if (pEl) { const pVal = (st.customUvPerC > 0) ? 2 : st.probeType; pEl.value = pVal; updateCalMode(pVal); }
    }
    if (!document.getElementById('cfgCjco').value && document.getElementById('cfgCjco').value !== '0')
      document.getElementById('cfgCjco').value = st.cjcOffset;

    const crEl = document.getElementById('calResult');
    if (st.customUvPerC > 0)
      crEl.textContent = 'Calibrated uV/\u00B0C = ' + st.customUvPerC.toFixed(4) + ', Offset = ' + st.offset.toFixed(4);
    else
      crEl.textContent = '';

    if (document.getElementById('calMv1').value === '') {
      document.getElementById('calMv1').value   = st.calMv1;
      document.getElementById('calCjc1').value  = st.calCjc1;
      document.getElementById('calTemp1').value = st.calTemp1;
      document.getElementById('calMv2').value   = st.calMv2;
      document.getElementById('calCjc2').value  = st.calCjc2;
      document.getElementById('calTemp2').value = st.calTemp2;
    }
    drawChart(hist, currentSetpoint);

    // Sync log if run active
    if (st.runActive) syncLog();
    // Ramp status polling (Stage 2) - only in autoramp mode when run is active
    if (st.runActive && document.getElementById('runModeSelect').value === 'autoramp') {
      pollRamp();
    }
  } catch(e) { console.warn('poll error', e); }
}

async function pollRamp() {
  try {
    const rs = await fetch('/rampstatus').then(r => r.json());
    // Map state name
    const stateName = (rs.rampState >= 0 && rs.rampState < RAMP_STATE_NAMES.length)
      ? RAMP_STATE_NAMES[rs.rampState]
      : String(rs.rampState);
    const isOvershoot = rs.rampState === 4;
    const stateEl = document.getElementById('rsState');
    if (stateEl) stateEl.textContent = stateName;
    if (stateEl) stateEl.style.color = isOvershoot ? '#e74c3c' : '#eee';

    const stepEl = document.getElementById('rsStep');
    if (stepEl) stepEl.textContent = (rs.rampStep + 1) + ' of ' + rs.stepCount + '  (' + rs.stepTarget.toFixed(1) + '\u00B0C)';
    const targetEl = document.getElementById('rsTarget');
    if (targetEl) targetEl.textContent = rs.stepTarget.toFixed(1) + '\u00B0C';
    const predEl = document.getElementById('rsPredPeak');
    if (predEl) predEl.textContent = rs.rampState === 1 ? rs.predictedPeak.toFixed(1) + '\u00B0C' : '--';
    const overEl = document.getElementById('rsOvershoot');
    if (rs.overshootAmt > 0) {
      overEl.textContent = '+' + rs.overshootAmt.toFixed(1) + '\u00B0C \u26A0';
      overEl.style.color = '#e74c3c';
    } else {
      overEl.textContent = 'none';
      overEl.style.color = '#0f9';
    }

    const coastCur = document.getElementById('rsCoastCur');
    if (coastCur) {
      const curRatio = rs.coastBase - rs.coastSlope * (rs.stepTarget / 100.0);
      coastCur.textContent = curRatio.toFixed(3);
    }
    const coastModel = document.getElementById('rsCoastModel');
    if (coastModel) coastModel.textContent = 'base=' + rs.coastBase.toFixed(3) + '  slope=' + rs.coastSlope.toFixed(3) + '/100°C';

    const ageEl = document.getElementById('rsAge');
    if (ageEl) {
      const ageS = Math.floor(rs.stateAgeMs / 1000);
      ageEl.textContent = Math.floor(ageS/60) + ':' + String(ageS%60).padStart(2,'0');
    }

    const soakEl = document.getElementById('rsSoakRemain');
    if (soakEl) soakEl.textContent = rs.rampState === 5 ? Math.floor(rs.soakRemainS/60) + ':' + String(rs.soakRemainS%60).padStart(2,'0') + ' remaining' : '--';

    // Learned steps table
    const tbody = document.getElementById('learnedBody');
    if (tbody) {
      tbody.innerHTML = '';
      if (rs.learned && Array.isArray(rs.learned)) {
        rs.learned.forEach((s, idx) => {
          const tr = document.createElement('tr');
          tr.innerHTML = `<td>${idx + 1}</td>`
            + `<td style="text-align:right">${s.fireStart.toFixed(0)}\u00B0C</td>`
            + `<td style="text-align:right">${s.cutoff.toFixed(0)}\u00B0C</td>`
            + `<td style="text-align:right;color:${(s.target !== undefined && s.peak > s.target+10) ? '#e74c3c':'#0f9'}">${s.peak.toFixed(0)}\u00B0C</td>`
            + `<td style="text-align:right">${s.coastRatio.toFixed(3)}</td>`;
          tbody.appendChild(tr);
        });
      }
      // Fill blanks for remaining steps if needed
      for (let i = rs.learnedCount; i < rs.stepCount - 1; i++) {
        const tr = document.createElement('tr');
        tr.innerHTML = `<td style="color:#555">${i+1}</td>` + `<td colspan="4" style="color:#555;text-align:right">--</td>`;
        tbody.appendChild(tr);
      }
    }
  } catch(e) { console.warn('pollRamp error', e); }
}

setInterval(poll, 1000);
openDB().then(() => { updateLogInfo(); loadProfileFromDevice(); poll(); });

// ── Config form ───────────────────────────────────────────────────────────────
document.getElementById('cfgForm').addEventListener('submit', async e => {
  e.preventDefault();
  await fetch('/config', {method:'POST', body: new URLSearchParams(new FormData(e.target))});
  alert('Saved!'); poll();
});

// ── Calibration form ──────────────────────────────────────────────────────────
document.getElementById('calForm').addEventListener('submit', async e => {
  e.preventDefault();
  try {
    const r = await fetch('/calibrate', {method:'POST', body: new URLSearchParams(new FormData(e.target))});
    if (!r.ok) throw new Error(await r.text());
    const data = await r.json();
    document.getElementById('calResult').textContent = 'Calibrated uV/\u00B0C = ' + data.uvPerC.toFixed(4) + ', Offset = ' + data.offset.toFixed(4);
    poll();
  } catch(err) {
    document.getElementById('calResult').textContent = 'Calibration failed: ' + err;
  }
});

// ── WiFi form ─────────────────────────────────────────────────────────────────
document.getElementById('wifiForm').addEventListener('submit', async e => {
  e.preventDefault();
  await fetch('/wifi', {method:'POST', body: new URLSearchParams(new FormData(e.target))});
  alert('WiFi saved. Device will reboot.');
});

// ── Helpers ───────────────────────────────────────────────────────────────────
async function clearCalibration() {
  try {
    const r = await fetch('/calibrate/clear', {method:'POST'});
    if (!r.ok) throw new Error(await r.text());
    document.getElementById('calResult').textContent = 'Calibration cleared';
    document.getElementById('calForm').reset();
    poll();
  } catch(err) { document.getElementById('calResult').textContent = 'Clear failed: ' + err; }
}

function setOutput(mode) {
  fetch('/output', {method:'POST', body: new URLSearchParams({mode})})
    .then(r => { if (!r.ok) throw new Error('HTTP ' + r.status); })
    .then(() => poll())
    .catch(e => console.warn('output error', e));
}

function updateCalMode(val) {
  const el = document.getElementById('calInputs');
  if (el) el.style.display = (val == '2') ? 'block' : 'none';
}
</script>
</body>
</html>
)rawhtml";
