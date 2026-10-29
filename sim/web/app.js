// IRL2FA browser simulator: two devices, an IR link, a clock, a battery.
// SPDX-License-Identifier: AGPL-3.0-or-later
import createIRL2FA from './irl2fa.mjs';
import {
  BTN, Device, World, DISPLAY_W, DISPLAY_H, DISPLAY_STRIDE,
  POWER_DEFAULTS, mergePowerModel, fromHardwareModel, chargeMAs, lifeDays, profileAvgMA, profileLifeDays, base32Decode, totpWith,
} from './irl2fa-device.js';

const $ = (s, root = document) => root.querySelector(s);
const units = [...document.querySelectorAll('.unit')];
const stage = $('#stage');
const reduceMotion = matchMedia('(prefers-reduced-motion: reduce)').matches;

const SCREEN_LABEL = {
  menu: 'Menu', codes: 'Codes', code: 'Code', contacts: 'Contacts', contact: 'Contact words',
  pair: 'Pairing', sas: 'Compare symbols', text: 'Typing', code_opts: 'Code options',
  confirm: 'Confirm', time: 'Set time', tz: 'Time zone', pin: 'PIN', message: 'Message', about: 'About',
};

const state = {
  warp: 1,
  simUtc: Date.now(),
  skew: [0, 0],
  active: 0,
  model: POWER_DEFAULTS,
  modelSrc: 'built-in defaults',
  ir: { frames: 0, delivered: 0, lost: 0, lastAt: -1e9 },
  held: [null, null],
};

let devices = [];
let world = null;
let screens = [];

// ------------------------------------------------------------------
// e-paper rendering

const PAPER = [0xe6, 0xe2, 0xd5];
const INK = [0x24, 0x25, 0x22];
const UP = 4; // offscreen upscale factor before the smooth downscale

function fbToCanvas(fb, invert) {
  const small = document.createElement('canvas');
  small.width = DISPLAY_W;
  small.height = DISPLAY_H;
  const sctx = small.getContext('2d');
  const img = sctx.createImageData(DISPLAY_W, DISPLAY_H);
  const px = img.data;
  for (let y = 0; y < DISPLAY_H; y++) {
    for (let x = 0; x < DISPLAY_W; x++) {
      let black = fb ? (fb[y * DISPLAY_STRIDE + (x >> 3)] >> (7 - (x & 7))) & 1 : 0;
      if (invert) black ^= 1;
      const c = black ? INK : PAPER;
      const o = (y * DISPLAY_W + x) * 4;
      px[o] = c[0]; px[o + 1] = c[1]; px[o + 2] = c[2]; px[o + 3] = 255;
    }
  }
  sctx.putImageData(img, 0, 0);
  const big = document.createElement('canvas');
  big.width = DISPLAY_W * UP;
  big.height = DISPLAY_H * UP;
  const bctx = big.getContext('2d');
  bctx.imageSmoothingEnabled = false;
  bctx.drawImage(small, 0, 0, big.width, big.height);
  return big;
}

function makeGrain() {
  const c = document.createElement('canvas');
  c.width = 256; c.height = 128;
  const ctx = c.getContext('2d');
  const img = ctx.createImageData(c.width, c.height);
  for (let i = 0; i < img.data.length; i += 4) {
    const v = 128 + (Math.random() - 0.5) * 70;
    img.data[i] = img.data[i + 1] = img.data[i + 2] = v;
    img.data[i + 3] = 255;
  }
  ctx.putImageData(img, 0, 0);
  return c;
}
const GRAIN = makeGrain();

