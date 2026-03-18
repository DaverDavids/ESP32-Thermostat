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
  .on{background:#2ecc71}.off{background:#e74c3c}
  #wifiSection{display:none}
</style>
</head>
<body>

<div class="card">
  <h2>&#x1F321; Live Temperature</h2>
  <div class="big">Temp: <span id="temp">--</span> &deg;C</div>
  <div>Setpoint: <span id="sp">--</span> &deg;C &nbsp;
    Output: <span id="out" class="badge">--</span>
  </div>
  <div>Raw voltage: <span id="rawMv">--</span> mV</div>
</div>

<div class="card">
  <h2>&#x1F4CA; History</h2>
  <canvas id="chart"></canvas>
</div>

<div class="card">
  <h2>&#x2699;&#xFE0F; Configuration</h2>
  <form id="cfgForm">
    <label>Setpoint (&deg;C)<input type="number" step="1" name="sp" id="cfgSp"></label>
    <label>Hysteresis (&deg;C)<input type="number" step="0.5" name="hyst" id="cfgHyst"></label>
    <label>Probe Offset (&deg;C)<input type="number" step="0.5" name="off" id="cfgOff"></label>
    <label>Probe Type
      <select name="ptype" id="cfgPtype">
        <option value="0">K-Type (~41 &micro;V/&deg;C)</option>
        <option value="1">J-Type (~52 &micro;V/&deg;C)</option>
      </select>
    </label>
    <div>Current uV/&deg;C: <span id="uvpc">--</span></div>
    <button type="submit">Save Config</button>
  </form>
</div>

<div class="card">
  <h2>&#x1F527; Calibration</h2>
  <form id="calForm">
    <div style="display:grid;grid-template-columns:1fr 1fr;gap:1rem;">
      <div>
        <label>Point 1 - Voltage (mV)<input type="number" step="0.0001" name="mv1" id="calMv1"></label>
        <label>Point 1 - Temperature (&deg;C)<input type="number" step="0.1" name="temp1" id="calTemp1"></label>
      </div>
      <div>
        <label>Point 2 - Voltage (mV)<input type="number" step="0.0001" name="mv2" id="calMv2"></label>
        <label>Point 2 - Temperature (&deg;C)<input type="number" step="0.1" name="temp2" id="calTemp2"></label>
      </div>
    </div>
    <div style="display:flex;gap:0.5rem;margin-top:0.5rem;">
      <button type="submit">Calibrate</button>
      <button type="button" onclick="clearCalibration()" style="background:#777;">Clear Calibration</button>
    </div>
  </form>
  <div id="calResult" style="margin-top:.5rem;color:#0f9">--</div>
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
// ── Minimal canvas line chart (no external libs) ─────────────────────────────
function drawChart(data, setpoint) {
  const c = document.getElementById('chart');
  const w = c.width = c.offsetWidth;
  const h = c.height = 200;
  const ctx = c.getContext('2d');
  ctx.clearRect(0, 0, w, h);
  if (!data.length) return;
  const min = Math.min(...data) - 10;
  const max = Math.max(...data, setpoint) + 10;
  const sx = w / (data.length - 1 || 1);
  const sy = h / (max - min);
  const py = v => h - (v - min) * sy;
  // Setpoint line
  ctx.strokeStyle = '#e94560'; ctx.lineWidth = 1; ctx.setLineDash([4,4]);
  ctx.beginPath(); ctx.moveTo(0, py(setpoint)); ctx.lineTo(w, py(setpoint)); ctx.stroke();
  ctx.setLineDash([]);
  // Temp line
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
    document.getElementById('rawMv').textContent = st.shuntMV.toFixed(4);
    document.getElementById('uvpc').textContent = st.uvPerC.toFixed(4);
    const outEl = document.getElementById('out');
    outEl.textContent = st.output ? 'ON' : 'OFF';
    outEl.className = 'badge ' + (st.output ? 'on' : 'off');
    currentSetpoint = st.setpoint;
    // Pre-fill config form if empty
    if (!document.getElementById('cfgSp').value) {
      document.getElementById('cfgSp').value    = st.setpoint;
      document.getElementById('cfgHyst').value  = st.hysteresis;
      document.getElementById('cfgOff').value   = st.offset;
      document.getElementById('cfgPtype').value = st.probeType;
    }
    drawChart(hist, currentSetpoint);
  } catch(e) { console.warn('poll error', e); }
}

setInterval(poll, 3000);
poll();

// ── Config form ──────────────────────────────────────────────────────────────
document.getElementById('cfgForm').addEventListener('submit', async e => {
  e.preventDefault();
  const fd = new FormData(e.target);
  const params = new URLSearchParams(fd);
  await fetch('/config', {method:'POST', body:params});
  alert('Saved!');
  poll();
});

// ── Calibration form ──────────────────────────────────────────────────────────
document.getElementById('calForm').addEventListener('submit', async e => {
  e.preventDefault();
  const fd = new FormData(e.target);
  const params = new URLSearchParams(fd);
  try {
    const r = await fetch('/calibrate', {method:'POST', body:params});
    if (!r.ok) throw new Error(await r.text());
    const data = await r.json();
    document.getElementById('calResult').textContent = 'Calibrated uV/°C = ' + data.uvPerC.toFixed(4);
    poll();
  } catch (err) {
    document.getElementById('calResult').textContent = 'Calibration failed: ' + err;
  }
});

// ── WiFi form ─────────────────────────────────────────────────────────────────
document.getElementById('wifiForm').addEventListener('submit', async e => {
  e.preventDefault();
  const fd = new FormData(e.target);
  const params = new URLSearchParams(fd);
  await fetch('/wifi', {method:'POST', body:params});
  alert('WiFi saved. Device will reboot.');
});

// ── Clear calibration ─────────────────────────────────────────────────────────
async function clearCalibration() {
  try {
    const r = await fetch('/calibrate/clear', {method:'POST'});
    if (!r.ok) throw new Error(await r.text());
    document.getElementById('calResult').textContent = 'Calibration cleared';
    document.getElementById('calForm').reset();
    poll();
  } catch (err) {
    document.getElementById('calResult').textContent = 'Clear failed: ' + err;
  }
}
</script>
</body>
</html>
)rawhtml";
