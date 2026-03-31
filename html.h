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

<div class="card" id="stopPanel">
  <div style="display:flex;gap:.5rem;align-items:center;flex-wrap:wrap;">
    <button id="btnStop" onclick="toggleStop()" style="background:#e74c3c;">&#x26A0; STOP</button>
    <span id="stopStatus" style="font-size:.9rem;color:#aaa;">Normal operation</span>
  </div>
</div>

<div class="card">
  <h2>&#x1F321; Live Temperature</h2>
  <div class="big">Temp: <span id="temp">--</span> &deg;C</div>
  <div>Setpoint: <span id="sp">--</span> &deg;C &nbsp; Output: <span id="out" class="badge">--</span></div>
  <div style="margin-top:.5rem;font-size:.9rem;color:#aaa;">
    <div>CJC (board): <span id="cjcC">--</span> &deg;C</div>
    <div>Shunt mV (raw): <span id="shuntMV">--</span> mV</div>
    <div>Total mV (with CJC): <span id="totalMV">--</span> mV</div>
  </div>
  <div id="manualControls" style="margin-top:.8rem;display:none;gap:.5rem;flex-wrap:wrap;">
    <button onclick="setManual('on')">Manual ON</button>
    <button onclick="setManual('off')" style="background:#555;">Manual OFF</button>
  </div>
</div>

<div class="card">
  <h2>&#x25B6; Run Control</h2>
  <div style="display:flex;gap:.5rem;align-items:flex-end;flex-wrap:wrap;">
    <div style="flex:1;min-width:140px;">
      <label style="margin-bottom:.3rem;">Mode
        <select id="runModeSelect">
          <option value="manual">Manual (On/Off)</option>
          <option value="bangbang">Bang-Bang</option>
          <option value="autoramp">Auto-Ramp</option>
        </select>
      </label>
    </div>
    <button onclick="startRun()" style="background:#2ecc71;color:#111;">&#x25B6; Start</button>
    <button onclick="stopRun()"  style="background:#e74c3c;">&#x25A0; Stop</button>
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

  <div class="card" id="profileLibPanel" style="display:none;">
    <h2>&#x1F4DA; Profile Library</h2>
    <div style="display:flex;gap:.5rem;align-items:flex-end;flex-wrap:wrap;margin-bottom:.6rem;">
      <div style="flex:1;min-width:160px;">
        <label style="margin-bottom:.3rem;">Saved Profiles
          <select id="profileLibSelect" style="width:100%;">
            <option value="">-- select --</option>
          </select>
        </label>
      </div>
      <button onclick="loadSelectedProfile()" style="background:#2980b9;">&#x21A9; Load</button>
      <button onclick="deleteSelectedProfile()" style="background:#a33;">&#x1F5D1; Delete</button>
    </div>
    <div style="display:flex;gap:.5rem;flex-wrap:wrap;">
      <input type="text" id="saveProfileName" placeholder="Save current as..." style="flex:1;min-width:140px;">
      <button onclick="saveCurrentProfile()" style="background:#2ecc71;color:#111;">&#x1F4BE; Save</button>
    </div>
    <div id="profileLibMsg" style="margin-top:.4rem;font-size:.85rem;color:#0f9;"></div>
  </div>

<div class="card">
  <h2>&#x1F4CA; Run Chart</h2>
  <canvas id="runChart" style="width:100%;height:220px;display:block;"></canvas>
  <div id="chartLegend" style="font-size:.8rem;color:#aaa;margin-top:.3rem;display:flex;gap:1rem;flex-wrap:wrap;">
    <span style="color:#0f9;">&#9644; Temperature</span>
    <span style="color:#e94560;">&#9644; Setpoint</span>
    <span style="color:#f39c12;">&#9644; Step Targets</span>
  </div>
  <div id="chartMsg" style="font-size:.85rem;color:#aaa;margin-top:.3rem;">Waiting for run data...</div>
</div>