class Screen {
  constructor(canvas) {
    this.c = canvas;
    this.ctx = canvas.getContext('2d');
    this.cur = { fb: null, img: fbToCanvas(null, false), inv: null };
    this.prev = null;
    this.anim = null;
    this.dirty = true;
  }
  show(fb, full) {
    this.prev = this.cur; // the image currently on the glass
    this.cur = { fb, img: fbToCanvas(fb, false), inv: null };
    this.anim = reduceMotion ? null : { full, t0: performance.now() };
    this.dirty = true;
  }
  inverted(frame) {
    if (!frame.inv) frame.inv = fbToCanvas(frame.fb, true);
    return frame.inv;
  }
  fit() {
    const r = this.c.getBoundingClientRect();
    const dpr = Math.min(window.devicePixelRatio || 1, 3);
    const w = Math.max(1, Math.round(r.width * dpr)), h = Math.max(1, Math.round(r.height * dpr));
    if (this.c.width !== w || this.c.height !== h) {
      this.c.width = w;
      this.c.height = h;
      this.dirty = true;
    }
  }
  draw(now) {
    this.fit();
    if (!this.dirty && !this.anim) return;
    const { ctx, c } = this;
    const W = c.width, H = c.height;
    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = 'high';
    ctx.globalAlpha = 1;
    const fill = (rgb) => { ctx.fillStyle = `rgb(${rgb.join(',')})`; ctx.fillRect(0, 0, W, H); };
    const img = (canvas, a = 1) => { ctx.globalAlpha = a; ctx.drawImage(canvas, 0, 0, W, H); ctx.globalAlpha = 1; };
    let done = true;
    if (this.anim && this.prev) {
      const e = now - this.anim.t0;
      if (this.anim.full) {
        // Global refresh: the panel inverts the old image, goes black,
        // white, shows the new image inverted, then settles.
        if (e < 110) img(this.inverted(this.prev));
        else if (e < 220) fill(INK);
        else if (e < 320) fill(PAPER);
        else if (e < 430) img(this.inverted(this.cur));
        else img(this.cur.img);
        done = e >= 430;
      } else {
        // Fast (partial) refresh: a quick fade with a faint ghost.
        if (e < 160) { img(this.prev.img); img(this.cur.img, e / 160); }
        else { img(this.cur.img); img(this.prev.img, Math.max(0, 0.07 * (1 - (e - 160) / 400))); }
        done = e >= 560;
      }
    } else {
      img(this.cur.img);
    }
    // Paper grain.
    ctx.globalCompositeOperation = 'multiply';
    ctx.globalAlpha = 0.07;
    ctx.drawImage(GRAIN, 0, 0, W, H);
    ctx.globalAlpha = 1;
    ctx.globalCompositeOperation = 'source-over';
    if (done) this.anim = null;
    this.dirty = false;
  }
}

// ------------------------------------------------------------------
// IR beam between the devices' top edges

const beam = { c: $('#beam'), pulses: [] };
beam.ctx = beam.c.getContext('2d');

function centerOf(el, base) {
  const r = el.getBoundingClientRect();
  return { x: r.left + r.width / 2 - base.left, y: r.top + r.height * 0.35 - base.top };
}

function beamPath(i, j, base) {
  const p0 = centerOf($('.irwin.tx', units[i]), base);
  const p3 = centerOf($('.irwin.rx', units[j]), base);
  const pts = [];
  if (Math.abs(p3.x - p0.x) >= Math.abs(p3.y - p0.y)) {
    // Arc above the top edges; the stage leaves ~90 px of room (a cubic
    // with both handles at -k peaks at 0.75 k).
    const k = Math.min(i === 0 ? 70 : 108, (24 + Math.abs(p3.x - p0.x) * 0.16) * (i === 0 ? 1 : 1.35));
    const c1 = { x: p0.x, y: p0.y - k }, c2 = { x: p3.x, y: p3.y - k };
    for (let n = 0; n <= 48; n++) {
      const t = n / 48, u = 1 - t;
      pts.push({
        x: u * u * u * p0.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * p3.x,
        y: u * u * u * p0.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * p3.y,
      });
    }
  } else {
    // Stacked (narrow screens): loop round the right-hand side.
    const xr = base.width - (i === 0 ? 5 : 11); // in the page gutter
    const lift = 12 + (i === 0 ? 0 : 6);
    const way = [p0, { x: p0.x, y: p0.y - lift }, { x: xr, y: p0.y - lift }, { x: xr, y: p3.y - lift },
      { x: p3.x, y: p3.y - lift }, p3];
    for (let s = 0; s < way.length - 1; s++) {
      for (let n = 0; n < 12; n++) {
        const t = n / 12;
        pts.push({ x: way[s].x + (way[s + 1].x - way[s].x) * t, y: way[s].y + (way[s + 1].y - way[s].y) * t });
      }
    }
    pts.push(p3);
  }
  let len = 0;
  const acc = [0];
  for (let n = 1; n < pts.length; n++) {
    len += Math.hypot(pts[n].x - pts[n - 1].x, pts[n].y - pts[n - 1].y);
    acc.push(len);
  }
  return { pts, acc, len, p0, p3 };
}

