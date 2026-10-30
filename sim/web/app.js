// IRL2FA browser simulator: two devices, an IR link, a clock, a battery.
// SPDX-License-Identifier: AGPL-3.0-or-later
import createIRL2FA from './irl2fa.mjs';
import {
  BTN, Device, World, DISPLAY_W, DISPLAY_H, DISPLAY_STRIDE, POWER, irLinkQuality,
  chargeMAs, lifeDays, drainMAhPerYear, profileLifeYears, cellMv, base32Decode, totpWith,
} from './irl2fa-device.js';

const $ = (s, root = document) => root.querySelector(s);
const units = [...document.querySelectorAll('.unit')];
const stage = $('#stage');
const reduceMotion = matchMedia('(prefers-reduced-motion: reduce)').matches;

const SCREEN_LABEL = {
  menu: 'Menu', codes: 'Codes', code: 'Code', contacts: 'Contacts', contact: 'Contact words',
  pair: 'Pairing', sas: 'Compare symbols', text: 'Typing', code_opts: 'Code options',
  confirm: 'Confirm', time: 'Set time', tz: 'Time zone', pin: 'PIN', message: 'Message', about: 'About',
  simple: 'Simple mode', simple_intro: 'Simple mode: about',
};

const state = {
  warp: 1,
  simUtc: Date.now(),
  skew: [0, 0],
  active: 0,
  profile: 'normal',
  kase: 'typ',
  // Charge (µAh) each device's cell has given up to fast-forwarded time.
  skippedUAh: [0, 0],
  skippedDays: 0,
  ir: { lastAt: -1e9 },
  held: [null, null],
};

let devices = [];
let world = null;
let screens = [];

// ------------------------------------------------------------------
// e-paper rendering
//
// The panel shows what the firmware sent it, the way an SSD1680 panel does:
//  - a full refresh (FULL in the firmware: wake, sleep, and every
//    FULL_REFRESH_EVERY = 5 partial ones) runs the flashing waveform for the
//    panel's full refresh time (3 s): the old image inverted, black, white,
//    black, white, the new image inverted, then the new image. It clears
//    all ghosting.
//  - a partial refresh (0.42 s) drives only the pixels that change, with a
//    short flicker, and leaves a faint residue of what was there: ghosting
//    builds up over the partial refreshes until the next full one.
// Times run on the simulation clock, so time-warp speeds them up too.

const PAPER = [0xe6, 0xe2, 0xd5];
const INK = [0x24, 0x25, 0x22];
const UP = 4; // offscreen upscale factor before the smooth downscale
const GHOST_STEP = 0.045; // residue left by one partial update of a pixel
const GHOST_MAX = 0.22;
const N_PX = DISPLAY_W * DISPLAY_H;

function px(fb, i) {
  return fb ? (fb[(i / DISPLAY_W | 0) * DISPLAY_STRIDE + ((i % DISPLAY_W) >> 3)] >> (7 - ((i % DISPLAY_W) & 7))) & 1 : 0;
}

/** Renders a 1-bpp frame (with ghost residue g in -1..1 per pixel: + is a
 * dark residue on white, - a light one on black) to an upscaled canvas.
 * `pick(i)` overrides a pixel: 0 white, 1 black, undefined = the frame. */