<div class="card" id="runSummaryPanel" style="display:none;">
  <h2>&#x1F3C1; Run Summary</h2>
  <div id="runSummaryContent">
    <table style="width:100%;font-size:.85rem;border-collapse:collapse;">
      <thead><tr>
        <th style="text-align:left;color:#aaa;">Step</th>
        <th style="text-align:right;color:#aaa;">Target</th>
        <th style="text-align:right;color:#aaa;">Peak</th>
        <th style="text-align:right;color:#aaa;">Overshoot</th>
        <th style="text-align:right;color:#aaa;">CoastR</th>
        <th style="text-align:right;color:#aaa;">FireStart</th>
        <th style="text-align:right;color:#aaa;">Cutoff</th>
      </tr></thead>
      <tbody id="summaryBody"></tbody>
    </table>
    <div id="summaryFooter" style="margin-top:.5rem;font-size:.85rem;color:#aaa;"></div>
  </div>
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
  <div style="margin-top:.8rem;border-top:1px solid #333;padding-top:.6rem;">
    <strong style="font-size:.85rem;color:#aaa;">&#x1F512; Change Web Password</strong>
    <label style="margin-top:.4rem;">Username
      <input type="text" id="authUser" autocomplete="username" placeholder="admin">
    </label>
    <label>New Password
      <input type="password" id="authPass" autocomplete="new-password" placeholder="new password">
    </label>
    <button onclick="changeAuth()" style="background:#e94560;margin-top:.4rem;">Update Credentials</button>
    <div id="authMsg" style="font-size:.85rem;color:#0f9;margin-top:.3rem;"></div>
  </div>
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
    drawRunChart();
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
  document.getElementById('rampPanel').style.display       = isRamp ? 'block' : 'none';
  document.getElementById('profilePanel').style.display    = isRamp ? 'block' : 'none';
  document.getElementById('profileLibPanel').style.display = isRamp ? 'block' : 'none';
  if (isRamp) refreshProfileList();
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
    runChartStepTargets = Array.isArray(p.stepTargets) ? p.stepTargets : [];
    drawRunChart();
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

// ── Profile library ───────────────────────────────────────────────────────────
async function refreshProfileList(activeProfileName) {
  try {
    const names = await fetch('/profiles').then(r => r.json());
    const sel   = document.getElementById('profileLibSelect');
    const cur   = sel.value;
    sel.innerHTML = '<option value="">-- select --</option>';
    names.forEach(n => {
      const opt = document.createElement('option');
      opt.value = n; opt.textContent = n;
      if (n === cur) opt.selected = true;
      sel.appendChild(opt);
    });
    const nameInput = document.getElementById('saveProfileName');
    if (!nameInput.value && activeProfileName) {
      nameInput.value = activeProfileName;
    }
  } catch(e) {
    document.getElementById('profileLibMsg').textContent = 'List failed: ' + e;
  }
}

async function loadSelectedProfile() {
  const name = document.getElementById('profileLibSelect').value;
  if (!name) { alert('Select a profile first.'); return; }
  try {
    const r = await fetch('/profiles/load', {
      method: 'POST',
      body: new URLSearchParams({name})
    });
    if (!r.ok) throw new Error(await r.text());
    await loadProfileFromDevice();
    document.getElementById('profileLibMsg').textContent = 'Loaded: ' + name;
  } catch(e) {
    document.getElementById('profileLibMsg').textContent = 'Load failed: ' + e;
  }
}

async function saveCurrentProfile() {
  const nameInput = document.getElementById('saveProfileName');
  const name = nameInput.value.trim();
  if (!name) { alert('Enter a profile name to save.'); return; }
  try {
    const r = await fetch('/profiles/save', {
      method: 'POST',
      body: new URLSearchParams({name})
    });
    if (!r.ok) throw new Error(await r.text());
    const result = await r.json();
    document.getElementById('profileLibMsg').textContent = 'Saved: ' + result.saved;
    await refreshProfileList();
    document.getElementById('profName').value = result.saved;
  } catch(e) {
    document.getElementById('profileLibMsg').textContent = 'Save failed: ' + e;
  }
}