function pointAt(path, s) {
  const d = Math.max(0, Math.min(1, s)) * path.len;
  let n = 1;
  while (n < path.acc.length - 1 && path.acc[n] < d) n++;
  const a = path.acc[n - 1], b = path.acc[n];
  const t = b > a ? (d - a) / (b - a) : 0;
  return { x: path.pts[n - 1].x + (path.pts[n].x - path.pts[n - 1].x) * t, y: path.pts[n - 1].y + (path.pts[n].y - path.pts[n - 1].y) * t };
}

function strokeRange(ctx, path, s0, s1) {
  const d0 = Math.max(0, s0) * path.len, d1 = Math.min(1, s1) * path.len;
  if (d1 <= d0) return;
  ctx.beginPath();
  const a = pointAt(path, s0);
  ctx.moveTo(a.x, a.y);
  for (let n = 0; n < path.pts.length; n++) if (path.acc[n] > d0 && path.acc[n] < d1) ctx.lineTo(path.pts[n].x, path.pts[n].y);
  const b = pointAt(path, s1);
  ctx.lineTo(b.x, b.y);
  ctx.stroke();
}

const IR_RGB = '229, 72, 77';

function drawBeam(now) {
  const { c, ctx } = beam;
  const base = c.getBoundingClientRect();
  const dpr = Math.min(window.devicePixelRatio || 1, 3);
  const W = Math.round(base.width * dpr), H = Math.round(base.height * dpr);
  if (c.width !== W || c.height !== H) { c.width = W; c.height = H; }
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, base.width, base.height);
  if (!world) return;
  const paths = [beamPath(0, 1, base), beamPath(1, 0, base)];
  ctx.lineCap = 'round';
  if (world.linked) {
    ctx.setLineDash([2, 7]);
    ctx.lineWidth = 1.5;
    ctx.strokeStyle = `rgba(${IR_RGB}, 0.28)`;
    for (const p of paths) strokeRange(ctx, p, 0, 1);
    ctx.setLineDash([]);
  }
  const TRAVEL = 380;
  beam.pulses = beam.pulses.filter((p) => now - p.t0 < 900);
  for (const p of beam.pulses) {
    const age = now - p.t0;
    const path = paths[p.from];
    if (!p.linked) {
      // Nobody in front of the emitter: light spreads out and fades.
      const a = Math.max(0, 1 - age / 520);
      const r = 14 + age * 0.09;
      const g = ctx.createRadialGradient(path.p0.x, path.p0.y, 2, path.p0.x, path.p0.y - r * 0.6, r);
      g.addColorStop(0, `rgba(${IR_RGB}, ${0.5 * a})`);
      g.addColorStop(1, `rgba(${IR_RGB}, 0)`);
      ctx.fillStyle = g;
      ctx.beginPath();
      ctx.moveTo(path.p0.x, path.p0.y);
      ctx.arc(path.p0.x, path.p0.y, r, Math.PI * 1.25, Math.PI * 1.75);
      ctx.closePath();
      ctx.fill();
      continue;
    }
    const head = age / TRAVEL;
    const stop = p.lost ? 0.5 : 1.0;
    const h = Math.min(head, stop);
    const fade = head > stop ? Math.max(0, 1 - (head - stop) * (p.lost ? 2.5 : 4)) : 1;
    if (fade <= 0) continue;
    ctx.shadowColor = `rgba(${IR_RGB}, ${0.9 * fade})`;
    ctx.shadowBlur = 12;
    ctx.lineWidth = 3;
    ctx.strokeStyle = `rgba(${IR_RGB}, ${0.85 * fade})`;
    strokeRange(ctx, path, h - 0.22, h);
    ctx.shadowBlur = 0;
    const q = pointAt(path, h);
    ctx.fillStyle = `rgba(255, 220, 220, ${fade})`;
    ctx.beginPath();
    ctx.arc(q.x, q.y, 2.6, 0, Math.PI * 2);
    ctx.fill();
    if (p.lost && head >= stop) {
      ctx.strokeStyle = `rgba(${IR_RGB}, ${fade})`;
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.moveTo(q.x - 5, q.y - 5); ctx.lineTo(q.x + 5, q.y + 5);
      ctx.moveTo(q.x + 5, q.y - 5); ctx.lineTo(q.x - 5, q.y + 5);
      ctx.stroke();
    }
  }
}

