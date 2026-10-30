// Thin JS wrapper around one WebAssembly instance of the IRL2FA firmware
// (one instance = one device), plus a World that runs two devices in
// lock-step simulated time and carries IR bytes between them.
// Shared by the page (app.js) and the Node tests (sim/test).
// SPDX-License-Identifier: AGPL-3.0-or-later
import POWER from './power-model.js';

export { POWER };
export const BTN = Object.freeze({ UP: 1, DOWN: 2, LEFT: 4, RIGHT: 8, CENTER: 16 });
export const DISPLAY_W = 250;
export const DISPLAY_H = 122;
export const DISPLAY_STRIDE = 32;
export const FONT = Object.freeze({ SMALL: 0, MEDIUM: 1, LARGE: 2, MONO: 3, DIGITS: 4, HUGE: 5 });

export class Device {
  constructor(mod, name) {
    this.m = mod;
    this.name = name;
    this.buttons = 0;
    this.due = 0;          // ms until the firmware wants polling again
    this.sleeping = false;
    this.ref = { full: 0, fast: 0 };
    this.io = mod._sim_io_buf();
    this.ioCap = mod._sim_io_cap();
    this.str = mod._sim_str_buf();
    this.strCap = mod._sim_str_cap();
    // How long a refresh keeps the core from polling: the panel's BUSY time
    // plus the STM32's own time (drawing, then the driver: supply on,
    // resets, RAM transfer), both from the same model as the battery budget.
    const ov = POWER.epd.overhead_s || 0;
    mod._sim_set_epd_timing(Math.round((POWER.epd.full_refresh_s + ov) * 1000),
      Math.round((POWER.epd.partial_refresh_s + ov) * 1000));
  }

  static async create(factory, name, moduleOpts = {}) {
    return new Device(await factory(moduleOpts), name);
  }

  /** Blank board (erased flash) with a 64-bit RNG seed given as two uint32. */
  reset(seedLo, seedHi) {
    this.m._sim_reset(seedLo >>> 0, seedHi >>> 0);
    this.buttons = 0;
    this.due = 0;
    this.sleeping = false;
    this.ref = { full: 0, fast: 0 };
  }

  init(unixMs) {
    if (unixMs !== undefined) this.setUnixMs(unixMs);
    this.m._sim_init();
    this.due = 0;
    this.sleeping = false;
  }

  poll() {
    const r = this.m._sim_poll();
    if (r < 0) {
      this.sleeping = true;
      this.due = Infinity;
    } else {
      this.due = r;
    }
    return r;
  }

  /** Joystick state. A press while asleep wakes the device (as the EXTI
   * line does on the board); that press is not taken as a key. */
  setButtons(mask) {
    this.buttons = mask;
    this.m._sim_set_buttons(mask);
    if (this.sleeping && mask) {
      this.m._sim_wake();
      this.sleeping = false;
    }
    if (!this.sleeping) this.due = 0;
  }

  advance(ms) { this.m._sim_advance(ms); }
  /** ms until the e-paper refresh in progress finishes (0 = idle). */
  busyMs() { return this.m._sim_busy_ms(); }
  /** Jumps the clock forward as if asleep the whole time (fast-forward). */
  skip(ms) { this.m._sim_skip_ms(ms); }
  setUnixMs(ms) { this.m._sim_set_unix_ms(Math.floor(ms)); }
  unix() { return this.m._sim_get_unix(); }
  setBatteryMv(mv) { this.m._sim_set_battery_mv(mv); this.due = 0; }
  screen() { return this.m.UTF8ToString(this.m._sim_screen_name()); }
  irOn() { return !!this.m._sim_ir_on(); }
  unlocked() { return !!this.m._sim_unlocked(); }
  pinSet() { return !!this.m._sim_pin_set(); }
  /** Simple ("vulnerable") mode is on: turned on through the device's own Settings. */
  simpleMode() { return !!this.m._sim_simple_mode(); }

  /** A copy of the panel's current 1-bpp image. */
  framebuffer() {
    const p = this.m._sim_framebuffer();
    return this.m.HEAPU8.slice(p, p + DISPLAY_STRIDE * DISPLAY_H);
  }

