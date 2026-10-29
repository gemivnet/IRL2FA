// End-to-end tests of the WebAssembly build: two simulated devices driven
// only through the joystick, exactly as a person would.
// Run after sim/build.sh:   node --test sim/test
// SPDX-License-Identifier: AGPL-3.0-or-later
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHmac, randomBytes } from 'node:crypto';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const dist = new URL('../dist/', import.meta.url);
if (!existsSync(fileURLToPath(new URL('irl2fa.mjs', dist)))) {
  throw new Error('sim/dist is missing: run sim/build.sh first');
}
const { default: createIRL2FA } = await import(new URL('irl2fa.mjs', dist));
const {
  BTN, Device, World, FONT, DISPLAY_STRIDE, DISPLAY_H,
  POWER_DEFAULTS, chargeMAs, profileAvgMA, lifeDays, base32Decode, totpWith,
} = await import(new URL('irl2fa-device.js', dist));

const CS_LABEL = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 -_.@+&\'#';
const CS_BASE32 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ234567';
const T0 = 1800000007 * 1000; // 2027-01-15 08:00:07 UTC
const TITLE_H = 15;

async function newDevice(name) {
  const d = await Device.create(createIRL2FA, name);
  const seed = randomBytes(8);
  d.reset(seed.readUInt32LE(0), seed.readUInt32LE(4));
  d.init(T0);
  return d;
}

async function newWorld() {
  const a = await newDevice('A');
  const b = await newDevice('B');
  const w = new World([a, b]);
  w.advance(50);
  return { a, b, w };
}

function press(w, d, btn) {
  d.setButtons(btn);
  w.advance(30);
  d.setButtons(0);
  w.advance(30);
}

function presses(w, d, btn, n) {
  for (let i = 0; i < n; i++) press(w, d, btn);
}

/** Types on the character wheel (which starts at index 0 for empty text). */
function typeText(w, d, charset, s) {
  let cur = 0;
  for (const ch of s) {
    const want = charset.indexOf(ch);
    assert.ok(want >= 0, `${ch} not in charset`);
    const n = charset.length;
    const down = (want - cur + n) % n;
    const up = (cur - want + n) % n;
    if (down <= up) presses(w, d, BTN.DOWN, down);
    else presses(w, d, BTN.UP, up);
    press(w, d, BTN.RIGHT);
    cur = want;
  }
}

/** Compares rows [y0, y1) of two framebuffers from byte column `bx` on. */
function regionEqual(fa, fb, y0, y1, bx = 0) {
  for (let y = y0; y < y1; y++)
    for (let x = bx; x < DISPLAY_STRIDE; x++)
      if (fa[y * DISPLAY_STRIDE + x] !== fb[y * DISPLAY_STRIDE + x]) return false;
  return true;
}

function inkIn(f, y0, y1) {
  let n = 0;
  for (let i = y0 * DISPLAY_STRIDE; i < y1 * DISPLAY_STRIDE; i++) n += f[i] ? 1 : 0;
  return n;
}

const nodeHmacSha1 = async (key, msg) => new Uint8Array(createHmac('sha1', key).update(msg).digest());

async function pairBoth(w, a, b) {
  for (const d of [a, b]) {
    assert.equal(d.screen(), 'menu');
    presses(w, d, BTN.DOWN, 2); // "Pair with someone"
    press(w, d, BTN.CENTER);
    assert.equal(d.screen(), 'pair');
    assert.ok(d.irOn(), 'IR powered while pairing');
  }
  // Apart, nothing passes.
  w.advance(1500);
  assert.equal(a.screen(), 'pair');
  assert.equal(b.screen(), 'pair');
  // Held together: the commit/reveal exchange completes.
  w.linked = true;
  assert.ok(w.until(() => a.screen() === 'sas' && b.screen() === 'sas', 10000),
    `both on the SAS screen (a=${a.screen()} b=${b.screen()})`);
  w.advance(100);
}