function onIr(from, to, bytes, delivered, lost) {
  const now = performance.now();
  state.ir.frames++;
  if (delivered) state.ir.delivered++;
  if (lost && world.linked) state.ir.lost++;
  state.ir.lastAt = now;
  // Throttle the animation at high time-warp.
  const last = beam.pulses.findLast?.((p) => p.from === from);
  if (!last || now - last.t0 > 120) beam.pulses.push({ from, to, t0: now, linked: world.linked, lost: lost && world.linked });
  const tx = $('.irwin.tx', units[from]);
  tx.classList.add('lit');
  clearTimeout(tx._t);
  tx._t = setTimeout(() => tx.classList.remove('lit'), 140);
}

// ------------------------------------------------------------------
// devices, input

function randomSeed() {
  const s = new Uint32Array(2);
  crypto.getRandomValues(s);
  return s;
}

function bootDevice(i) {
  const d = devices[i];
  const s = randomSeed();
  d.reset(s[0], s[1]);
  d.setBatteryMv(Number($('#mv').value));
  d.init(state.simUtc + state.skew[i] * 1000);
  screens[i].prev = null;
  screens[i].anim = null;
}

function setActive(i) {
  state.active = i;
  units.forEach((u, k) => u.classList.toggle('active', k === i));
}

function applyButtons(i) {
  const name = state.held[i];
  devices[i].setButtons(name ? BTN[name] : 0);
  world.advance(0); // let the firmware see the edge immediately
  const joy = $('.joy', units[i]);
  if (name) joy.dataset.tilt = name; else delete joy.dataset.tilt;
  for (const b of units[i].querySelectorAll('[data-btn]')) b.classList.toggle('on', b.dataset.btn === name);
  refreshStatus();
}

function press(i, name) {
  // A nav switch reports one direction at a time.
  state.held[i] = name;
  setActive(i);
  applyButtons(i);
}

function release(i, name) {
  if (state.held[i] !== name) return;
  state.held[i] = null;
  applyButtons(i);
}

function wireButtons(i) {
  const unit = units[i];
  const device = $('.device', unit);
  for (const b of unit.querySelectorAll('[data-btn]')) {
    const name = b.dataset.btn;
    b.addEventListener('pointerdown', (e) => {
      if (e.button !== 0) return;
      e.preventDefault();
      if (b.closest('.joy')) device.focus({ preventScroll: true });
      b.setPointerCapture(e.pointerId);
      press(i, name);
    });
    const up = () => release(i, name);
    b.addEventListener('pointerup', up);
    b.addEventListener('pointercancel', up);
    b.addEventListener('lostpointercapture', up);
    b.addEventListener('contextmenu', (e) => e.preventDefault());
    // Keyboard activation of a focused touch-pad button: a short press.
    b.addEventListener('click', (e) => {
      if (e.detail !== 0) return;
      press(i, name);
      setTimeout(() => release(i, name), 90);
    });
  }
  device.addEventListener('focus', () => setActive(i));
  device.addEventListener('pointerdown', () => { setActive(i); device.focus({ preventScroll: true }); });
  const skew = $('[data-role="skew"]', unit);
  const out = $('[data-role="skew-out"]', unit);
  skew.addEventListener('input', () => {
    const v = Number(skew.value);
    state.skew[i] = v;
    out.textContent = `${v > 0 ? '+' : ''}${v} s`;
    devices[i].setUnixMs(state.simUtc + v * 1000);
    devices[i].due = 0;
  });
}

const KEYS = { ArrowUp: 'UP', ArrowDown: 'DOWN', ArrowLeft: 'LEFT', ArrowRight: 'RIGHT', Enter: 'CENTER', ' ': 'CENTER' };

document.addEventListener('keydown', (e) => {
  const name = KEYS[e.key];
  if (!name || !world || e.altKey || e.ctrlKey || e.metaKey) return;
  const t = e.target;
  const onDevice = t && t.classList && t.classList.contains('device');
  if (!onDevice && t !== document.body) return; // leave form controls alone
  e.preventDefault();
  if (e.repeat) return; // holding is the firmware's job
  const i = onDevice ? units.indexOf(t.closest('.unit')) : state.active;
  press(i, name);
});
document.addEventListener('keyup', (e) => {
  const name = KEYS[e.key];
  if (!name) return;
  for (let i = 0; i < 2; i++) release(i, name);
});
window.addEventListener('blur', () => {
  for (let i = 0; i < 2; i++) if (state.held[i]) release(i, state.held[i]);
});

