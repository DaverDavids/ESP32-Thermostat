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
async function startRun() {
  const mode = document.getElementById('runModeSelect').value;
  if (mode === 'autoramp') { alert('Auto-Ramp coming in Stage 2. Using Bang-Bang for now.'); }
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
    } else {
      rsEl.textContent = 'No run active.';
      rsEl.style.color = '#aaa';
    }

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
    // CJC offset is filled in the unified cfgSp block above

    const crEl = document.getElementById('calResult');
    if (st.customUvPerC > 0)
      crEl.textContent = 'Calibrated uV/\u00B0C = ' + st.customUvPerC.toFixed(4) + ', Offset = ' + st.offset.toFixed(4);
    else
      crEl.textContent = '';

    if (document.getElementById('calMv1').value === '') {
      document.getElementById('calMv1').value   = st.calMv1;
      document.getElementById('calCjc1').value  = st.cjcC.toFixed(1);
      document.getElementById('calTemp1').value = st.calTemp1;
      document.getElementById('calMv2').value   = st.calMv2;
      document.getElementById('calCjc2').value  = st.cjcC.toFixed(1);
      document.getElementById('calTemp2').value = st.calTemp2;
    }
    drawChart(hist, currentSetpoint);

    // Sync log if run active
    if (st.runActive) syncLog();
  } catch(e) { console.warn('poll error', e); }
}

setInterval(poll, 1000);
openDB().then(() => { updateLogInfo(); poll(); });

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
