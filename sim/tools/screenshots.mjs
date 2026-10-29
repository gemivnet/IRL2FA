// Takes the README/docs screenshots of the simulator with Playwright.
//   sim/tools/screenshots.sh            (builds, serves sim/dist, runs this in Docker)
// SPDX-License-Identifier: AGPL-3.0-or-later
import { chromium } from 'playwright';

const URL = process.env.SIM_URL || 'http://127.0.0.1:8123/';
const OUT = process.env.OUT || 'docs/images/sim';
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function tap(page, dev, btn) {
  await page.evaluate(([d, b]) => window.irl2fa.press(d, b), [dev, btn]);
  await sleep(70);
  await page.evaluate(([d, b]) => window.irl2fa.release(d, b), [dev, btn]);
  await sleep(140);
}

async function screenName(page, dev) {
  return page.evaluate((d) => document.querySelectorAll('[data-role="screen"]')[d].textContent, dev);
}

async function waitScreen(page, dev, re, ms = 15000) {
  const end = Date.now() + ms;
  while (Date.now() < end) {
    if (re.test(await screenName(page, dev))) return;
    await sleep(100);
  }
  throw new Error(`device ${dev} never reached ${re} (at "${await screenName(page, dev)}")`);
}

const browser = await chromium.launch();
try {
  // Desktop
  const page = await browser.newPage({ viewport: { width: 1400, height: 900 }, deviceScaleFactor: 1 });
  await page.goto(URL);
  await page.waitForFunction(() => window.irl2fa && window.irl2fa.devices && window.irl2fa.devices.length === 2);
  await sleep(1500);
  await page.screenshot({ path: `${OUT}/sim-desktop.png` });

  // Pair the two devices: Home -> "Pair with someone" on both, then hold them together.
  for (const d of [0, 1]) { await tap(page, d, 'DOWN'); await tap(page, d, 'DOWN'); await tap(page, d, 'CENTER'); }
  await page.evaluate(() => { const c = document.querySelector('#together'); c.checked = true; c.dispatchEvent(new Event('change', { bubbles: true })); });
  await waitScreen(page, 0, /compare|sas/i);
  await waitScreen(page, 1, /compare|sas/i);
  await sleep(1200);
  await (await page.$('#stage')).screenshot({ path: `${OUT}/sim-pairing.png` });

  // Confirm and name the contacts ("A" + done is enough), then show the words.
  for (const d of [0, 1]) { await tap(page, d, 'CENTER'); }
  for (const d of [0, 1]) { await tap(page, d, 'RIGHT'); await tap(page, d, 'CENTER'); }
  await waitScreen(page, 0, /contact|words/i);
  await waitScreen(page, 1, /contact|words/i);
  await sleep(1500);
  await (await page.$('#stage')).screenshot({ path: `${OUT}/sim-words.png` });

  // Mobile
  const m = await browser.newPage({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, isMobile: true, hasTouch: true });
  await m.goto(URL);
  await m.waitForFunction(() => window.irl2fa && window.irl2fa.devices && window.irl2fa.devices.length === 2);
  await sleep(1500);
  await m.screenshot({ path: `${OUT}/sim-mobile.png` });
  console.log('screenshots written to', OUT);
} finally {
  await browser.close();
}
