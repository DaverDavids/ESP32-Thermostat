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
  .card{background:#16213e;border-radius:8px;padding:1rem;margin-bottom:0}
  .big{font-size:2.2rem;font-weight:bold}
  .big span{color:#e94560}
  label{display:block;margin:.4rem 0 .1rem}
  input,select{width:100%;padding:.4rem;border-radius:4px;border:none;background:#0f3460;color:#eee;box-sizing:border-box}
  button{margin-top:.6rem;padding:.5rem 1.2rem;background:#e94560;color:#fff;border:none;border-radius:4px;cursor:pointer}
  canvas{width:100%!important;max-height:200px}
  .badge{display:inline-block;padding:.2rem .6rem;border-radius:4px;font-size:.85rem}
  .on{background:#2ecc71;color:#111}.off{background:#e74c3c}.auto{background:#2980b9}
  #wifiSection{display:none}
  #logInfo{font-size:.85rem;color:#aaa;margin-top:.4rem}

  /* ── Layout grid ── */
  .row{display:grid;gap:.75rem;margin-bottom:.75rem}
  .row-2{grid-template-columns:1fr 1fr}
  .row-3{grid-template-columns:repeat(auto-fill, minmax(220px, 1fr))}
  .row-1{grid-template-columns:1fr}

  @media(max-width:600px){
    .row-2,.row-3{grid-template-columns:1fr}
  }

  /* ── Ramp status value cells ── */
  .rs-val{font-size:1.55rem;font-weight:bold;color:#e94560;line-height:1.15}
  .rs-val.good{color:#2ecc71}
  .rs-val.warn{color:#f39c12}
  .rs-val.alert{color:#e74c3c}
  .rs-grid{
    display:grid;
    grid-template-columns:repeat(auto-fill,minmax(140px,1fr));
    gap:.5rem .75rem;
    margin-bottom:.6rem;
  }
  .rs-cell{}
  .rs-label{font-size:.78rem;color:#aaa;margin-bottom:.15rem}
</style>
</head>
<body>

<!-- ── E-stop bar ─────────────────────────────────────────────────────────── -->
<div class="row row-1" style="margin-bottom:.75rem">
  <div class="card" id="stopPanel">
    <div style="display:flex;gap:.5rem;align-items:center;flex-wrap:wrap;">
      <button id="btnStop" onclick="toggleStop()" style="background:#e74c3c;">&#x26A0; STOP</button>
      <span id="stopStatus" style="font-size:.9rem;color:#aaa;">Normal operation</span>
    </div>
  </div>
</div>

<!-- ── Row 1: Live Temp + Run Control ────────────────────────────────────── -->
<div class="row row-2">
  <div class="card">
    <h2>&#x1F321; Live Temperature</h2>
    <div class="big">Temp: <span id="temp">--</span> &deg;C</div>
    <div style="margin-top:.4rem">Setpoint: <span id="sp">--</span> &deg;C &nbsp; Output: <span id="out" class="badge">--</span></div>
    <div id="manualControls" style="margin-top:.8rem;display:none;gap:.5rem;flex-wrap:wrap;">
      <button onclick="setManual('on')">Manual ON</button>
      <button onclick="setManual('off')" style="background:#555;">Manual OFF</button>
    </div>
  </div>

  <div class="card">
    <h2>&#x25B6; Run Control</h2>
    <div style="display:flex;gap:.5rem;align-items:flex-end;flex-wrap:wrap;">
      <div style="flex:1;min-width:120px;">
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
</div>

<!-- ── Row 2: Auto-Ramp Status (full width) ───────────────────────────────── -->
<div class="row row-1">
  <div class="card" id="rampPanel" style="display:none;">
    <h2>&#x1F525; Auto-Ramp Status</h2>
    <div class="rs-grid">
      <div class="rs-cell">
        <div class="rs-label">State</div>
        <div class="rs-val" id="rsState">--</div>
      </div>
      <div class="rs-cell">
        <div class="rs-label">Step</div>
        <div class="rs-val" id="rsStep">--</div>
      </div>
      <div class="rs-cell">
        <div class="rs-label">Step Target</div>
        <div class="rs-val" id="rsTarget">--</div>
      </div>
      <div class="rs-cell">
        <div class="rs-label">Predicted Peak</div>
        <div class="rs-val" id="rsPredPeak">--</div>
      </div>
      <div class="rs-cell">
        <div class="rs-label">Overshoot</div>
        <div class="rs-val" id="rsOvershoot">--</div>
      </div>
      <div class="rs-cell">
        <div class="rs-label">Coast Ratio (cur est)</div>
        <div class="rs-val" id="rsCoastCur">--</div>
      </div>
      <div class="rs-cell">
        <div class="rs-label">Coast Model</div>
        <div class="rs-val" style="font-size:1.1rem" id="rsCoastModel">--</div>
      </div>
      <div class="rs-cell">
        <div class="rs-label">Time in State</div>
        <div class="rs-val" id="rsAge">--</div>
      </div>
      <div class="rs-cell">
        <div class="rs-label">Soak Remaining</div>
        <div class="rs-val" id="rsSoakRemain">--</div>
      </div>
    </div>
    <div style="margin-top:.4rem;">
      <strong style="color:#aaa;font-size:.85rem;">Learned Steps</strong>
      <table id="learnedTable" style="width:100%;font-size:.85rem;margin-top:.3rem;border-collapse:collapse;">
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
</div>

<!-- ── Row 3: Ramp Profile + Profile Library ─────────────────────────────── -->
<div class="row row-2">
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
      <div style="flex:1;min-width:120px;">
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
      <input type="text" id="saveProfileName" placeholder="Save current as..." style="flex:1;min-width:120px;">
      <button onclick="saveCurrentProfile()" style="background:#2ecc71;color:#111;">&#x1F4BE; Save</button>
    </div>
    <div id="profileLibMsg" style="margin-top:.4rem;font-size:.85rem;color:#0f9;"></div>
  </div>
</div>

<!-- ── Run Chart (full width) ─────────────────────────────────────────────── -->
<div class="row row-1">
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
</div>

<!-- ── Run Summary ─────────────────────────────────────────────────────────── -->
<div class="row row-1">
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
</div>

<!-- ── Data Log ─────────────────────────────────────────────────────────────── -->
<div class="row row-1">
  <div class="card">
    <h2>&#x1F4BE; Data Log</h2>
    <div id="logInfo">No run data in browser yet.</div>
    <div style="margin-top:.6rem;display:flex;gap:.5rem;flex-wrap:wrap;">
      <button onclick="exportCSV()" style="background:#2980b9;">&#x2B07; Export CSV</button>
      <button onclick="clearBrowserLog()" style="background:#555;">Clear Browser Log</button>
      <button onclick="rePullLog()" style="background:#555;">Re-pull from Device</button>
    </div>
  </div>
</div>

<!-- ── Row 4: Config + Web Password + WiFi ───────────────────────────────── -->
<div class="row row-3">
  <div class="card">
    <h2>&#x2699;&#xFE0F; Configuration</h2>
    <form id="cfgForm">
      <label>Setpoint (&deg;C)<input type="number" step="1" name="sp" id="cfgSp"></label>
      <label>Hysteresis (&deg;C)<input type="number" step="0.5" name="hyst" id="cfgHyst"></label>
      <label>Probe Offset (&deg;C)<input type="number" step="0.5" name="off" id="cfgOff"></label>
      <button type="submit">Save Config</button>
    </form>
  </div>

  <div class="card">
    <h2>&#x1F512; Web Password</h2>
    <label>Username
      <input type="text" id="authUser" autocomplete="username" placeholder="admin">
    </label>
    <label>New Password
      <input type="password" id="authPass" autocomplete="new-password" placeholder="new password">
    </label>
    <button onclick="changeAuth()" style="background:#e94560;margin-top:.4rem;">Update Credentials</button>
    <div id="authMsg" style="font-size:.85rem;color:#0f9;margin-top:.3rem;"></div>
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
</div>

<div style="height:.75rem"></div>

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
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 4000);
    const since = await dbGetMaxTs();
    lastSyncTs  = since;
    const resp  = await fetch('/log?since=' + since, { signal: controller.signal });
    const text  = await resp.text();
    clearTimeout(timeout);
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

// ── Panel visibility ───────────────────────────────────────────────────────────────
//
// Single source of truth: show/hide ramp panels based on the dropdown value.
// Called from both the dropdown's change handler and from poll().
// poll() must NOT override panel visibility if a run is not active — the
// user may have already switched the dropdown to autoramp before starting.
function syncPanelVisibility() {
  const isRamp = document.getElementById('runModeSelect').value === 'autoramp';
  document.getElementById('rampPanel').style.display       = isRamp ? 'block' : 'none';
  document.getElementById('profilePanel').style.display    = isRamp ? 'block' : 'none';
  document.getElementById('profileLibPanel').style.display = isRamp ? 'block' : 'none';
}

// ── Run control ───────────────────────────────────────────────────────────────
document.getElementById('runModeSelect').addEventListener('change', function() {
  syncPanelVisibility();
  if (this.value === 'autoramp') refreshProfileList();
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
  // Reset UI run-tracking state BEFORE the fetch so a cancelled confirm
  // doesn't leave wasRunActive in a stuck state.
  wasRunActive    = false;
  wasSelectedMode = ['manual','bangbang','autoramp'].indexOf(mode);

  document.getElementById('runSummaryPanel').style.display = 'none';
  document.getElementById('summaryBody').innerHTML = '';
  runChartStepTargets = [];
  runChartCurrentStep = 0;
  await dbClear();
  drawRunChart();
  try {
    const r = await fetch('/run', {method:'POST', body: new URLSearchParams({mode, action:'start'})});
    if (!r.ok) {
      const msg = await r.text();
      document.getElementById('runStatus').textContent = 'Start failed: ' + msg;
      document.getElementById('runStatus').style.color = '#e74c3c';
    }
  } catch(e) {
    document.getElementById('runStatus').textContent = 'Start failed: ' + e;
    document.getElementById('runStatus').style.color = '#e74c3c';
  }
}

async function stopRun() {
  try {
    await fetch('/run', {method:'POST', body: new URLSearchParams({action:'stop'})});
  } catch(e) { console.warn('stopRun error', e); }
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

// ── Poll ─────────────────────────────────────────────────────────────────────
//
// FIX 1: poll() no longer unconditionally overwrites runModeSelect.
//         It only syncs the dropdown when a run is actually active — so
//         the user can freely change the mode selector between runs without
//         poll() snapping it back to whatever the device last reported.
//
// FIX 2: syncPanelVisibility() is called based on the dropdown value,
//         not on st.selectedMode. This means ramp panels are visible
//         whenever the user has autoramp selected, even before Start is hit.
//
// FIX 3: wasRunActive is explicitly set to false in the run-just-ended
//         branch (after we fire the summary fetch), so subsequent polls
//         stop seeing "run just ended" and the Start button is usable again.

let wasRunActive    = false;
let wasSelectedMode = 0;
const RAMP_STATE_NAMES = [ 'IDLE', 'HEATING', 'COASTING', 'SOAKING', 'OVERSHOOT WAIT', 'FINAL SOAK', 'DONE' ];

let pollInFlight = false;
async function poll() {
  if (pollInFlight) return;
  pollInFlight = true;
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 4000);
  try {
    const [st] = await Promise.all([
      fetch('/status', { signal: controller.signal }).then(r=>r.json()),
      fetch('/history', { signal: controller.signal }).then(r=>r.json())
    ]);
    clearTimeout(timeout);
    document.getElementById('temp').textContent = st.temp.toFixed(1);
    document.getElementById('sp').textContent   = st.setpoint.toFixed(1);
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

    // E-stop panel
    const stopBtn    = document.getElementById('btnStop');
    const stopStatus = document.getElementById('stopStatus');
    if (st.stopLatched) {
      stopBtn.textContent      = '\u21BA RELEASE STOP';
      stopBtn.style.background = '#f39c12';
      stopStatus.textContent   = 'E-STOP LATCHED - heater disabled';
      stopStatus.style.color   = '#e74c3c';
    } else {
      stopBtn.textContent      = '\u26A0 STOP';
      stopBtn.style.background = '#e74c3c';
      stopStatus.textContent   = 'Normal operation';
      stopStatus.style.color   = '#aaa';
    }

    // Manual mode controls visibility
    const manualControls = document.getElementById('manualControls');
    manualControls.style.display = (st.selectedMode === 0 && !st.stopLatched) ? 'flex' : 'none';

    // FIX 1: Only sync the dropdown when a run is active (device is authoritative).
    // When idle, leave the dropdown alone so the user can pre-select a mode.
    if (st.runActive) {
      const modeValues = ['manual', 'bangbang', 'autoramp'];
      document.getElementById('runModeSelect').value = modeValues[st.selectedMode] || 'bangbang';
    }

    // FIX 2: Panel visibility is always driven by the dropdown, not st.runActive.
    syncPanelVisibility();

    // Run status line
    const rsEl = document.getElementById('runStatus');
    if (st.runActive) {
      const mLabel  = modeNames[st.selectedMode];
      const elapsed = st.runElapsed || 0;
      const m = Math.floor(elapsed / 60), s = elapsed % 60;
      rsEl.textContent = '\u25CF Running \u2022 ' + mLabel +
        ' \u2022 Elapsed: ' + m + ':' + String(s).padStart(2, '0');
      rsEl.style.color = '#2ecc71';
    } else {
      if (wasRunActive) {
        // FIX 3: run just ended — fire summary, then clear the flag immediately
        // so the next poll() doesn't re-enter this branch.
        const wasRamp = wasSelectedMode === 2;
        wasRunActive    = false;   // ← clear BEFORE the async fetch so it can't re-trigger
        wasSelectedMode = st.selectedMode;

        rsEl.textContent = wasRamp ? '\u2705 Auto-Ramp run complete.' : '\u25A0 Run stopped.';
        rsEl.style.color = wasRamp ? '#0f9' : '#aaa';

        if (wasRamp) {
          fetch('/rampstatus').then(r => r.json()).then(rs => {
            showRunSummary(rs.learned, runChartStepTargets);
            drawRunChart();
          }).catch(() => {});
        }
      } else if (rsEl.textContent === 'No run active.' || rsEl.textContent === '') {
        rsEl.textContent = 'No run active.';
        rsEl.style.color = '#aaa';
      }
      // If we previously showed a "run complete" or "run stopped" message, leave it
      // visible — don't overwrite it with "No run active." on the next tick.
    }

    // Track previous run state for next poll
    if (st.runActive) {
      wasRunActive    = true;
      wasSelectedMode = st.selectedMode;
    }

    if (!document.getElementById('cfgSp').value) {
      document.getElementById('cfgSp').value   = st.setpoint;
      document.getElementById('cfgHyst').value = st.hysteresis;
      document.getElementById('cfgOff').value  = st.offset;
    }

    // Update duty cycle display from status
    const dcStatusEl = document.getElementById('dcStatus');
    if (dcStatusEl) {
      const usedS   = (st.dcOnTimeMs  / 1000).toFixed(1);
      const limitS  = ((st.dcPct / 100) * st.dcPeriodMs / 1000).toFixed(1);
      const remS    = (st.dcRemainingMs / 1000).toFixed(1);
      dcStatusEl.innerHTML = 'Used: ' + usedS + 's / ' + limitS + 's &bull; Remaining: ' + remS + 's'
        + (st.dcForceOff ? ' <span class="badge off">DC LIMIT</span>' : '');
    }
    // Only overwrite input fields when device value actually changed (not user edits)
    const dcPctEl = document.getElementById('dcPct');
    const dcPerEl = document.getElementById('dcPeriodMs');
    if (dcPctEl && dcPctEl.dataset.lastDevice !== String(st.dcPct.toFixed(0))) {
      dcPctEl.value = st.dcPct.toFixed(0);
      dcPctEl.dataset.lastDevice = st.dcPct.toFixed(0);
    }
    if (dcPerEl && dcPerEl.dataset.lastDevice !== String(st.dcPeriodMs)) {
      dcPerEl.value = st.dcPeriodMs;
      dcPerEl.dataset.lastDevice = String(st.dcPeriodMs);
    }

    if (st.runActive) syncLog();
    if (st.runActive && st.selectedMode === 2) pollRamp();
  } catch(e) {
    clearTimeout(timeout);
    console.warn('poll error', e);
  } finally {
    pollInFlight = false;
  }
}

async function pollRamp() {
  try {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 4000);
    const rs = await fetch('/rampstatus', { signal: controller.signal }).then(r => r.json());
    clearTimeout(timeout);

    if (rs.stepCount && rs.stepCount > 0) {
      runChartCurrentStep = rs.rampStep;
    }
    drawRunChart();

    const stateName  = (rs.rampState >= 0 && rs.rampState < RAMP_STATE_NAMES.length)
      ? RAMP_STATE_NAMES[rs.rampState] : String(rs.rampState);
    const isOvershoot = rs.rampState === 4;
    const stateEl = document.getElementById('rsState');
    if (stateEl) { stateEl.textContent = stateName; stateEl.className = 'rs-val' + (isOvershoot ? ' alert' : ''); }

    const stepEl = document.getElementById('rsStep');
    if (stepEl) stepEl.textContent = (rs.rampStep + 1) + ' / ' + rs.stepCount;
    const targetEl = document.getElementById('rsTarget');
    if (targetEl) targetEl.textContent = rs.stepTarget.toFixed(1) + '\u00B0C';
    const predEl = document.getElementById('rsPredPeak');
    if (predEl) predEl.textContent = rs.rampState === 1 ? rs.predictedPeak.toFixed(1) + '\u00B0C' : '--';
    const overEl = document.getElementById('rsOvershoot');
    if (rs.overshootAmt > 0) {
      overEl.textContent = '+' + rs.overshootAmt.toFixed(1) + '\u00B0C \u26A0';
      overEl.className   = 'rs-val alert';
    } else {
      overEl.textContent = 'none';
      overEl.className   = 'rs-val good';
    }

    const coastCur = document.getElementById('rsCoastCur');
    if (coastCur) {
      const curRatio = rs.coastBase - rs.coastSlope * (rs.stepTarget / 100.0);
      coastCur.textContent = curRatio.toFixed(3);
    }
    const coastModel = document.getElementById('rsCoastModel');
    if (coastModel) coastModel.textContent = 'base=' + rs.coastBase.toFixed(3) + '  slope=' + rs.coastSlope.toFixed(3) + '/100\u00B0C';

    const ageEl = document.getElementById('rsAge');
    if (ageEl) {
      const ageS = Math.floor(rs.stateAgeMs / 1000);
      ageEl.textContent = Math.floor(ageS / 60) + ':' + String(ageS % 60).padStart(2, '0');
    }

    const soakEl = document.getElementById('rsSoakRemain');
    if (soakEl) soakEl.textContent = rs.rampState === 5
      ? Math.floor(rs.soakRemainS / 60) + ':' + String(rs.soakRemainS % 60).padStart(2, '0') + ' remaining'
      : '--';

    const tbody = document.getElementById('learnedBody');
    if (tbody) {
      tbody.innerHTML = '';
      if (rs.learned && Array.isArray(rs.learned)) {
        rs.learned.forEach((s, idx) => {
          const tr = document.createElement('tr');
          tr.innerHTML = `<td>${idx + 1}</td>`
            + `<td style="text-align:right">${s.fireStart.toFixed(0)}\u00B0C</td>`
            + `<td style="text-align:right">${s.cutoff.toFixed(0)}\u00B0C</td>`
            + `<td style="text-align:right;color:${(s.target !== undefined && s.peak > s.target + 10) ? '#e74c3c' : '#0f9'}">${s.peak.toFixed(0)}\u00B0C</td>`
            + `<td style="text-align:right">${s.coastRatio.toFixed(3)}</td>`;
          tbody.appendChild(tr);
        });
      }
      for (let i = rs.learnedCount; i < rs.stepCount - 1; i++) {
        const tr = document.createElement('tr');
        tr.innerHTML = `<td style="color:#555">${i + 1}</td>` + `<td colspan="4" style="color:#555;text-align:right">--</td>`;
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

// ── WiFi form ─────────────────────────────────────────────────────────────────
document.getElementById('wifiForm').addEventListener('submit', async e => {
  e.preventDefault();
  await fetch('/wifi', {method:'POST', body: new URLSearchParams(new FormData(e.target))});
  alert('WiFi saved. Device will reboot.');
});

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
    // Update lastDevice so poll() won't overwrite
    document.getElementById('dcPct').dataset.lastDevice = pct.toFixed(0);
    document.getElementById('dcPeriodMs').dataset.lastDevice = String(ms);
  } catch(e) {
    document.getElementById('dcMsg').textContent = 'Failed: ' + e;
  }
}

// ── Helpers ───────────────────────────────────────────────────────────────────
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
  } catch(e) { console.warn('stop error', e); }
}

function setManual(action) {
  fetch('/manual', {method:'POST', body: new URLSearchParams({action})})
    .then(r => { if (!r.ok) throw new Error('HTTP ' + r.status); })
    .then(() => poll())
    .catch(e => console.warn('manual error', e));
}
</script>

<!-- ── GPIO / Pin Debug Panel ─────────────────────────────────────── -->
<div style="margin-bottom:.75rem">
  <div class="card" id="pinDebugPanel">
    <h2 style="cursor:pointer;user-select:none;" onclick="togglePinPanel()">
      &#x1F527; GPIO Pin State
      <span id="pinPanelToggle" style="font-size:.75rem;color:#aaa;margin-left:.5rem;">[collapse]</span>
    </h2>
    <div id="pinPanelBody">
      <div style="display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:.5rem;margin-bottom:.75rem;">
        <div class="pin-cell" id="pc-btnUp">
          <div class="pin-label">BTN UP &nbsp;<span class="pin-num">GPIO5</span></div>
          <div class="pin-val" id="pv-btnUp">--</div>
          <div class="pin-phase" id="pp-btnUp">--</div>
        </div>
        <div class="pin-cell" id="pc-btnDn">
          <div class="pin-label">BTN DN &nbsp;<span class="pin-num">GPIO7</span></div>
          <div class="pin-val" id="pv-btnDn">--</div>
          <div class="pin-phase" id="pp-btnDn">--</div>
        </div>
        <div class="pin-cell" id="pc-btnCtr">
          <div class="pin-label">BTN CTR &nbsp;<span class="pin-num">GPIO6</span></div>
          <div class="pin-val" id="pv-btnCtr">--</div>
          <div class="pin-phase" id="pp-btnCtr">--</div>
        </div>
        <div class="pin-cell" id="pc-mosfet">
          <div class="pin-label">MOSFET &nbsp;<span class="pin-num">GPIO0</span></div>
          <div class="pin-val" id="pv-mosfet">--</div>
          <div class="pin-phase" id="pp-mosfet" style="font-size:.7rem;color:#aaa;">OUTPUT</div>
        </div>
        <div class="pin-cell" style="opacity:.55;">
          <div class="pin-label">I2C SDA &nbsp;<span class="pin-num">GPIO10</span></div>
          <div class="pin-val" style="color:#555;">I2C</div>
          <div class="pin-phase" style="font-size:.7rem;color:#555;">OLED</div>
        </div>
        <div class="pin-cell" style="opacity:.55;">
          <div class="pin-label">I2C SCL &nbsp;<span class="pin-num">GPIO8</span></div>
          <div class="pin-val" style="color:#555;">I2C</div>
          <div class="pin-phase" style="font-size:.7rem;color:#555;">OLED</div>
        </div>
        <div class="pin-cell" style="opacity:.55;">
          <div class="pin-label">MAX6675 SCK &nbsp;<span class="pin-num">GPIO4</span></div>
          <div class="pin-val" style="color:#555;">SPI</div>
          <div class="pin-phase" style="font-size:.7rem;color:#555;">Thermocouple</div>
        </div>
        <div class="pin-cell" style="opacity:.55;">
          <div class="pin-label">MAX6675 CS &nbsp;<span class="pin-num">GPIO3</span></div>
          <div class="pin-val" style="color:#555;">SPI</div>
          <div class="pin-phase" style="font-size:.7rem;color:#555;">Thermocouple</div>
        </div>
        <div class="pin-cell" style="opacity:.55;">
          <div class="pin-label">MAX6675 SO &nbsp;<span class="pin-num">GPIO2</span></div>
          <div class="pin-val" style="color:#555;">SPI</div>
          <div class="pin-phase" style="font-size:.7rem;color:#555;">Thermocouple</div>
        </div>
      </div>
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

      setPinCell('btnUp',  p.btnUp,  p.btnUp  ? 'PRESSED' : 'OPEN', p.btnUpPhase,  'pressed');
      setPinCell('btnDn',  p.btnDn,  p.btnDn  ? 'PRESSED' : 'OPEN', p.btnDnPhase,  'pressed');
      setPinCell('btnCtr', p.btnCtr, p.btnCtr ? 'PRESSED' : 'OPEN', p.btnCtrPhase, 'pressed');
      setPinCell('mosfet', p.outputOn, p.outputOn ? 'HIGH / ON' : 'LOW / OFF', 'OUTPUT', 'active');

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

      fEs.textContent      = 'E-stop presses: ' + p.estopCount + '/3';
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
