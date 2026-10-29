// Takes the README/docs screenshots of the simulator with Playwright.
//   sim/tools/screenshots.sh            (builds, serves sim/dist, runs this in Docker)
// SPDX-License-Identifier: AGPL-3.0-or-later
import { chromium } from 'playwright';

const URL = process.env.SIM_URL || 'http://127.0.0.1:8123/';
const OUT = process.env.OUT || 'docs/images/sim';
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// Presses are held for 70 ms and then the panel is given time to finish its
// refresh (0.42 s partial, 3 s full): a press during a refresh that the
// previous press caused is merged with it, as on the device.
async function tap(page, dev, btn) {
  await page.evaluate(([d, b]) => window.irl2fa.press(d, b), [dev, btn]);
  await sleep(70);
  await page.evaluate(([d, b]) => window.irl2fa.release(d, b), [dev, btn]);
  await page.waitForFunction((d) => window.irl2fa.devices[d].busyMs() === 0, dev, { timeout: 10000 });
  await sleep(200);
}

async function screenName(page, dev) {
  return page.evaluate((d) => document.querySelectorAll('[data-role="screen"]')[d].textContent, dev);
}

async function waitScreen(page, dev, re, ms = 60000) {
  const end = Date.now() + ms;
  while (Date.now() < end) {
    if (re.test(await screenName(page, dev))) return;
    await sleep(100);
  }
  throw new Error(`device ${dev} never reached ${re} (at "${await screenName(page, dev)}")`);
}

async function setInput(page, sel, value) {
  await page.evaluate(([s, v]) => {
    const el = document.querySelector(s);
    if (el.type === 'checkbox') el.checked = v; else el.value = String(v);
    el.dispatchEvent(new Event(el.type === 'checkbox' ? 'change' : 'input', { bubbles: true }));
  }, [sel, value]);
}

async function open(browser, opts) {
  const page = await browser.newPage(opts);
  const errors = [];
  page.on('pageerror', (e) => errors.push(e));
  page.on('console', (m) => { if (m.type() === 'error') errors.push(new Error(m.text())); });
  await page.goto(URL);
  await page.waitForFunction(() => window.irl2fa && window.irl2fa.devices && window.irl2fa.devices.length === 2);
  page.checkErrors = () => { if (errors.length) throw errors[0]; };
  return page;
}

const browser = await chromium.launch();
try {
  // Desktop, after the boot refresh has settled.
  const page = await open(browser, { viewport: { width: 1400, height: 1000 }, deviceScaleFactor: 1 });
  await sleep(4500);
  await page.screenshot({ path: `${OUT}/sim-desktop.png` });

  // Out of range: both pairing, 14 cm apart. Frames fail and pairing retries.
  for (const d of [0, 1]) { await tap(page, d, 'DOWN'); await tap(page, d, 'DOWN'); await tap(page, d, 'CENTER'); }
  await setInput(page, '#dist', 14);
  await setInput(page, '#together', true);
  await sleep(6000);
  if (!/pairing/i.test(await screenName(page, 0))) throw new Error('paired out of range?');
  await (await page.$('#stage')).screenshot({ path: `${OUT}/sim-range.png` });

  // Close together: the exchange completes.
  await setInput(page, '#dist', 2);
  await waitScreen(page, 0, /compare|sas/i);
  await waitScreen(page, 1, /compare|sas/i);
  await sleep(3500);
  await (await page.$('#stage')).screenshot({ path: `${OUT}/sim-pairing.png` });

  // Confirm and name the contacts ("A" + done is enough), then show the words
  // after a few redraws (partial refreshes leave some ghosting).
  for (const d of [0, 1]) { await tap(page, d, 'CENTER'); }
  for (const d of [0, 1]) { await tap(page, d, 'RIGHT'); await tap(page, d, 'CENTER'); }
  await waitScreen(page, 0, /contact|words/i);
  await waitScreen(page, 1, /contact|words/i);
  await sleep(11000);
  await (await page.$('#stage')).screenshot({ path: `${OUT}/sim-words.png` });

  // Battery panel after a year of normal use, fast-forwarded.
  await page.click('[data-ff="365"]');
  await sleep(1500);
  // Clear of the sticky header, and with its details open.
  const box = await page.evaluate(() => {
    const p = document.querySelector('section[aria-labelledby="bat-h"]');
    p.querySelector('details').open = true;
    window.scrollTo(0, p.getBoundingClientRect().top + window.scrollY - 80);
    const r = p.getBoundingClientRect();
    return { x: r.left - 8, y: r.top - 8, width: r.width + 16, height: r.height + 16 };
  });
  await sleep(300);
  await page.screenshot({ path: `${OUT}/sim-battery.png`, clip: box });
  page.checkErrors();

  // Mobile
  const m = await open(browser, { viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, isMobile: true, hasTouch: true });
  await sleep(4500);
  await m.screenshot({ path: `${OUT}/sim-mobile.png` });
  const overflow = await m.evaluate(() => document.documentElement.scrollWidth - window.innerWidth);
  if (overflow > 0) throw new Error(`mobile page scrolls sideways by ${overflow} px`);
  m.checkErrors();
  console.log('screenshots written to', OUT);
} finally {
  await browser.close();
}