async function deleteSelectedProfile() {
  const name = document.getElementById('profileLibSelect').value;
  if (!name) { alert('Select a profile to delete.'); return; }
  if (!confirm('Delete profile "' + name + '"?')) return;
  try {
    const r = await fetch('/profiles/delete', {
      method: 'POST',
      body: new URLSearchParams({name})
    });
    if (!r.ok) throw new Error(await r.text());
    document.getElementById('profileLibMsg').textContent = 'Deleted: ' + name;
    await refreshProfileList();
  } catch(e) {
    document.getElementById('profileLibMsg').textContent = 'Delete failed: ' + e;
  }
}

async function startRun() {
  const mode = document.getElementById('runModeSelect').value;
  const rows = await dbGetAll();
  if (rows.length > 0) {
    if (!confirm('Starting a new run will clear ' + rows.length + ' stored log rows from the previous run. Continue?')) return;
  }
  document.getElementById('runSummaryPanel').style.display = 'none';
  document.getElementById('summaryBody').innerHTML = '';
  runChartStepTargets = [];
  runChartCurrentStep = 0;
  await dbClear();
  drawRunChart();
  await fetch('/run', {method:'POST', body: new URLSearchParams({mode: mode, action:'start'})});
}

async function stopRun() {
  await fetch('/run', {method:'POST', body: new URLSearchParams({action:'stop'})});
}

// ── Run chart ─────────────────────────────────────────────────────────────────
let runChartStepTargets = [];
let runChartCurrentStep = 0;

function drawRunChart() {
  dbGetAll().then(rows => {
    const c   = document.getElementById('runChart');
    if (!c) return;
    const w = c.offsetWidth || 300;
    c.width  = w;
    c.height = 220;
    const h = c.height;
    const ctx = c.getContext('2d');
    ctx.clearRect(0, 0, w, h);

    const msg = document.getElementById('chartMsg');
    if (!rows.length) {
      msg.textContent = 'Waiting for run data...';
      return;
    }
    msg.textContent = rows.length + ' samples \u2022 ' + (rows[rows.length-1].t_s) + 's elapsed';

    const temps = rows.map(r => r.tempC);
    const sps   = rows.map(r => r.setpoint);
    const times = rows.map(r => r.t_s);

    const allVals = [...temps, ...sps, ...runChartStepTargets];
    let yMin = Math.min(...allVals) - 15;
    let yMax = Math.max(...allVals) + 15;
    if (yMax - yMin < 50) yMax = yMin + 50;

    const tMin  = 0;
    const tMax  = Math.max(...times, 60);

    const px = t => Math.round((t - tMin) / (tMax - tMin) * (w - 1));
    const py = v => Math.round(h - (v - yMin) / (yMax - yMin) * (h - 1));

    ctx.setLineDash([4, 6]);
    ctx.lineWidth = 1;
    runChartStepTargets.forEach((tgt, i) => {
      const isActive = (i === runChartCurrentStep);
      ctx.strokeStyle = isActive ? '#f39c12' : 'rgba(243,156,18,0.35)';
      ctx.beginPath();
      ctx.moveTo(0, py(tgt));
      ctx.lineTo(w, py(tgt));
      ctx.stroke();
      ctx.fillStyle = isActive ? '#f39c12' : 'rgba(243,156,18,0.5)';
      ctx.font = '10px sans-serif';
      ctx.fillText(tgt.toFixed(0) + '\u00B0C', w - 38, py(tgt) - 3);
    });
    ctx.setLineDash([]);

    if (sps.length > 1) {
      ctx.strokeStyle = '#e94560';
      ctx.lineWidth = 1;
      ctx.setLineDash([5, 5]);
      ctx.beginPath();
      sps.forEach((sp, i) => {
        const x = px(times[i]), y = py(sp);
        i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
      });
      ctx.stroke();
      ctx.setLineDash([]);
    }

    ctx.strokeStyle = '#0f9';
    ctx.lineWidth = 2;
    ctx.beginPath();
    temps.forEach((t, i) => {
      const x = px(times[i]), y = py(t);
      i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    });
    ctx.stroke();

    ctx.fillStyle = '#666';
    ctx.font = '10px sans-serif';
    for (let i = 0; i <= 4; i++) {
      const v = yMin + (yMax - yMin) * (i / 4);
      const y = py(v);
      ctx.fillText(v.toFixed(0), 2, y - 2);
    }
  });
}