// ------------------------------------------------------------------
// panels

function fmtDur(ms) {
  const s = Math.floor(ms / 1000);
  if (s < 60) return `${s}s`;
  const m = Math.floor(s / 60);
  if (m < 60) return `${m}m ${String(s % 60).padStart(2, '0')}s`;
  const h = Math.floor(m / 60);
  if (h < 48) return `${h}h ${String(m % 60).padStart(2, '0')}m`;
  return `${Math.floor(h / 24)}d ${h % 24}h`;
}
function fmtCharge(uAh) {
  return uAh < 1000 ? `${uAh.toFixed(uAh < 10 ? 2 : 1)} µAh` : `${(uAh / 1000).toFixed(2)} mAh`;
}
function fmtCurrent(mA) {
  return mA < 1 ? `${(mA * 1000).toFixed(mA < 0.01 ? 2 : 0)} µA` : `${mA.toFixed(2)} mA`;
}
function fmtLife(days) {
  if (!isFinite(days)) return '–';
  if (days < 2) return `${(days * 24).toFixed(0)} hours`;
  if (days < 365) return `${days.toFixed(0)} days`;
  return `${(days / 365).toFixed(1)} years`;
}

function setEnergyCell(key, text) {
  const el = document.querySelector(`[data-e="${key}"]`);
  if (el && el.textContent !== text) el.textContent = text;
}

function refreshEnergy() {
  const m = state.model;
  devices.forEach((d, i) => {
    const e = d.energy();
    const mAs = chargeMAs(m, e);
    const total = (e.awakeMs + e.asleepMs) / 1000;
    const avg = total > 0 ? mAs / total : 0;
    setEnergyCell(`${i}:used`, fmtCharge(mAs / 3.6));
    setEnergyCell(`${i}:split`, `${fmtDur(e.awakeMs)} / ${fmtDur(e.asleepMs)}`);
    setEnergyCell(`${i}:ref`, `${e.full} / ${e.fast}`);
    setEnergyCell(`${i}:ir`, fmtDur(e.irMs));
    setEnergyCell(`${i}:avg`, total > 1 ? fmtCurrent(avg) : '–');
    setEnergyCell(`${i}:life`, total > 1 ? fmtLife(lifeDays(m, avg)) : '–');
  });
}

function showModel() {
  const m = state.model;
  $('#profile-name').textContent = `Projection, ${m.profile.name}`;
  const days = profileLifeDays(m);
  $('#profile-life').textContent = `${fmtLife(days)}`;
  $('#profile-life').title = `${days.toFixed(0)} days at ${fmtCurrent(profileAvgMA(m))} average`;
  $('#model-src').textContent = `Source: ${state.modelSrc}. A “use” is ${m.profile.awake_s_per_use} s awake ` +
    `with ${m.profile.full_refreshes_per_use} full and ${m.profile.fast_refreshes_per_use} fast refreshes` +
    (m.profile.pairings_per_year ? `, plus ${m.profile.pairings_per_year} pairings a year` : '') +
    (m.self_discharge_pct_per_year ? `, and ${m.self_discharge_pct_per_year}%/year self-discharge` : '') + '.';
  const list = $('#model-list');
  list.replaceChildren(...[
    `sleep ${+m.sleep_uA.toFixed(3)} µA`,
    `awake (MCU) ${m.awake_mA} mA`,
    `full refresh ${m.full_refresh.mA} mA × ${m.full_refresh.s} s`,
    `fast refresh ${m.fast_refresh.mA} mA × ${m.fast_refresh.s} s`,
    `IR on ${m.ir_mA} mA`,
    `cell ${m.battery} ${m.battery_mAh} mAh usable`,
  ].map((t) => Object.assign(document.createElement('li'), { textContent: t })));
}