test('two devices pair over IR, compare symbols, and show crosswise words', async () => {
  const { a, b, w } = await newWorld();
  await pairBoth(w, a, b);

  // The six symbols (rows 38..73) are drawn identically on both screens.
  const fa = a.framebuffer(), fb = b.framebuffer();
  assert.ok(inkIn(fa, 38, 74) > 50, 'symbols drawn');
  assert.ok(regionEqual(fa, fb, TITLE_H + 1, 106), 'SAS screens match');

  // Both people press "they match" and name each other.
  press(w, a, BTN.CENTER);
  press(w, b, BTN.CENTER);
  assert.equal(a.screen(), 'text');
  assert.equal(b.screen(), 'text');
  typeText(w, a, CS_LABEL, 'Bob');
  typeText(w, b, CS_LABEL, 'Alice');
  press(w, a, BTN.CENTER);
  press(w, b, BTN.CENTER);
  assert.equal(a.screen(), 'contact');
  assert.equal(b.screen(), 'contact');
  assert.equal(a.count('contact'), 1);
  assert.equal(b.count('contact'), 1);
  assert.equal(a.recordName('contact', 0), 'Bob');
  assert.equal(b.recordName('contact', 0), 'Alice');
  assert.ok(!a.irOn() && !b.irOn(), 'IR off after pairing');

  // Words: A's SAY is B's HEAR and the other way round, for several windows.
  for (let off = -2; off <= 2; off++) {
    const wa = a.contactWords(0, off), wb = b.contactWords(0, off);
    assert.equal(wa.give, wb.receive);
    assert.equal(wa.receive, wb.give);
    assert.notEqual(wa.give, wa.receive);
  }

  // And that is what the screens show: SAY at y=16, HEAR at y=46 (font_large, x=50).
  let ok = false;
  for (let attempt = 0; attempt < 3 && !ok; attempt++) {
    w.advance(150); // re-render if a 15 s window just turned over
    const wa = a.contactWords(0, 0), wb = b.contactWords(0, 0);
    const sayA = a.probeText(FONT.LARGE, 50, TITLE_H + 1, 0, a.word(wa.give));
    const hearB = b.probeText(FONT.LARGE, 50, TITLE_H + 31, 0, b.word(wb.receive));
    const scrA = a.framebuffer(), scrB = b.framebuffer();
    ok = regionEqual(scrA, sayA, TITLE_H + 1, TITLE_H + 27, 6) &&
         regionEqual(scrB, hearB, TITLE_H + 31, TITLE_H + 56, 6) &&
         a.word(wa.give) === b.word(wb.receive);
    if (ok) console.log(`  words: A says "${a.word(wa.give)}" = B hears "${b.word(wb.receive)}"; ` +
                        `B says "${b.word(wb.give)}" = A hears "${a.word(wa.receive)}"`);
  }
  assert.ok(ok, 'contact screens show matching words crosswise');
});

test('pairing survives a noisy link (10% of frames dropped)', async () => {
  const { a, b, w } = await newWorld();
  w.noisy = true;
  let n = 0;
  w.random = () => ((n = (n * 1103515245 + 12345) >>> 0) / 2 ** 32);
  await pairBoth(w, a, b);
  assert.ok(regionEqual(a.framebuffer(), b.framebuffer(), TITLE_H + 1, 106));
});

test('adding a TOTP code with the joystick shows the RFC 6238 code', async () => {
  const { a, w } = await newWorld();
  presses(w, a, BTN.DOWN, 3); // "Add a code"
  press(w, a, BTN.CENTER);
  assert.equal(a.screen(), 'text');
  typeText(w, a, CS_LABEL, 'Demo');
  press(w, a, BTN.CENTER);
  typeText(w, a, CS_BASE32, 'JBSWY3DPEHPK3PXP');
  press(w, a, BTN.CENTER);
  assert.equal(a.screen(), 'code_opts');
  press(w, a, BTN.CENTER); // 6 digits, 30 s, SHA1
  assert.equal(a.screen(), 'code');
  assert.equal(a.count('totp'), 1);
  assert.equal(a.recordName('totp', 0), 'Demo');

  w.advance(120);
  const t = a.unix();
  const ref = await totpWith(nodeHmacSha1, base32Decode('JBSWY3DPEHPK3PXP'), t);
  assert.equal(String(a.totpCode(0)).padStart(6, '0'), ref, 'stored secret gives the reference code');
  // The panel shows "123 456" in the 44 px digits font under the title.
  const probe = a.probeText(FONT.DIGITS, 125, TITLE_H + 6, 1, `${ref.slice(0, 3)} ${ref.slice(3)}`);
  assert.ok(regionEqual(a.framebuffer(), probe, TITLE_H + 1, 92), `screen shows ${ref}`);
  const wrong = String((Number(ref) + 1) % 1e6).padStart(6, '0');
  const other = a.probeText(FONT.DIGITS, 125, TITLE_H + 6, 1, `${wrong.slice(0, 3)} ${wrong.slice(3)}`);
  assert.ok(!regionEqual(a.framebuffer(), other, TITLE_H + 1, 92), 'and not another code');
  console.log(`  code at ${t}: ${ref}`);
});

