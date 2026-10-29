// End-to-end tests of the WebAssembly build: two simulated devices driven
// only through the joystick, exactly as a person would.
// Run after sim/build.sh:   node --test sim/test
// SPDX-License-Identifier: AGPL-3.0-or-later
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHmac, randomBytes } from 'node:crypto';
import { existsSync, readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const dist = new URL('../dist/', import.meta.url);
if (!existsSync(fileURLToPath(new URL('irl2fa.mjs', dist)))) {
  throw new Error('sim/dist is missing: run sim/build.sh first');
}
const { default: createIRL2FA } = await import(new URL('irl2fa.mjs', dist));
const {
  BTN, Device, World, FONT, DISPLAY_STRIDE, DISPLAY_H, POWER, irLinkQuality,
  chargeMAs, profileLifeYears, cellMv, base32Decode, totpWith,
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

async function newWorld(opts = {}) {
  const a = await newDevice('A');
  const b = await newDevice('B');
  const w = new World([a, b], { random: lcg(7), ...opts });
  w.advance(50);
  settle(w, a);
  settle(w, b);
  return { a, b, w };
}

/** Lets the panel finish the refresh in progress (it takes its real time:
 * 3 s full, 0.42 s partial) and the firmware see the release. Pressing
 * again before that would merge the presses, as on the board. */
function settle(w, d) {
  w.until(() => d.busyMs() === 0, 10000, 10);
  w.advance(40);
}

function press(w, d, btn) {
  d.setButtons(btn);
  w.advance(30);
  d.setButtons(0);
  w.advance(30);
  settle(w, d);
}

/** A deterministic random source for the IR channel. */
function lcg(seed = 1) {
  let n = seed >>> 0;
  return () => ((n = (n * 1103515245 + 12345) >>> 0) / 2 ** 32);
}

/** Records every refresh each device sends to its panel: 'F' or 'p'. */
function recordRefreshes(w) {
  const log = new Map(w.devices.map((d) => [d, []]));
  w.onRefresh = (d, full) => log.get(d).push(full ? 'F' : 'p');
  return log;
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

function startPairing(w, a, b) {
  for (const d of [a, b]) {
    assert.equal(d.screen(), 'menu');
    presses(w, d, BTN.DOWN, 2); // "Pair with someone"
    press(w, d, BTN.CENTER);
    assert.equal(d.screen(), 'pair');
    assert.ok(d.irOn(), 'IR powered while pairing');
  }
}

async function pairBoth(w, a, b) {
  startPairing(w, a, b);
  // Apart, nothing passes.
  w.advance(1500);
  assert.equal(a.screen(), 'pair');
  assert.equal(b.screen(), 'pair');
  // Held together: the commit/reveal exchange completes.
  w.linked = true;
  assert.ok(w.until(() => a.screen() === 'sas' && b.screen() === 'sas', 60000),
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
  await pairBoth(w, a, b);
  assert.ok(regionEqual(a.framebuffer(), b.framebuffer(), TITLE_H + 1, 106));
});

test('IR link model: clean up close, errors rising with distance and angle', () => {
  const at = (distanceCm, angleDeg = 0) => irLinkQuality({ distanceCm, angleDeg });
  assert.ok(at(2).ber < 1e-12 && at(2).frameOk(62) > 0.999, 'held together: clean');
  assert.ok(at(6).frameOk(62) > 0.99, '6 cm on axis: clean');
  for (const d of [4, 6, 8, 9, 10, 12]) assert.ok(at(d + 1).ber >= at(d).ber, `BER rises past ${d} cm`);
  assert.ok(at(9).frameOk(62) < 0.5, '9 cm: most long frames damaged');
  assert.ok(at(12).frameOk(38) < 0.001, '12 cm: nothing gets through');
  assert.ok(at(4, 40).ber > at(4, 0).ber, 'off axis is worse');
  assert.ok(at(4, 60).frameOk(62) < at(4, 30).frameOk(62));
  assert.ok(at(3, 90).frameOk(38) < 1e-9, 'side by side: no link');
});

test('pairing at short range succeeds through half-duplex collisions', async () => {
  const { a, b, w } = await newWorld();
  startPairing(w, a, b);
  w.link = { distanceCm: 2, angleDeg: 10 };
  w.linked = true;
  const t0 = w.elapsedMs;
  assert.ok(w.until(() => a.screen() === 'sas' && b.screen() === 'sas', 60000),
    `both on the SAS screen (a=${a.screen()} b=${b.screen()})`);
  const s = w.stats;
  console.log(`  2 cm: paired in ${((w.elapsedMs - t0) / 1000).toFixed(1)} s; ${s.delivered} frames ok, ` +
    `${s.collisions} collided, ${s.corrupted} corrupted, ${s.dropouts} dropped`);
  assert.equal(s.corrupted + s.dropouts, 0, 'no bit errors at 2 cm');
  settle(w, a);
  settle(w, b);
  assert.ok(regionEqual(a.framebuffer(), b.framebuffer(), TITLE_H + 1, 106), 'SAS screens match');
});

test('out of range, pairing keeps retrying and does not complete', async () => {
  const { a, b, w } = await newWorld();
  startPairing(w, a, b);
  w.link = { distanceCm: 14, angleDeg: 0 };
  w.linked = true;
  w.advance(20000);
  const s = w.stats;
  console.log(`  14 cm: after 20 s ${s.frames} frames sent, ${s.delivered} intact, ` +
    `${s.corrupted} corrupted, ${s.dropouts} dropped, ${s.collisions} collided`);
  assert.equal(a.screen(), 'pair');
  assert.equal(b.screen(), 'pair');
  assert.ok(a.irOn() && b.irOn(), 'still listening');
  assert.ok(s.frames > 100, 'both keep retransmitting');
  const heard = s.frames - s.collisions - s.unheard;
  assert.ok(s.delivered === 0 && s.corrupted + s.dropouts === heard, 'every frame that reached the other side was lost or damaged');
  // Bring them together: the same session completes.
  w.link = { distanceCm: 3, angleDeg: 0 };
  assert.ok(w.until(() => a.screen() === 'sas' && b.screen() === 'sas', 60000), 'pairs once close');
});

test('at the edge of range (8 cm), pairing needs retries but completes', async () => {
  const { a, b, w } = await newWorld();
  startPairing(w, a, b);
  w.link = { distanceCm: 8, angleDeg: 0 };
  w.linked = true;
  const ok = w.until(() => a.screen() === 'sas' && b.screen() === 'sas', 120000);
  const s = w.stats;
  console.log(`  8 cm: ${ok ? 'paired' : 'not paired'} after ${(w.elapsedMs / 1000).toFixed(0)} s; ` +
    `${s.corrupted} corrupted, ${s.dropouts} dropped, ${s.collisions} collided of ${s.frames}`);
  assert.ok(s.corrupted + s.dropouts + s.collisions > 0, 'some frames damaged, dropped or collided');
  assert.ok(ok, 'but it gets there');
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
  w.advance(24000);
  assert.ok(!b.sleeping, 'awake before the 30 s idle timeout');
  w.advance(8000);
  assert.ok(b.sleeping, 'asleep after 30 s idle');
  assert.ok(b.refreshes().full > full0, 'sleep screen drawn with a full refresh');
  w.advance(4000); // the sleep screen's full refresh keeps it awake 3 s
  const before = b.energy();
  w.advance(10000);
  const after = b.energy();
  assert.equal(after.asleepMs - before.asleepMs, 10000);
  assert.equal(after.awakeMs, before.awakeMs);
  b.setButtons(BTN.CENTER);
  w.advance(30);
  b.setButtons(0);
  w.advance(30);
  settle(w, b);
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
  settle(w, a);
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

test('energy accounting', async () => {
  const { a, w } = await newWorld();
  const start = w.elapsedMs;
  a.resetEnergy();
  presses(w, a, BTN.DOWN, 2);
  press(w, a, BTN.CENTER); // pairing: IR on
  w.advance(2000);
  press(w, a, BTN.LEFT);   // cancel: IR off
  w.advance(40000);        // idle -> sleep
  const e = a.energy();
  assert.ok(a.sleeping);
  assert.ok(e.irMs >= 2000 && e.irMs < 3000, `IR time ${e.irMs}`);
  assert.ok(e.fast > 0 && e.full >= 1);
  assert.ok(e.busyMs >= e.full * 3000 + e.fast * 420 - 1, `panel busy ${e.busyMs} ms`);
  assert.equal(Math.round(e.awakeMs + e.asleepMs), w.elapsedMs - start);
  assert.ok(chargeMAs(e, 'worst') > chargeMAs(e, 'typ'));
});

test('a refresh blocks the core; presses during it are latched, as on the board', async () => {
  const { a, w } = await newWorld();
  press(w, a, BTN.CENTER);
  assert.equal(a.screen(), 'codes');
  // The codes list redraws every 5 s. A press during that refresh is not
  // lost: the EXTI latch holds it and the core handles it afterwards.
  assert.ok(w.until(() => a.busyMs() > 0, 6000, 10), 'periodic redraw');
  const r0 = a.refreshes();
  a.setButtons(BTN.LEFT);
  w.advance(30);
  a.setButtons(0);
  w.advance(30);
  assert.deepEqual(a.refreshes(), r0, 'nothing drawn while busy');
  assert.equal(a.screen(), 'codes', 'not handled yet');
  settle(w, a);
  assert.equal(a.screen(), 'menu', 'handled once the panel is idle');

  // But a second press during the refresh that the first one caused is
  // merged with it: the core never saw the first released. The board does
  // the same (hal_buttons() in firmware/port/stm32u0/hal_stm32u0.c).
  a.setButtons(BTN.DOWN);
  w.advance(30);
  a.setButtons(0);
  w.advance(10);
  assert.ok(a.busyMs() > 300, `partial refresh in progress (${a.busyMs()} ms left)`);
  a.setButtons(BTN.DOWN);
  w.advance(30);
  a.setButtons(0);
  settle(w, a);
  press(w, a, BTN.CENTER);
  assert.equal(a.screen(), 'contacts', 'one row down, not two');
});

test('refreshes follow the firmware: full on wake and sleep and after every 5 partial ones', async () => {
  const { a, b, w } = await newWorld();
  await pairBoth(w, a, b);
  press(w, a, BTN.CENTER);
  typeText(w, a, CS_LABEL, 'B');
  press(w, a, BTN.CENTER);
  assert.equal(a.screen(), 'contact');
  const log = recordRefreshes(w);
  w.advance(45000); // the words redraw every 5 s; then the idle timeout
  assert.ok(a.sleeping);
  a.setButtons(BTN.CENTER);
  w.advance(30);
  a.setButtons(0);
  presses(w, a, BTN.DOWN, 7);
  const seq = log.get(a).join('');
  console.log(`  refreshes: ${seq}`);
  assert.ok(!/p{6}/.test(seq), 'never more than 5 partial refreshes in a row');
  // ... F (sleep screen) F (wake) then 5 partial and a full, FULL_REFRESH_EVERY = 5.
  assert.ok(seq.includes('FFpppppF'), 'sleep and wake are full; then a full after 5 partial');
});

test('a scripted use in the simulator matches the emulator measurement', async () => {
  const { a, b, w } = await newWorld();
  await pairBoth(w, a, b);
  press(w, a, BTN.CENTER);
  typeText(w, a, CS_LABEL, 'A');
  press(w, a, BTN.CENTER);
  for (const label of ['A', 'B', 'C']) assert.ok(a.addTotp(label, 'AAAAAAAAAAAAAAAA') >= 0);
  settle(w, a);
  press(w, a, BTN.LEFT);
  press(w, a, BTN.LEFT);
  w.until(() => a.sleeping, 60000);
  w.advance(4000);
  // The same script as firmware/renode/power.robot "a typical use".
  const tap = (btn, expect) => {
    press(w, a, btn);
    w.advance(600);
    if (expect) assert.equal(a.screen(), expect);
  };
  a.resetEnergy();
  const log = recordRefreshes(w);
  tap(BTN.CENTER, 'menu');
  w.advance(1000);
  tap(BTN.DOWN);
  tap(BTN.CENTER, 'contacts');
  tap(BTN.CENTER, 'contact');
  w.advance(10000);
  tap(BTN.LEFT, 'contacts');
  tap(BTN.LEFT, 'menu');
  tap(BTN.CENTER, 'codes');
  w.advance(1000);
  tap(BTN.DOWN);
  tap(BTN.DOWN);
  tap(BTN.CENTER, 'code');
  w.advance(6000);
  tap(BTN.UP);
  w.advance(2000);
  tap(BTN.UP);
  w.advance(2000);
  assert.ok(w.until(() => a.sleeping, 60000));
  w.advance(4000);
  const e = a.energy();
  const m = POWER.measured.use;
  const seq = log.get(a);
  const full = seq.filter((x) => x === 'F').length, partial = seq.length - full;
  const mAs = chargeMAs(e, 'typ');
  console.log(`  sim: ${(e.awakeMs / 1000).toFixed(1)} s awake, ${full} full + ${partial} partial, ${mAs.toFixed(1)} mA·s; ` +
    `emulator: ${m.awake_s.toFixed(1)} s, ${m.full_refreshes} + ${m.partial_refreshes}, ${POWER.cases.typ.use_mAs} mA·s`);
  assert.ok(Math.abs(full - m.full_refreshes) <= 1, 'full refreshes');
  assert.ok(Math.abs(partial - m.partial_refreshes) <= 1, 'partial refreshes');
  assert.ok(Math.abs(e.awakeMs / 1000 - m.awake_s) <= 0.1 * m.awake_s, 'awake time');
  assert.ok(Math.abs(mAs - POWER.cases.typ.use_mAs) <= 0.1 * POWER.cases.typ.use_mAs, 'charge per use');
});

test('battery projection agrees with hardware/power/power.py, and the sim model is current', (t) => {
  const root = new URL('../../', import.meta.url);
  const script = fileURLToPath(new URL('hardware/power/power.py', root));
  let out, js;
  try {
    out = execFileSync('python3', [script], { encoding: 'utf8' });
    js = execFileSync('python3', [script, '--sim-js'], { encoding: 'utf8' });
  } catch {
    return t.skip('python3 not available');
  }
  const committed = readFileSync(fileURLToPath(new URL('sim/web/power-model.js', root)), 'utf8');
  assert.equal(committed, js, 'run: python3 hardware/power/power.py --update');
  for (const name of Object.keys(POWER.profiles)) {
    const m = new RegExp(`^${name}\\s+typical\\s+([0-9.]+) y\\s+worst\\s+([0-9.]+) y`, 'm').exec(out);
    assert.ok(m, `power.py prints ${name}`);
    assert.equal(profileLifeYears(name, 'typ').toFixed(1), m[1], `${name} typical`);
    assert.equal(profileLifeYears(name, 'worst').toFixed(1), m[2], `${name} worst`);
  }
  assert.ok(profileLifeYears('normal', 'worst') >= POWER.requirement_years);
  console.log(`  20 uses/day: ${profileLifeYears('normal', 'typ').toFixed(1)} years typical, ` +
    `${profileLifeYears('normal', 'worst').toFixed(1)} worst case`);
});

test('cell voltage falls with charge used', () => {
  assert.equal(cellMv(1), 3050);
  assert.equal(cellMv(0), 2000);
  for (let s = 0; s < 1; s += 0.01) assert.ok(cellMv(s + 0.01) >= cellMv(s));
});

test('reference TOTP matches RFC 6238 test vector', async () => {
  const key = new TextEncoder().encode('12345678901234567890');
  assert.equal(await totpWith(nodeHmacSha1, key, 59, 30, 8), '94287082');
  assert.equal(await totpWith(nodeHmacSha1, key, 1111111109, 30, 8), '07081804');
  assert.deepEqual([...base32Decode('JBSWY3DPEHPK3PXP')], [...Buffer.from('48656c6c6f21deadbeef', 'hex')]);
});