async function showRunSummary(learnedSteps, stepTargets) {
  const panel = document.getElementById('runSummaryPanel');
  const tbody = document.getElementById('summaryBody');
  const footer = document.getElementById('summaryFooter');
  if (!panel || !tbody) return;

  tbody.innerHTML = '';
  if (!learnedSteps || !learnedSteps.length) {
    panel.style.display = 'none';
    return;
  }

  learnedSteps.forEach((s, i) => {
    const tgt      = (stepTargets && stepTargets[i] !== undefined) ? stepTargets[i] : s.target || '--';
    const overshootVal = (typeof s.peak === 'number' && typeof tgt === 'number')
      ? (s.peak - tgt)
      : null;
    const overshoot = overshootVal !== null ? overshootVal.toFixed(1) : '--';
    const ovColor   = overshootVal === null ? '#aaa'
      : overshootVal > 10 ? '#e74c3c'
      : overshootVal > 3  ? '#f39c12'
      : '#0f9';
    const tr = document.createElement('tr');
    tr.innerHTML = `<td>${i + 1}</td>`
      + `<td style="text-align:right">${typeof tgt === 'number' ? tgt.toFixed(0) : tgt}&deg;C</td>`
      + `<td style="text-align:right">${s.peak.toFixed(0)}&deg;C</td>`
      + `<td style="text-align:right;color:${ovColor}">${(overshootVal !== null && overshootVal > 0) ? '+' : ''}${overshoot}&deg;C</td>`
      + `<td style="text-align:right">${s.coastRatio.toFixed(3)}</td>`
      + `<td style="text-align:right">${s.fireStart.toFixed(0)}&deg;C</td>`
      + `<td style="text-align:right">${s.cutoff.toFixed(0)}&deg;C</td>`;
    tbody.appendChild(tr);
  });

  const rows = await dbGetAll();
  if (rows.length) {
    const totalS = rows[rows.length-1].t_s;
    const m = Math.floor(totalS/60), s = totalS % 60;
    footer.textContent = 'Total run time: ' + m + ':' + String(s).padStart(2,'0')
      + ' \u2022 ' + rows.length + ' samples logged';
  }

  panel.style.display = 'block';
}