async function loadModel() {
  let model = POWER_DEFAULTS;
  try {
    const r = await fetch('power-defaults.json', { cache: 'no-cache' });
    if (r.ok) { model = mergePowerModel(model, await r.json()); state.modelSrc = 'power-defaults.json'; }
  } catch { /* built-in defaults */ }
  try {
    const r = await fetch('power-model.json', { cache: 'no-cache' });
    if (r.ok) {
      const hw = await r.json();
      const conv = fromHardwareModel(hw, model);
      if (conv) { model = conv; state.modelSrc = 'hardware/power/model.json (typical values)'; }
      else { model = mergePowerModel(model, hw); state.modelSrc = 'power-model.json'; }
    }
  } catch { /* none */ }
  state.model = model;
  showModel();
}

const pad2 = (n) => String(n).padStart(2, '0');
function fmtUtc(ms) {
  const d = new Date(ms);
  return `${d.getUTCFullYear()}-${pad2(d.getUTCMonth() + 1)}-${pad2(d.getUTCDate())} ` +
    `${pad2(d.getUTCHours())}:${pad2(d.getUTCMinutes())}:${pad2(d.getUTCSeconds())}`;
}

function refreshStatus() {
  devices.forEach((d, i) => {
    const chip = $('[data-role="screen"]', units[i]);
    const text = d.sleeping ? 'Asleep · press to wake' : (SCREEN_LABEL[d.screen()] || d.screen());
    if (chip.textContent !== text) chip.textContent = text;
    chip.classList.toggle('asleep', d.sleeping);
    $('[data-role="ir"]', units[i]).hidden = !d.irOn();
  });
  const clock = $('#clock');
  const drift = Math.round((state.simUtc - Date.now()) / 1000);
  const sub = state.warp !== 1 || Math.abs(drift) > 2
    ? `simulated · ${drift >= 0 ? '+' : ''}${fmtDur(Math.abs(drift) * 1000)}${drift < 0 ? ' behind' : ' ahead of'} real time`
    : 'UTC · following the real clock';
  const html = `${fmtUtc(state.simUtc)}<small>${sub}</small>`;
  if (clock.innerHTML !== html) clock.innerHTML = html;
  const ago = performance.now() - state.ir.lastAt;
  const irs = $('#irstats');
  const t = state.ir.frames === 0 ? 'IR idle'
    : `IR ${ago < 600 ? '●' : '○'} ${state.ir.delivered} frames passed${state.ir.lost ? `, ${state.ir.lost} lost` : ''}`;
  if (irs.textContent !== t) irs.textContent = t;
}

// Reference TOTP with Web Crypto.
const keyCache = new Map();
async function webHmacSha1(key, msg) {
  const id = Array.from(key).join(',');
  let k = keyCache.get(id);
  if (!k) {
    k = await crypto.subtle.importKey('raw', key, { name: 'HMAC', hash: 'SHA-1' }, false, ['sign']);
    keyCache.clear();
    keyCache.set(id, k);
  }
  return new Uint8Array(await crypto.subtle.sign('HMAC', k, msg));
}
let totpBusy = false;
async function refreshTotp() {
  if (totpBusy) return;
  totpBusy = true;
  try {
    const key = base32Decode($('#totp-secret').value);
    const out = $('#totp-real'), simOut = $('#totp-sim');
    const now = Date.now() / 1000;
    $('#totp-ring').style.setProperty('--p', String((30 - (now % 30)) / 30));
    if (!key || key.length === 0) {
      out.textContent = '------';
      simOut.textContent = 'Not valid base32 (letters A–Z and digits 2–7).';
      return;
    }
    const real = await totpWith(webHmacSha1, key, Math.floor(now));
    out.textContent = `${real.slice(0, 3)} ${real.slice(3)}`;
    const devT = devices.length ? devices[0].unix() : now;
    if (Math.floor(devT / 30) !== Math.floor(now / 30)) {
      const sim = await totpWith(webHmacSha1, key, devT);
      simOut.textContent = `Your device's clock is in another 30 s step: it should show ${sim.slice(0, 3)} ${sim.slice(3)}.`;
    } else {
      simOut.textContent = `${Math.ceil(30 - (now % 30))} s left · same step as your device's clock.`;
    }
  } finally {
    totpBusy = false;
  }
}

function setWarp(w) {
  state.warp = w;
  for (const b of document.querySelectorAll('.seg button')) b.setAttribute('aria-checked', String(Number(b.dataset.warp) === w));
}

