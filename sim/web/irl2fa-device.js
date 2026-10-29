// Thin JS wrapper around one WebAssembly instance of the IRL2FA firmware
// (one instance = one device), plus a World that runs two devices in
// lock-step simulated time and carries IR bytes between them.
// Shared by the page (app.js) and the Node tests (sim/test).
// SPDX-License-Identifier: AGPL-3.0-or-later

export const BTN = Object.freeze({ UP: 1, DOWN: 2, LEFT: 4, RIGHT: 8, CENTER: 16 });
export const DISPLAY_W = 250;
export const DISPLAY_H = 122;
export const DISPLAY_STRIDE = 32;
export const FONT = Object.freeze({ SMALL: 0, MEDIUM: 1, LARGE: 2, MONO: 3, DIGITS: 4 });

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
  setUnixMs(ms) { this.m._sim_set_unix_ms(Math.floor(ms)); }
  unix() { return this.m._sim_get_unix(); }
  setBatteryMv(mv) { this.m._sim_set_battery_mv(mv); this.due = 0; }
  screen() { return this.m.UTF8ToString(this.m._sim_screen_name()); }
  irOn() { return !!this.m._sim_ir_on(); }
  unlocked() { return !!this.m._sim_unlocked(); }
  pinSet() { return !!this.m._sim_pin_set(); }

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

/** Two (or more) devices sharing simulated time and an IR link. */
export class World {
  constructor(devices, { random = Math.random } = {}) {
    this.devices = devices;
    this.linked = false;     // devices held top edge to top edge
    this.noisy = false;      // drop `dropRate` of frames
    this.dropRate = 0.1;
    this.random = random;
    this.elapsedMs = 0;
    this.onRefresh = null;   // (device, isFull) => void
    this.onIr = null;        // (fromIdx, toIdx, bytes, delivered) => void
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

  _transfer() {
    const ds = this.devices;
    for (let i = 0; i < ds.length; i++) {
      const bytes = ds[i].irTake();
      if (!bytes.length) continue;
      for (let j = 0; j < ds.length; j++) {
        if (j === i) continue;
        const to = ds[j];
        const lost = this.noisy && this.random() < this.dropRate;
        const delivered = this.linked && !to.sleeping && to.irOn() && !lost;
        if (delivered) to.irRx(bytes);
        if (this.onIr) this.onIr(i, j, bytes, delivered, lost);
      }
    }
  }

  /** Runs `ms` of simulated time, polling each awake device when it asks. */
  advance(ms) {
    let left = Math.max(0, Math.floor(ms));
    for (let guard = 0; guard < 1e7; guard++) {
      for (const d of this.devices) if (!d.sleeping && d.due <= 0) this._poll(d);
      this._transfer();
      if (left <= 0) break;
      let step = left;
      for (const d of this.devices) if (!d.sleeping) step = Math.min(step, Math.max(1, d.due));
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

export const POWER_DEFAULTS = Object.freeze({
  sleep_uA: 1.1,
  awake_mA: 0.9,
  full_refresh: { mA: 3.5, s: 3.0 },
  fast_refresh: { mA: 3.5, s: 0.6 },
  ir_mA: 6,
  battery_mAh: 540,
  battery: 'CR2450',
  profile: {
    name: '20 uses/day',
    uses_per_day: 20,
    awake_s_per_use: 70,
    full_refreshes_per_use: 2,
    fast_refreshes_per_use: 6,
    ir_s_per_use: 0,
  },
});

/** Merges a power model over the defaults, keeping only known keys with
 * numeric values so a differently shaped file cannot break the page. */
export function mergePowerModel(base, extra) {
  const out = JSON.parse(JSON.stringify(base));
  if (!extra || typeof extra !== 'object') return out;
  const walk = (dst, src) => {
    for (const k of Object.keys(dst)) {
      if (!(k in src)) continue;
      if (dst[k] && typeof dst[k] === 'object' && src[k] && typeof src[k] === 'object') walk(dst[k], src[k]);
      else if (typeof dst[k] === typeof src[k]) dst[k] = src[k];
    }
  };
  walk(out, extra);
  return out;
}

/** Charge in mA·s for a set of counters ({awakeMs, asleepMs, irMs, full, fast}). */
export function chargeMAs(model, e) {
  return (
    (e.awakeMs / 1000) * model.awake_mA +
    (e.asleepMs / 1000) * (model.sleep_uA / 1000) +
    (e.irMs / 1000) * model.ir_mA +
    e.full * model.full_refresh.mA * model.full_refresh.s +
    e.fast * model.fast_refresh.mA * model.fast_refresh.s
  );
}

/** Battery life in days at an average current (mA). */
export function lifeDays(model, avgMA) {
  return avgMA > 0 ? model.battery_mAh / avgMA / 24 : Infinity;
}

/** Average current (mA) of a daily usage profile. */
export function profileAvgMA(model, p = model.profile) {
  const day = 86400;
  const awake = p.uses_per_day * p.awake_s_per_use;
  const mAs = chargeMAs(model, {
    awakeMs: awake * 1000,
    asleepMs: Math.max(0, day - awake) * 1000,
    irMs: p.uses_per_day * p.ir_s_per_use * 1000,
    full: p.uses_per_day * p.full_refreshes_per_use,
    fast: p.uses_per_day * p.fast_refreshes_per_use,
  });
  return mAs / day;
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