  refreshes() {
    return { full: this.m._sim_refreshes(1), fast: this.m._sim_refreshes(0) };
  }

  irTake() {
    const n = this.m._sim_ir_take();
    return this.m.HEAPU8.slice(this.io, this.io + n);
  }

  irRx(bytes) {
    for (let off = 0; off < bytes.length; off += this.ioCap) {
      const chunk = bytes.subarray(off, off + this.ioCap);
      this.m.HEAPU8.set(chunk, this.io);
      this.m._sim_ir_rx(chunk.length);
    }
    this.due = Math.min(this.due, 0);
  }

  energy() {
    const m = this.m;
    return {
      awakeMs: m._sim_energy_awake_ms(),
      asleepMs: m._sim_energy_asleep_ms(),
      irMs: m._sim_energy_ir_ms(),
      busyMs: m._sim_energy_busy_ms(),
      full: m._sim_energy_full(),
      fast: m._sim_energy_fast(),
      wakes: m._sim_energy_wakes(),
    };
  }
  resetEnergy() { this.m._sim_energy_reset(); }

  // ---- sim-only helpers ----

  _putStrings(...parts) {
    const enc = new TextEncoder();
    const bytes = [];
    for (const p of parts) bytes.push(...enc.encode(p), 0);
    if (bytes.length > this.strCap) throw new Error('string too long');
    this.m.HEAPU8.set(bytes, this.str);
  }

  /** Adds a 6-digit/30 s/SHA1 code straight into the store (demo data).
   * Returns the slot or -1 (locked, bad secret, full). */
  addTotp(label, base32) {
    this._putStrings(label, base32);
    const slot = this.m._sim_add_totp();
    if (slot >= 0) { this.m._sim_refresh_ui(); this.due = 0; }
    return slot;
  }

  count(kind) { return this.m._sim_count(kind === 'totp' ? 1 : 2); }
  totpCode(n) { return this.m._sim_totp_code(n); }
  recordName(kind, n) {
    if (!this.m._sim_record_name(kind === 'totp' ? 1 : 2, n)) return null;
    return this.m.UTF8ToString(this.str);
  }
  /** {give, receive} word indices of the nth contact, `offset` 15 s windows from now. */
  contactWords(n, offset = 0) {
    const v = this.m._sim_contact_words(n, offset);
    if (v === -1) return null;
    return { give: v & 0xffff, receive: (v >>> 16) & 0xffff };
  }
  word(i) { return this.m.UTF8ToString(this.m._sim_word(i)); }

  /** Renders `text` with a firmware font into a scratch framebuffer. */
  probeText(font, x, y, align, text) {
    this._putStrings(text);
    const p = this.m._sim_probe_text(font, x, y, align);
    return this.m.HEAPU8.slice(p, p + DISPLAY_STRIDE * DISPLAY_H);
  }
}

// ---- IR link model ----
//
// The pairing link is a VSMB2948SL LED (±25° half-intensity angle) shining
// on a VEMT2523SLX01 phototransistor (±35°) across the gap between the two
// devices' top edges, NRZ at 4800 baud. The model:
//   received signal  ∝ cos^mTx(θ) · cos^mRx(θ) / d²   (Lambertian lobes with
//                    the parts' half angles, inverse square in distance)
//   signal/noise q   = Q0 · (D0 / d)² · gain(θ)
//   bit error rate   = Q(q), the Gaussian tail (NRZ, decision at mid-level)
//   frame dropout    = 1 / (1 + e^(4 (q − 1.8))): the receiver missing a
//                      start edge and losing byte sync for the frame
// Q0 and D0 are chosen so the link is clean to about 8 cm on axis, marginal
// at 9-10 cm and dead beyond (docs/research/parts-research-2026-10-29.md
// §2.3 expects 2-5 cm with margin). They are assumptions until a board is
// measured. Frames take their real airtime (10 bits per byte), and the link
// is half duplex: a frame that arrives while the receiving device is itself
// transmitting is lost (its phototransistor sees its own LED).
export const IR_LINK = Object.freeze({ Q0: 6, D0_CM: 6, TX_HALF_DEG: 25, RX_HALF_DEG: 35, MIN_CM: 0.5 });

