import { Simulator } from './sim.js';

const $ = (id) => document.getElementById(id);
const EE_KEY = 'sterownik-oleju-eeprom-v1';

function loadEeprom() {
  try {
    const s = localStorage.getItem(EE_KEY);
    if (!s) return null;
    return Uint8Array.from(atob(s), (c) => c.charCodeAt(0));
  } catch { return null; }
}
function saveEeprom(bytes) {
  try { localStorage.setItem(EE_KEY, btoa(String.fromCharCode(...bytes))); } catch { /* brak dostepu */ }
}

let sim;
let lastEeprom = '';

function boot(eeprom) {
  sim = new Simulator(window.FIRMWARE_HEX, eeprom);
  applyControls();
  lastEeprom = sim.eepromBackend.memory.join(',');
}

// ---------------------------------------------------------------------------
// OLED
// ---------------------------------------------------------------------------
const canvas = $('oled');
const ctx = canvas.getContext('2d');
const img = ctx.createImageData(128, 64);
const ON = [174, 230, 255], OFF = [6, 10, 14];
function drawOled() {
  const o = sim.oled;
  if (!o.dirty) return;
  o.dirty = false;
  const d = img.data;
  for (let y = 0; y < 64; y++) {
    for (let x = 0; x < 128; x++) {
      const c = o.on && o.pixel(x, y) ? ON : OFF;
      const i = (y * 128 + x) * 4;
      d[i] = c[0]; d[i + 1] = c[1]; d[i + 2] = c[2]; d[i + 3] = 255;
    }
  }
  ctx.putImageData(img, 0, 0);
}

// ---------------------------------------------------------------------------
// Enkoder: przeciaganie pierscienia, kolko myszy, strzalki, przycisk w srodku
// ---------------------------------------------------------------------------
const knob = $('knob');
const knobDial = $('knob-dial');
const DETENT_DEG = 24;
let knobAngle = 0, dragLast = null, dragAcc = 0;

function turn(steps) {
  if (!steps) return;
  sim.rotate(steps);
  knobAngle += steps * DETENT_DEG;
  knobDial.style.transform = `rotate(${knobAngle}deg)`;
}
function angleOf(e) {
  const r = knob.getBoundingClientRect();
  return Math.atan2(e.clientY - (r.top + r.height / 2), e.clientX - (r.left + r.width / 2)) * 180 / Math.PI;
}
knob.addEventListener('pointerdown', (e) => {
  if (e.target.closest('#enc-btn')) return;
  knob.setPointerCapture(e.pointerId);
  dragLast = angleOf(e); dragAcc = 0;
});
knob.addEventListener('pointermove', (e) => {
  if (dragLast === null) return;
  const a = angleOf(e);
  let d = a - dragLast;
  if (d > 180) d -= 360;
  if (d < -180) d += 360;
  dragLast = a; dragAcc += d;
  const steps = Math.trunc(dragAcc / DETENT_DEG);
  if (steps) { dragAcc -= steps * DETENT_DEG; turn(steps); }
});
const endDrag = () => { dragLast = null; };
knob.addEventListener('pointerup', endDrag);
knob.addEventListener('pointercancel', endDrag);
knob.addEventListener('wheel', (e) => { e.preventDefault(); turn(e.deltaY > 0 ? -1 : 1); }, { passive: false });
$('enc-left').addEventListener('click', () => turn(-1));
$('enc-right').addEventListener('click', () => turn(1));

const encBtn = $('enc-btn');
const press = (down) => { sim.setButton(down); encBtn.classList.toggle('down', down); };
encBtn.addEventListener('pointerdown', (e) => { e.preventDefault(); encBtn.setPointerCapture(e.pointerId); press(true); });
encBtn.addEventListener('pointerup', () => press(false));
encBtn.addEventListener('pointercancel', () => press(false));
encBtn.addEventListener('contextmenu', (e) => e.preventDefault());
document.addEventListener('keydown', (e) => {
  if (e.target.matches('input')) return;
  if (e.key === 'ArrowRight' || e.key === 'ArrowUp') { turn(1); e.preventDefault(); }
  else if (e.key === 'ArrowLeft' || e.key === 'ArrowDown') { turn(-1); e.preventDefault(); }
  else if ((e.key === ' ' || e.key === 'Enter') && !e.repeat && !e.target.matches('button')) { press(true); e.preventDefault(); }
});
document.addEventListener('keyup', (e) => {
  if ((e.key === ' ' || e.key === 'Enter') && !e.target.matches('button')) press(false);
});