function toCanvas(fb, { invert = false, ghost = null, pick = null } = {}) {
  const small = document.createElement('canvas');
  small.width = DISPLAY_W;
  small.height = DISPLAY_H;
  const sctx = small.getContext('2d');
  const img = sctx.createImageData(DISPLAY_W, DISPLAY_H);
  const d = img.data;
  for (let i = 0; i < N_PX; i++) {
    let black = px(fb, i);
    if (invert) black ^= 1;
    let k = black; // 0 = paper .. 1 = ink
    if (pick) { const p = pick(i); if (p !== undefined) k = p; }
    else if (ghost) { const g = ghost[i]; k = black ? 1 + Math.min(0, g) : Math.max(0, g); }
    const o = i * 4;
    d[o] = PAPER[0] + (INK[0] - PAPER[0]) * k;
    d[o + 1] = PAPER[1] + (INK[1] - PAPER[1]) * k;
    d[o + 2] = PAPER[2] + (INK[2] - PAPER[2]) * k;
    d[o + 3] = 255;
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

// Full-refresh waveform phases, as fractions of the refresh time.
const FULL_PHASES = [
  [0.10, 'oldInv'], [0.22, 'black'], [0.34, 'white'], [0.46, 'black'], [0.58, 'white'],
  [0.78, 'newInv'], [1.0, 'settle'],
];

class Screen {
  constructor(canvas) {
    this.c = canvas;
    this.ctx = canvas.getContext('2d');
    this.fb = null;
    this.ghost = new Float32Array(N_PX);
    this.partials = 0; // since the last full refresh
    this.img = toCanvas(null);
    this.anim = null;
    this.dirty = true;
  }

  /** The firmware sent a frame. `t` is the simulation clock (ms). */
  show(fb, full, t) {
    const prev = this.fb;
    const prevImg = this.img;
    if (full) {
      this.ghost.fill(0);
      this.partials = 0;
    } else {
      // Pixels that change keep a little of their old state.
      for (let i = 0; i < N_PX; i++) {
        const a = px(prev, i), b = px(fb, i);
        if (a !== b) {
          const g = this.ghost[i] + (a ? GHOST_STEP : -GHOST_STEP);
          this.ghost[i] = Math.max(-GHOST_MAX, Math.min(GHOST_MAX, g));
        }
      }
      this.partials++;
    }
    this.fb = fb;
    this.img = toCanvas(fb, { ghost: this.ghost });
    const dur = (full ? POWER.epd.full_refresh_s : POWER.epd.partial_refresh_s) * 1000;
    if (reduceMotion || !prev) { this.anim = null; }
    else if (full) {
      this.anim = { full, t0: t, dur, frames: { oldInv: toCanvas(prev, { invert: true }), newInv: toCanvas(fb, { invert: true }) } };
    } else {
      // Mid-update: changed pixels are driven grey, then flick to the new state.
      const mid = toCanvas(fb, { pick: (i) => (px(prev, i) !== px(fb, i) ? 0.55 : undefined) });
      this.anim = { full, t0: t, dur, frames: { prevImg, mid } };
    }
    this.dirty = true;
  }

  get busy() { return !!this.anim; }

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

  draw(t) {
    this.fit();
    if (!this.dirty && !this.anim) return;
    const { ctx, c } = this;
    const W = c.width, H = c.height;
    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = 'high';
    ctx.globalAlpha = 1;
    const fill = (rgb) => { ctx.fillStyle = `rgb(${rgb.join(',')})`; ctx.fillRect(0, 0, W, H); };
    const img = (canvas, a = 1) => { ctx.globalAlpha = a; ctx.drawImage(canvas, 0, 0, W, H); ctx.globalAlpha = 1; };
    const a = this.anim;
    if (a) {
      const f = Math.max(0, (t - a.t0) / a.dur);
      if (f >= 1) { this.anim = null; img(this.img); }
      else if (a.full) {
        const phase = FULL_PHASES.find(([end]) => f < end)[1];
        if (phase === 'oldInv') img(a.frames.oldInv);
        else if (phase === 'black') fill(INK);
        else if (phase === 'white') fill(PAPER);
        else if (phase === 'newInv') img(a.frames.newInv);
        else { fill(PAPER); img(this.img, (f - 0.78) / 0.22); }
      } else if (f < 0.45) {
        img(a.frames.prevImg);
        img(a.frames.mid, f / 0.45);
      } else {
        img(a.frames.mid);
        img(this.img, (f - 0.45) / 0.55);
      }
    } else {
      img(this.img);
    }
    // Paper grain.
    ctx.globalCompositeOperation = 'multiply';
    ctx.globalAlpha = 0.07;
    ctx.drawImage(GRAIN, 0, 0, W, H);
    ctx.globalAlpha = 1;
    ctx.globalCompositeOperation = 'source-over';
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
    const rgb = p.corrupted ? '217, 119, 6' : IR_RGB;
    const h = Math.min(head, stop);
    const fade = head > stop ? Math.max(0, 1 - (head - stop) * (p.lost ? 2.5 : 4)) : 1;
    if (fade <= 0) continue;
    ctx.shadowColor = `rgba(${rgb}, ${0.9 * fade})`;
    ctx.shadowBlur = 12;
    ctx.lineWidth = 3;
    ctx.setLineDash(p.corrupted ? [4, 3] : []);
    ctx.strokeStyle = `rgba(${rgb}, ${0.85 * fade})`;
    strokeRange(ctx, path, h - 0.22, h);
    ctx.setLineDash([]);
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

function onIr(from, to, bytes, delivered, lost, reason) {
  const now = performance.now();
  state.ir.lastAt = now;
  // Throttle the animation at high time-warp.
  const last = beam.pulses.findLast?.((p) => p.from === from);
  const corrupted = reason === 'corrupted';
  if (!last || now - last.t0 > 120) {
    beam.pulses.push({ from, to, t0: now, linked: world.linked, lost: world.linked && lost && !corrupted, corrupted });
  }
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
  d.setBatteryMv(cellMv(soc(i)));
  d._mv = 0;
  d.init(state.simUtc + state.skew[i] * 1000);
  screens[i].fb = null;
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

const USABLE_UAH = POWER.battery.usable_mAh * 1000;

/** µAh device i's cell has delivered: what the simulation counted, at the
 * chosen case's currents, plus fast-forwarded time. */
function usedUAh(i) {
  return chargeMAs(devices[i].energy(), state.kase) / 3.6 + state.skippedUAh[i];
}
function soc(i) {
  return devices.length ? Math.max(0, 1 - usedUAh(i) / USABLE_UAH) : 1;
}

function refreshEnergy() {
  devices.forEach((d, i) => {
    const e = d.energy();
    const mAs = chargeMAs(e, state.kase);
    const total = (e.awakeMs + e.asleepMs) / 1000;
    const avg = total > 0 ? mAs / total : 0;
    const left = soc(i);
    const leftMAh = left * POWER.battery.usable_mAh;
    setEnergyCell(`${i}:used`, fmtCharge(usedUAh(i)));
    const mv = cellMv(left);
    setEnergyCell(`${i}:mv`, `${(mv / 1000).toFixed(2)} V`);
    // The firmware reads the cell through VREFINT: feed it the new voltage.
    if (Math.abs(mv - (d._mv || 0)) >= 10) { d._mv = mv; d.setBatteryMv(mv); }
    setEnergyCell(`${i}:split`, `${fmtDur(e.awakeMs)} / ${fmtDur(e.asleepMs)}`);
    setEnergyCell(`${i}:ref`, `${e.full} / ${e.fast}`);
    setEnergyCell(`${i}:ir`, fmtDur(e.irMs));
    setEnergyCell(`${i}:avg`, total > 1 ? fmtCurrent(avg) : '–');
    setEnergyCell(`${i}:life`, total > 1 ? fmtLife(lifeDays(avg) * left) : '–');
    setEnergyCell(`${i}:left`, fmtLife((leftMAh / drainMAhPerYear(state.profile, state.kase)) * 365));
    const pct = Math.round(left * 1000) / 10;
    const meter = document.querySelector(`[data-b="${i}:meter"]`);
    meter.setAttribute('aria-valuenow', String(Math.round(pct)));
    meter.setAttribute('aria-valuetext', `${pct.toFixed(1)} percent, ${(mv / 1000).toFixed(2)} volts`);
    meter.classList.toggle('low', left < 0.2 && left > 0.05);
    meter.classList.toggle('empty', left <= 0.05);
    document.querySelector(`[data-b="${i}:bar"]`).style.width = `${pct}%`;
    const t = `${pct.toFixed(pct < 10 ? 1 : 0)} %`;
    const el = document.querySelector(`[data-b="${i}:soc"]`);
    if (el.textContent !== t) el.textContent = t;
  });
}

function showModel() {
  const p = POWER.profiles[state.profile];
  const c = POWER.cases[state.kase];
  const years = profileLifeYears(state.profile, state.kase);
  $('#profile-name').textContent = `A new cell, ${p.uses_per_day} uses a day, ${p.pairings_per_year} pairings a year`;
  $('#profile-life').textContent = fmtLife(years * 365);
  $('#profile-life').title = `${(years * 365).toFixed(0)} days (${state.kase === 'typ' ? 'typical' : 'worst-case'} currents)`;
  const u = POWER.measured.use;
  $('#model-src').textContent = 'Generated from hardware/power: datasheet currents, and timings measured by running ' +
    `the real firmware in an emulator. One measured use: ${u.awake_s.toFixed(0)} s awake, ` +
    `${u.full_refreshes} full and ${u.partial_refreshes} partial refreshes, ${c.use_mAs.toFixed(1)} mA·s.`;
  const list = $('#model-list');
  list.replaceChildren(...[
    `asleep ${c.sleep_uA} µA`,
    `awake (MCU, RTC) ${c.awake_mA} mA average`,
    `full refresh ${POWER.epd.full_refresh_s} s, ${c.full_refresh_mAs} mA·s`,
    `partial refresh ${POWER.epd.partial_refresh_s} s, ${c.partial_refresh_mAs} mA·s`,
    `IR on ${c.ir_mA} mA average (LED at its measured duty)`,
    `pairing ${c.pairing_mAs.toFixed(0)} mA·s`,
    `cell ${POWER.battery.part}, ${POWER.battery.usable_mAh} mAh usable, ${POWER.battery.self_discharge_pct_per_year} %/year self-discharge`,
  ].map((t) => Object.assign(document.createElement('li'), { textContent: t })));
}

/** Skips `days` of the chosen usage on both devices. */
function fastForward(days) {
  const ms = days * 864e5;
  const uAh = (drainMAhPerYear(state.profile, state.kase) * 1000 * days) / 365;
  for (let i = 0; i < 2; i++) {
    state.skippedUAh[i] = Math.min(USABLE_UAH, state.skippedUAh[i] + uAh);
    devices[i].skip(ms);
    devices[i].due = 0;
  }
  state.simUtc += ms;
  state.skippedDays += days;
  world.advance(0);
  const p = POWER.profiles[state.profile];
  $('#ff-note').textContent = `Skipped ${state.skippedDays} days in all at ${p.uses_per_day} uses a day ` +
    `(${fmtCharge(uAh)} this step).` + (soc(0) <= 0 ? ' The cells are flat.' : '');
  refreshEnergy();
  refreshStatus();
}

function showLink() {
  const box = $('#linkq');
  const q = irLinkQuality(world.link);
  const okReveal = q.frameOk(62); // the longest frame, REVEAL
  let text, cls;
  if (!world.linked) { text = 'Link: out of line of sight'; cls = 'dead'; }
  else {
    const ber = q.ber < 1e-9 ? 'under 10⁻⁹' : q.ber.toExponential(1);
    text = `Link: bit error rate ${ber}, ${Math.round(okReveal * 100)} % of frames intact`;
    cls = okReveal > 0.9 ? '' : okReveal > 0.2 ? 'weak' : 'dead';
  }
  box.classList.toggle('weak', cls === 'weak');
  box.classList.toggle('dead', cls === 'dead');
  $('#linkq-bar').style.width = `${world.linked ? Math.round(okReveal * 100) : 0}%`;
  $('#linkq-text').textContent = text;
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
    $('[data-role="busy"]', units[i]).hidden = d.busyMs() <= 0;
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
  const st = world.stats;
  const bad = [st.corrupted && `${st.corrupted} corrupted`, st.dropouts && `${st.dropouts} dropped`,
    st.collisions && `${st.collisions} collided`].filter(Boolean).join(', ');
  const t = st.frames === 0 ? 'IR idle'
    : `IR ${ago < 600 ? '●' : '○'} ${st.delivered} frames passed${bad ? `, ${bad}` : ''}`;
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
  for (const b of document.querySelectorAll('[data-warp]')) b.setAttribute('aria-checked', String(Number(b.dataset.warp) === w));
}

function wirePanels() {
  for (const b of document.querySelectorAll('[data-warp]')) b.addEventListener('click', () => setWarp(Number(b.dataset.warp)));
  $('#now-btn').addEventListener('click', () => {
    state.simUtc = Date.now();
    devices.forEach((d, i) => { d.setUnixMs(state.simUtc + state.skew[i] * 1000); d.due = 0; });
    setWarp(1);
  });
  $('#together').addEventListener('change', (e) => {
    world.linked = e.target.checked;
    stage.classList.toggle('together', world.linked);
    showLink();
  });
  const geo = () => {
    world.link.distanceCm = Number($('#dist').value);
    world.link.angleDeg = Number($('#angle').value);
    $('#dist-out').textContent = `${world.link.distanceCm.toFixed(1)} cm`;
    $('#angle-out').textContent = `${world.link.angleDeg}°`;
    showLink();
  };
  $('#dist').addEventListener('input', geo);
  $('#angle').addEventListener('input', geo);
  geo();
  const sel = $('#profile');
  for (const [name, p] of Object.entries(POWER.profiles)) {
    sel.append(Object.assign(document.createElement('option'), {
      value: name, textContent: `${name[0].toUpperCase()}${name.slice(1)} (${p.uses_per_day}/day)`, selected: name === state.profile,
    }));
  }
  sel.addEventListener('change', () => { state.profile = sel.value; showModel(); refreshEnergy(); });
  for (const b of document.querySelectorAll('[data-case]')) {
    b.addEventListener('click', () => {
      state.kase = b.dataset.case;
      for (const o of document.querySelectorAll('[data-case]')) o.setAttribute('aria-checked', String(o === b));
      showModel();
      refreshEnergy();
    });
  }
  for (const b of document.querySelectorAll('[data-ff]')) b.addEventListener('click', () => fastForward(Number(b.dataset.ff)));
  $('#new-cells').addEventListener('click', () => {
    state.skippedUAh = [0, 0];
    state.skippedDays = 0;
    devices.forEach((d) => d.resetEnergy());
    $('#ff-note').textContent = 'New cells fitted.';
    refreshEnergy();
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
    state.ir = { lastAt: -1e9 };
    for (const k of Object.keys(world.stats)) world.stats[k] = 0;
    state.skippedUAh = [0, 0];
    state.skippedDays = 0;
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
  for (const s of screens) s.draw(state.simUtc);
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
  world.onRefresh = (d, full) => screens[devices.indexOf(d)].show(d.framebuffer(), full, state.simUtc);
  world.onIr = onIr;
  units.forEach((_, i) => wireButtons(i));
  wirePanels();
  for (let i = 0; i < 2; i++) bootDevice(i);
  setActive(0);
  world.advance(0);
  showModel();
  refreshStatus();
  window.addEventListener('resize', () => screens.forEach((s) => { s.dirty = true; }));
  // For automated checks and the curious: the live simulation.
  window.irl2fa = { devices, world, state, BTN, press, release };
  requestAnimationFrame(frame);
}

main();