const lobe = (halfDeg) => Math.log(0.5) / Math.log(Math.cos((halfDeg * Math.PI) / 180));

/** Complementary error function (Abramowitz & Stegun 7.1.26, |err| < 1.5e-7). */
export function erfc(x) {
  const z = Math.abs(x);
  const t = 1 / (1 + 0.3275911 * z);
  const y = t * (0.254829592 + t * (-0.284496736 + t * (1.421413741 + t * (-1.453152027 + t * 1.061405429))));
  const r = y * Math.exp(-z * z);
  return x >= 0 ? r : 2 - r;
}

/** Link quality for two devices `distanceCm` apart (top edge to top edge)
 * and `angleDeg` off axis: {q, ber, dropout, frameOk(nBytes)}. */
export function irLinkQuality({ distanceCm, angleDeg = 0 }) {
  const th = (Math.min(Math.abs(angleDeg), 90) * Math.PI) / 180;
  const c = Math.cos(th);
  const gain = c <= 1e-6 ? 0 : c ** lobe(IR_LINK.TX_HALF_DEG) * c ** lobe(IR_LINK.RX_HALF_DEG);
  const d = Math.max(IR_LINK.MIN_CM, distanceCm);
  const q = IR_LINK.Q0 * (IR_LINK.D0_CM / d) ** 2 * gain;
  const ber = Math.min(0.5, 0.5 * erfc(q / Math.SQRT2));
  const dropout = 1 / (1 + Math.exp(4 * (q - 1.8)));
  return {
    q, ber, dropout,
    /** Probability that a frame of n bytes arrives with no bit error. */
    frameOk: (n) => (1 - dropout) * (1 - ber) ** (8 * n),
  };
}

/** Two (or more) devices sharing simulated time and an IR link. */
export class World {
  constructor(devices, { random = Math.random, airtime = true } = {}) {
    this.devices = devices;
    this.linked = false;     // facing each other, top edge to top edge
    this.link = { distanceCm: 2, angleDeg: 0 };
    this.noisy = false;      // additionally drop `dropRate` of frames
    this.dropRate = 0.1;
    this.airtime = airtime;  // frames take 10 bits/byte at the IR baud rate
    this.baud = POWER.ir.baud;
    this.random = random;
    this.elapsedMs = 0;
    this.onRefresh = null;   // (device, isFull) => void
    this.onIr = null;        // (fromIdx, toIdx, bytes, delivered, lost, reason) => void
    this.pending = [];       // bytes in flight
    this.txLog = devices.map(() => []);   // [start, end] of each device's frames
    this.txUntil = devices.map(() => 0);
    this.stats = { frames: 0, delivered: 0, corrupted: 0, dropouts: 0, collisions: 0, unheard: 0 };
  }

  _poll(d) {
    d.poll();
    const r = d.refreshes();
    if (this.onRefresh) {
      if (r.full !== d.ref.full) this.onRefresh(d, true);
      else if (r.fast !== d.ref.fast) this.onRefresh(d, false);
    }
    d.ref = r;
  }

  quality() { return irLinkQuality(this.link); }

  /** Takes what each device has sent and puts it on the air, one byte per
   * 10 bit times (start, 8 data, stop): the receiver gets each byte when
   * its stop bit has arrived, as the UART interrupt delivers it on the
   * board, so a device can hear that the other is mid-frame. */
  _send() {
    const ds = this.devices;
    const now = this.elapsedMs;
    const bt = this.airtime ? (10 * 1000) / this.baud : 0;
    const q = this.quality();
    for (let i = 0; i < ds.length; i++) {
      const bytes = ds[i].irTake();
      if (!bytes.length) continue;
      const start = Math.max(now, this.txUntil[i]);
      const end = start + bytes.length * bt;
      this.txUntil[i] = end;
      this.txLog[i].push([start, end]);
      for (let j = 0; j < ds.length; j++) {
        if (j === i) continue;
        // Per frame and receiver: does it lose byte sync for this frame?
        const frame = {
          from: i, to: j, bytes, n: bytes.length, got: 0,
          dropped: q.dropout > 0 && this.random() < q.dropout || (this.noisy && this.random() < this.dropRate),
          collided: false, flipped: false, unheard: false,
        };
        for (let k = 0; k < bytes.length; k++) {
          this.pending.push({ at: start + (k + 1) * bt, t0: start + k * bt, frame, k });
        }
      }
    }
  }