function wirePanels() {
  for (const b of document.querySelectorAll('.seg button')) b.addEventListener('click', () => setWarp(Number(b.dataset.warp)));
  $('#now-btn').addEventListener('click', () => {
    state.simUtc = Date.now();
    devices.forEach((d, i) => { d.setUnixMs(state.simUtc + state.skew[i] * 1000); d.due = 0; });
    setWarp(1);
  });
  $('#together').addEventListener('change', (e) => {
    world.linked = e.target.checked;
    stage.classList.toggle('together', world.linked);
  });
  $('#noisy').addEventListener('change', (e) => { world.noisy = e.target.checked; });
  $('#mv').addEventListener('input', (e) => {
    const mv = Number(e.target.value);
    $('#mv-out').textContent = `${(mv / 1000).toFixed(2)} V`;
    devices.forEach((d) => d.setBatteryMv(mv));
  });
  $('#totp-secret').addEventListener('input', refreshTotp);
  const demo = $('#demo-btn');
  demo.addEventListener('click', () => {
    const d = devices[0];
    const a = d.addTotp('GitHub', 'JBSWY3DPEHPK3PXP');
    const b = a >= 0 ? d.addTotp('Example', 'GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ') : -1;
    world.advance(0);
    if (a < 0 || b < 0) {
      demo.textContent = d.pinSet() && !d.unlocked() ? 'Unlock your device first' : 'Could not add codes';
    } else {
      demo.textContent = 'Added 2 codes to your device';
      demo.disabled = true;
    }
    setTimeout(() => { if (!demo.disabled) demo.textContent = 'Load demo data'; }, 2500);
    refreshStatus();
  });
  $('#reset-btn').addEventListener('click', () => {
    for (let i = 0; i < 2; i++) { state.held[i] = null; bootDevice(i); }
    state.ir = { frames: 0, delivered: 0, lost: 0, lastAt: -1e9 };
    demo.disabled = false;
    demo.textContent = 'Load demo data';
    world.advance(0);
    refreshStatus();
  });
}

// ------------------------------------------------------------------
// main loop

let lastFrame = performance.now();
let simAcc = 0;
let lastUi = 0;
let lastTotpSecond = -1;

function frame(now) {
  const dt = Math.min(now - lastFrame, 6 * 3600 * 1000);
  lastFrame = now;
  const anyHeld = state.held.some(Boolean);
  simAcc += dt * (anyHeld ? 1 : state.warp);
  const step = Math.floor(simAcc);
  simAcc -= step;
  if (step > 0) {
    world.advance(step);
    state.simUtc += step;
  }
  for (const s of screens) s.draw(now);
  drawBeam(now);
  if (now - lastUi > 200) {
    lastUi = now;
    refreshStatus();
    refreshEnergy();
  }
  const sec = Math.floor(Date.now() / 1000);
  if (sec !== lastTotpSecond) { lastTotpSecond = sec; refreshTotp(); }
  requestAnimationFrame(frame);
}

async function main() {
  try {
    const wasm = await (await fetch(new URL('irl2fa.wasm', import.meta.url))).arrayBuffer();
    const opts = () => ({ wasmBinary: wasm.slice(0) });
    devices = [await Device.create(createIRL2FA, 'A', opts()), await Device.create(createIRL2FA, 'B', opts())];
  } catch (err) {
    const el = $('#load-error');
    el.hidden = false;
    el.textContent = `Could not start the simulator (${err && err.message ? err.message : err}). ` +
      'It needs WebAssembly and must be served over http(s), not opened as a file.';
    return;
  }
  screens = units.map((u) => new Screen($('canvas.epd', u)));
  world = new World(devices);
  world.onRefresh = (d, full) => screens[devices.indexOf(d)].show(d.framebuffer(), full);
  world.onIr = onIr;
  units.forEach((_, i) => wireButtons(i));
  wirePanels();
  for (let i = 0; i < 2; i++) bootDevice(i);
  setActive(0);
  world.advance(0);
  await loadModel();
  refreshStatus();
  window.addEventListener('resize', () => screens.forEach((s) => { s.dirty = true; }));
  // For automated checks and the curious: the live simulation.
  window.irl2fa = { devices, world, state, BTN, press, release };
  requestAnimationFrame(frame);
}

main();