let currentSetpoint = 500;
let wasRunActive = false;
let wasSelectedMode = 0;
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
    const modeNames = ['MANUAL', 'BANG-BANG', 'AUTO RAMP'];
    if (st.stopLatched) {
      outEl.textContent = 'E-STOP';
      outEl.className   = 'badge off';
    } else if (st.selectedMode === 0) {
      outEl.textContent = st.output ? 'ON' : 'OFF';
      outEl.className   = 'badge ' + (st.output ? 'on' : 'off');
    } else if (st.modeRunning) {
      outEl.textContent = st.output ? 'ON' : 'OFF';
      outEl.className   = 'badge ' + (st.output ? 'on' : 'off');
    } else {
      outEl.textContent = 'IDLE';
      outEl.className   = 'badge off';
    }
    currentSetpoint = st.setpoint;

    // E-stop panel
    const stopBtn = document.getElementById('btnStop');
    const stopStatus = document.getElementById('stopStatus');
    if (st.stopLatched) {
      stopBtn.textContent = '\u21BA RELEASE STOP';
      stopBtn.style.background = '#f39c12';
      stopStatus.textContent = 'E-STOP LATCHED - heater disabled';
      stopStatus.style.color = '#e74c3c';
    } else {
      stopBtn.textContent = '\u26A0 STOP';
      stopBtn.style.background = '#e74c3c';
      stopStatus.textContent = 'Normal operation';
      stopStatus.style.color = '#aaa';
    }

    // Manual mode controls visibility
    const manualControls = document.getElementById('manualControls');
    manualControls.style.display = (st.selectedMode === 0 && !st.stopLatched) ? 'flex' : 'none';

    // Mode selector
    const modeValues = ['manual', 'bangbang', 'autoramp'];
    document.getElementById('runModeSelect').value = modeValues[st.selectedMode] || 'bangbang';

    // Run status line
    const rsEl = document.getElementById('runStatus');
    if (st.runActive) {
      const mLabel = modeNames[st.selectedMode];
      const elapsed = st.runElapsed || 0;
      const m = Math.floor(elapsed/60), s = elapsed%60;
      rsEl.textContent = '\u25CF Running \u2022 ' + mLabel +
        ' \u2022 Elapsed: ' + m + ':' + String(s).padStart(2,'0');
      rsEl.style.color = '#2ecc71';

      const isRamp = st.selectedMode === 2;
      document.getElementById('rampPanel').style.display       = isRamp ? 'block' : 'none';
      document.getElementById('profilePanel').style.display    = isRamp ? 'block' : 'none';
      document.getElementById('profileLibPanel').style.display = isRamp ? 'block' : 'none';
    } else {
      if (wasRunActive) {
        const wasRamp = wasSelectedMode === 2;
        rsEl.textContent = wasRamp
          ? '\u2705 Auto-Ramp run complete.'
          : '\u25A0 Run stopped.';
        rsEl.style.color = wasRamp ? '#0f9' : '#aaa';

        if (wasRamp) {
          fetch('/rampstatus').then(r => r.json()).then(rs => {
            showRunSummary(rs.learned, runChartStepTargets);
            drawRunChart();
          }).catch(() => {});
        }
      } else {
        rsEl.textContent = 'No run active.';
        rsEl.style.color = '#aaa';
      }
    }
    wasRunActive = st.runActive;
    wasSelectedMode = st.selectedMode;

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
    if (st.runActive) syncLog();
    if (st.runActive && st.selectedMode === 2) {
      pollRamp();
    }
  } catch(e) { console.warn('poll error', e); }
}

async function pollRamp() {
  try {
    const rs = await fetch('/rampstatus').then(r => r.json());

    if (rs.stepCount && rs.stepCount > 0) {
      runChartCurrentStep = rs.rampStep;
    }
    drawRunChart();

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
openDB().then(() => {
  updateLogInfo();
  loadProfileFromDevice().then(() => {
    refreshProfileList(document.getElementById('profName').value);
  });
  poll();
});

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

async function changeAuth() {
  const user = document.getElementById('authUser').value.trim();
  const pass = document.getElementById('authPass').value.trim();
  if (!user || !pass) { alert('Both username and password are required.'); return; }
  try {
    const r = await fetch('/auth', {
      method: 'POST',
      body: new URLSearchParams({ user, pass })
    });
    if (!r.ok) throw new Error(await r.text());
    document.getElementById('authMsg').textContent = 'Credentials updated. Re-login required on next action.';
    document.getElementById('authPass').value = '';
  } catch(e) {
    document.getElementById('authMsg').textContent = 'Failed: ' + e;
  }
}

async function toggleStop() {
  try {
    const resp = await fetch('/status').then(r => r.json());
    const action = resp.stopLatched ? 'release' : 'latch';
    await fetch('/stop', {method:'POST', body: new URLSearchParams({action})});
    poll();
  } catch(e) {
    console.warn('stop error', e);
  }
}

function setManual(action) {
  fetch('/manual', {method:'POST', body: new URLSearchParams({action})})
    .then(r => { if (!r.ok) throw new Error('HTTP ' + r.status); })
    .then(() => poll())
    .catch(e => console.warn('manual error', e));
}

function updateCalMode(val) {
  const el = document.getElementById('calInputs');
  if (el) el.style.display = (val == '2') ? 'block' : 'none';
}
</script>

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
</body>
</html>
)rawhtml";