  _overlaps(j, start, end) {
    const log = this.txLog[j];
    for (const [a, b] of log) if (a < end && b > start) return true;
    return false;
  }

  /** Delivers the bytes whose stop bit has arrived. */
  _deliver() {
    const now = this.elapsedMs;
    if (!this.pending.some((p) => p.at <= now)) return;
    const due = this.pending.filter((p) => p.at <= now).sort((a, b) => a.at - b.at);
    this.pending = this.pending.filter((p) => p.at > now);
    const q = this.quality();
    for (const p of due) {
      const f = p.frame;
      const to = this.devices[f.to];
      let byte = f.bytes[p.k];
      if (!this.linked || to.sleeping || !to.irOn()) f.unheard = true;
      else if (!f.dropped) {
        if (this.airtime && this._overlaps(f.to, p.t0, p.at)) {
          // Its own LED is lit: the phototransistor sees both. Garbage.
          f.collided = true;
          byte = Math.floor(this.random() * 256);
        } else if (q.ber > 1e-12) {
          for (let b = 0; b < 8; b++) if (this.random() < q.ber) { byte ^= 1 << b; f.flipped = true; }
        }
        to.irRx(Uint8Array.of(byte));
      }
      if (p.k === f.n - 1) this._frameDone(f);
    }
    for (const log of this.txLog) while (log.length && log[0][1] < now - 2000) log.shift();
  }

  _frameDone(f) {
    const reason = f.unheard ? 'unheard' : f.dropped ? 'dropout' : f.collided ? 'collision' : f.flipped ? 'corrupted' : 'ok';
    this.stats.frames++;
    if (reason === 'ok') this.stats.delivered++;
    else if (reason === 'corrupted') this.stats.corrupted++;
    else if (reason === 'dropout') this.stats.dropouts++;
    else if (reason === 'collision') this.stats.collisions++;
    else this.stats.unheard++;
    if (this.onIr) this.onIr(f.from, f.to, f.bytes, reason === 'ok', reason !== 'ok' && reason !== 'unheard', reason);
  }

  /** Runs `ms` of simulated time, polling each awake device when it asks. */
  advance(ms) {
    let left = Math.max(0, Math.floor(ms));
    for (let guard = 0; guard < 1e7; guard++) {
      for (const d of this.devices) if (!d.sleeping && d.due <= 0) this._poll(d);
      this._send();
      this._deliver();
      if (left <= 0) break;
      let step = left;
      for (const d of this.devices) if (!d.sleeping) step = Math.min(step, Math.max(1, d.due));
      for (const p of this.pending) step = Math.min(step, Math.max(1, Math.ceil(p.at - this.elapsedMs)));
      for (const d of this.devices) {
        d.advance(step);
        if (!d.sleeping) d.due -= step;
      }
      left -= step;
      this.elapsedMs += step;
    }
  }

  /** Advances until pred() is true or `maxMs` passes; returns pred(). */
  until(pred, maxMs, stepMs = 20) {
    for (let t = 0; t < maxMs && !pred(); t += stepMs) this.advance(stepMs);
    return pred();
  }
}

// ---- energy model ----
//
// POWER is generated by hardware/power/power.py from the datasheet currents
// in hardware/power/model.json and the Renode measurements in
// hardware/power/measured.json, so the page, the tests and the battery
// budget cannot disagree. Per case ('typ' | 'worst'):
//   awake_mA            MCU (running + WFI) and RTC I2C, averaged over the
//                       measured use's awake time
//   full/partial_refresh_mAs  one refresh: BUSY time at the panel current
//                       plus its share of the panel's powered-idle time
//   ir_mA               LED (at its measured duty) + receiver, per second on
//   sleep_uA            Stop 2 + RTC + leakage