test('sleeps when idle and wakes on a press', async () => {
  const { b, w } = await newWorld();
  const full0 = b.refreshes().full;
  w.advance(61000);
  assert.ok(b.sleeping, 'asleep after 60 s idle');
  assert.ok(b.refreshes().full > full0, 'sleep screen drawn with a full refresh');
  const before = b.energy();
  w.advance(10000);
  const after = b.energy();
  assert.equal(after.asleepMs - before.asleepMs, 10000);
  assert.equal(after.awakeMs, before.awakeMs);
  b.setButtons(BTN.CENTER);
  w.advance(30);
  b.setButtons(0);
  w.advance(30);
  assert.ok(!b.sleeping);
  assert.equal(b.screen(), 'menu');
  // The waking press is not taken as a key: still on the first item.
  press(w, b, BTN.CENTER);
  assert.equal(b.screen(), 'codes');
});

test('a PIN set with the joystick is required after wake', async () => {
  const { a, w } = await newWorld();
  a.addTotp('GitHub', 'JBSWY3DPEHPK3PXP');
  w.advance(50);
  presses(w, a, BTN.DOWN, 4); // Settings
  press(w, a, BTN.CENTER);
  presses(w, a, BTN.DOWN, 2); // Set a PIN
  press(w, a, BTN.CENTER);
  assert.equal(a.screen(), 'pin');
  const pin = [BTN.UP, BTN.RIGHT, BTN.DOWN, BTN.LEFT, BTN.UP];
  for (const k of pin) press(w, a, k);
  press(w, a, BTN.CENTER);
  assert.equal(a.screen(), 'pin'); // "Enter it again"
  for (const k of pin) press(w, a, k);
  press(w, a, BTN.CENTER);
  assert.equal(a.screen(), 'message');
  assert.ok(a.pinSet());
  press(w, a, BTN.LEFT);
  assert.equal(a.screen(), 'menu');

  w.advance(61000);
  assert.ok(a.sleeping);
  assert.ok(!a.unlocked(), 'store locked while asleep');
  assert.equal(a.totpCode(0), -1, 'codes unreadable while locked');

  a.setButtons(BTN.CENTER);
  w.advance(30);
  a.setButtons(0);
  w.advance(30);
  assert.equal(a.screen(), 'pin', 'PIN prompt after wake');
  for (const k of [BTN.UP, BTN.UP, BTN.UP, BTN.UP]) press(w, a, k);
  press(w, a, BTN.CENTER);
  assert.equal(a.screen(), 'pin', 'wrong PIN rejected');
  assert.ok(!a.unlocked());
  for (const k of pin) press(w, a, k);
  press(w, a, BTN.CENTER);
  assert.equal(a.screen(), 'menu');
  assert.ok(a.unlocked());
  assert.ok(a.totpCode(0) >= 0);
});

test('energy accounting and battery projection', async () => {
  const { a, w } = await newWorld();
  a.resetEnergy();
  presses(w, a, BTN.DOWN, 2);
  press(w, a, BTN.CENTER); // pairing: IR on
  w.advance(2000);
  press(w, a, BTN.LEFT);   // cancel: IR off
  w.advance(70000);        // idle -> sleep
  const e = a.energy();
  assert.ok(a.sleeping);
  assert.ok(e.irMs >= 2000 && e.irMs < 2500, `IR time ${e.irMs}`);
  assert.ok(e.fast > 0 && e.full >= 1);
  assert.equal(Math.round(e.awakeMs + e.asleepMs), w.elapsedMs - 50);
  const mAs = chargeMAs(POWER_DEFAULTS, e);
  assert.ok(mAs > 0);
  const days = lifeDays(POWER_DEFAULTS, profileAvgMA(POWER_DEFAULTS));
  assert.ok(days > 300 && days < 3000, `20 uses/day projection ${days.toFixed(0)} days`);
  console.log(`  20 uses/day: ${days.toFixed(0)} days on ${POWER_DEFAULTS.battery_mAh} mAh`);
});

test('reference TOTP matches RFC 6238 test vector', async () => {
  const key = new TextEncoder().encode('12345678901234567890');
  assert.equal(await totpWith(nodeHmacSha1, key, 59, 30, 8), '94287082');
  assert.equal(await totpWith(nodeHmacSha1, key, 1111111109, 30, 8), '07081804');
  assert.deepEqual([...base32Decode('JBSWY3DPEHPK3PXP')], [...Buffer.from('48656c6c6f21deadbeef', 'hex')]);
});