// ---------------------------------------------------------------------------
// Panel: kociol, pompa, palnik
// ---------------------------------------------------------------------------
function applyControls() {
  sim.blower = $('sw-blower').checked;
  sim.stbOk = !$('sw-stb').checked;
  sim.relayWelded = $('sw-weld').checked;
  sim.safetyWelded = $('sw-sweld').checked;
  sim.mosfetShort = $('sw-short').checked;
  sim.jammed = $('sw-jam').checked;
  sim.tcOpen = $('sw-tc').checked;
  const ml = parseFloat($('ml-rev').value);
  if (ml > 0) sim.mlPerRev = ml;
}
for (const id of ['sw-blower', 'sw-stb', 'sw-weld', 'sw-sweld', 'sw-short', 'sw-jam', 'sw-tc', 'ml-rev']) {
  $(id).addEventListener('change', applyControls);
  $(id).addEventListener('input', applyControls);
}
$('btn-hang').addEventListener('click', () => sim.hang());
$('btn-ignite').addEventListener('click', () => sim.ignite());
$('btn-out').addEventListener('click', () => sim.extinguish());
$('btn-reset').addEventListener('click', () => { boot(sim.eepromBackend.memory.slice()); });
$('btn-eeprom').addEventListener('click', () => {
  try { localStorage.removeItem(EE_KEY); } catch { /* */ }
  boot(null);
  $('eeprom-note').textContent = 'Ustawienia i blokada skasowane, sterownik uruchomiony od nowa.';
});

// dzwiek buzzera (wlaczany przyciskiem - przegladarka wymaga gestu)
let audio = null, osc = null;
$('btn-sound').addEventListener('click', () => {
  if (audio) { audio.close(); audio = null; osc = null; $('btn-sound').setAttribute('aria-pressed', 'false'); return; }
  try {
    audio = new AudioContext();
    const gain = audio.createGain(); gain.gain.value = 0;
    osc = audio.createOscillator(); osc.type = 'square'; osc.frequency.value = 2400;
    osc.connect(gain).connect(audio.destination); osc.start();
    osc.gainNode = gain;
    $('btn-sound').setAttribute('aria-pressed', 'true');
  } catch { audio = null; }
});

const fmt = (v, d = 0) => v.toLocaleString('pl-PL', { minimumFractionDigits: d, maximumFractionDigits: d });
function updatePanel(speed) {
  const pumpOn = sim.duty > 0 && sim.rpm > 0;
  $('led-pump').classList.toggle('on', pumpOn);
  $('led-buzz').classList.toggle('on', sim.buzzer);
  $('led-12v').classList.toggle('on', sim.v12);
  $('led-safety').classList.toggle('on', sim.safetyClosed);
  $('v-wdt').textContent = sim.wdtResets;
  $('led-blower').classList.toggle('on', sim.blower);
  if (osc) osc.gainNode.gain.value = sim.buzzer ? 0.05 : 0;

  $('v-duty').textContent = fmt(sim.duty * 100) + ' %';
  $('v-rpm').textContent = fmt(sim.rpm, 0);
  $('v-revs').textContent = fmt(sim.revs, 1);
  $('v-oil').textContent = fmt(sim.oilMl, 1);
  $('v-flow').textContent = fmt(sim.flowLph, 2);
  $('v-temp').textContent = sim.tcOpen ? '—' : fmt(sim.temp, 0);
  $('temp-bar').style.width = Math.min(100, (sim.temp / 320) * 100) + '%';
  const flame = $('flame-state');
  flame.textContent = sim.flameLit ? 'pali się' : (sim.ignitePendingMs > 0 ? 'zapala się…' : 'zgaszony');
  flame.dataset.state = sim.flameLit ? 'on' : 'off';
  $('v-park').textContent = (sim.angle % 360) < 20 ? 'PARK' : '—';
  $('v-time').textContent = new Date(sim.timeMs).toISOString().substr(11, 8);
  $('v-speed').textContent = fmt(speed * 100) + ' %';
  $('v-12v').textContent = sim.motorSupply ? '12,6 V' : '0 V';
}

// ---------------------------------------------------------------------------
// Petla: emulacja w czasie rzeczywistym, z limitem pracy na klatke (telefony)
// ---------------------------------------------------------------------------
let prev = performance.now(), debt = 0, winReal = 0, winSim = 0, speed = 1, lastSave = 0;
function frame(now) {
  const dt = Math.min(100, now - prev);
  prev = now;
  debt = Math.min(debt + dt, 200);
  const t0 = performance.now();
  let done = 0;
  while (debt >= 1 && performance.now() - t0 < 24) {
    const n = Math.min(4, Math.floor(debt));
    sim.run(n); debt -= n; done += n;
  }
  if (performance.now() - t0 >= 24) debt = Math.min(debt, 20);
  winReal += dt; winSim += done;
  if (winReal >= 1000) { speed = winSim / winReal; winReal = 0; winSim = 0; }
  drawOled();
  updatePanel(speed);
  if (now - lastSave > 3000) {
    lastSave = now;
    const cur = sim.eepromBackend.memory.join(',');
    if (cur !== lastEeprom) { lastEeprom = cur; saveEeprom(sim.eepromBackend.memory); }
  }
  requestAnimationFrame(frame);
}

boot(loadEeprom());
requestAnimationFrame(frame);