/** Charge in mA·s for a set of counters ({awakeMs, asleepMs, irMs, full, fast}). */
export function chargeMAs(e, kase = 'typ') {
  const c = POWER.cases[kase];
  return (
    (e.awakeMs / 1000) * c.awake_mA +
    (e.asleepMs / 1000) * (c.sleep_uA / 1000) +
    (e.irMs / 1000) * c.ir_mA +
    e.full * c.full_refresh_mAs +
    e.fast * c.partial_refresh_mAs
  );
}

/** mAh per year of a usage profile, as hardware/power/power.py counts it. */
export function profileMAhPerYear(profile = 'normal', kase = 'typ') {
  const p = POWER.profiles[profile];
  const c = POWER.cases[kase];
  const sleep = (c.sleep_uA / 1000) * 24 * 365;
  const use = (c.use_mAs * p.uses_per_day * 365) / 3600;
  const pair = (c.pairing_mAs * p.pairings_per_year) / 3600;
  return sleep + use + pair;
}

/** mAh a year including the cell's self-discharge. */
export function drainMAhPerYear(profile = 'normal', kase = 'typ') {
  const b = POWER.battery;
  return profileMAhPerYear(profile, kase) + (b.usable_mAh * b.self_discharge_pct_per_year) / 100;
}

/** Battery life of a fresh cell under a usage profile, in years. */
export function profileLifeYears(profile = 'normal', kase = 'typ') {
  return POWER.battery.usable_mAh / drainMAhPerYear(profile, kase);
}

/** Battery life in days at an average current (mA). */
export function lifeDays(avgMA) {
  return avgMA > 0 ? POWER.battery.usable_mAh / avgMA / 24 : Infinity;
}

/** Approximate CR2450 terminal voltage at light load against state of
 * charge (0..1), read off the shape of typical lithium coin-cell discharge
 * curves: flat near 3 V, then a knee in the last ~10 %. */
export function cellMv(soc) {
  const pts = [[0, 2000], [0.02, 2400], [0.05, 2600], [0.1, 2750], [0.2, 2850], [0.5, 2920], [0.9, 2980], [1, 3050]];
  const s = Math.max(0, Math.min(1, soc));
  for (let k = 1; k < pts.length; k++) {
    if (s <= pts[k][0]) {
      const [a, va] = pts[k - 1], [b, vb] = pts[k];
      return Math.round(va + ((vb - va) * (s - a)) / (b - a));
    }
  }
  return 3050;
}

// ---- reference TOTP (RFC 6238, SHA-1) for comparing with a phone ----

export function base32Decode(s) {
  const A = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ234567';
  const clean = s.toUpperCase().replace(/[\s=-]/g, '');
  const out = [];
  let bits = 0, acc = 0;
  for (const c of clean) {
    const v = A.indexOf(c);
    if (v < 0) return null;
    acc = (acc << 5) | v;
    bits += 5;
    if (bits >= 8) {
      out.push((acc >>> (bits - 8)) & 0xff);
      bits -= 8;
    }
  }
  return new Uint8Array(out);
}

/** TOTP with a caller-supplied HMAC-SHA1 (Web Crypto in the browser,
 * node:crypto in the tests). */
export async function totpWith(hmacSha1, key, unixSeconds, period = 30, digits = 6) {
  const counter = Math.floor(unixSeconds / period);
  const msg = new Uint8Array(8);
  let c = counter;
  for (let i = 7; i >= 0; i--) { msg[i] = c & 0xff; c = Math.floor(c / 256); }
  const h = await hmacSha1(key, msg);
  const o = h[h.length - 1] & 0x0f;
  const bin = ((h[o] & 0x7f) << 24) | (h[o + 1] << 16) | (h[o + 2] << 8) | h[o + 3];
  return String(bin % 10 ** digits).padStart(digits, '0');
}
