# IRL2FA browser simulator

Two IRL2FA devices in a web page, running the **real firmware**: `firmware/core/`
and the virtual board `firmware/vhal/` compiled to WebAssembly, one instance per
device. Pair them over a simulated IR link, compare the symbols, read each
other's words, add TOTP codes, set a PIN, let them sleep, and watch the battery
estimate.

![Simulator](../docs/images/sim/sim-desktop.png)

## Layout

| Path | What it is |
| --- | --- |
| `sim_main.c` | Exported C API (`sim_*`): lifecycle, buttons (with the board's EXTI press latch), time, display (with the panel's BUSY time), IR, energy counters, and sim-only helpers (demo codes, word/code readback, text probe for the tests). |
| `build.sh` | Builds `dist/`: `irl2fa.mjs` + `irl2fa.wasm` (`-sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,node`) and copies `web/`. |
| `web/irl2fa-device.js` | `Device` (one WASM instance) and `World` (two devices in lock-step simulated time, the IR link model between them), the energy model and a reference TOTP. Shared by the page and the tests. |
| `web/index.html`, `app.js`, `style.css` | The page. No framework, no build step, no network access beyond its own files. |
| `web/power-model.js` | The battery panel's model, **generated** by `python3 hardware/power/power.py --update` from the datasheet currents and the Renode-measured profile. The tests fail if it is stale. |
| `test/sim.test.mjs` | Node tests driving both devices through the joystick only. |

`dist/` is generated and git-ignored.

## Build

Needs Docker (uses the `emscripten/emsdk:3.1.74` image) or `emcc` 3.1.74 on `PATH`.

```sh
sim/build.sh
```

## Test

Needs Node 22 or later (CI uses 24). Build first.

```sh
node --test sim/test
```

The tests pair two devices over IR (at 2 cm through collisions, with 10% of
frames dropped, and at 8.5 cm with bit errors), check that pairing keeps
retrying but never completes at 14 cm, check the refresh pattern against the
firmware's full-after-5-partial rule and a scripted use against the Renode
measurement (refresh counts, awake time, charge), and
check the SAS screens are identical, name the contacts with the joystick and
check one device's SAY word is the other's HEAR word, both in the store and as
drawn on the panels. They add `JBSWY3DPEHPK3PXP` through the character wheel
and compare the code on screen with an RFC 6238 reference from `node:crypto`,
and cover sleep/wake, a PIN being required after wake, and the energy counters.

## Run

```sh
python3 -m http.server 8000 --directory sim/dist
# open http://localhost:8000/
```

It has to be served over http(s). Opening `index.html` as a file will not load
the WebAssembly.

Controls: click or tap the joystick (hold a direction to auto-repeat). On
narrow screens there is a larger touch pad under each device. With the keyboard,
focus a device (click it or Tab to it) and use the arrow keys and Enter.

## Hardware behaviour

- **E-paper.** `hal_display_show()` blocks on the board until the panel drops
  BUSY: 3 s for a full refresh, 0.42 s for a partial one
  (`hardware/power/model.json`). The simulator holds the device busy for the
  same time: the core does not poll, and a press is latched (as the EXTI edge
  latch on the board does) and handled afterwards. Two presses of one
  direction during one refresh are one press, as on the board. The page draws
  a full refresh as the panel's flashing waveform (old image inverted, black,
  white, black, white, new image inverted, new image) and a partial one as a
  short flicker of the changed pixels that leaves a faint ghost; ghosting
  accumulates over partial refreshes and a full refresh clears it. The
  firmware chooses which: full on wake, on sleep and after every 5 partial
  refreshes (`FULL_REFRESH_EVERY` in `firmware/core/app.c`).
- **IR link** (`IR_LINK` in `web/irl2fa-device.js`). Received signal falls
  with the square of the distance and with the LED's (±25°) and
  phototransistor's (±35°) angular lobes; the bit error rate is the Gaussian
  tail of the resulting signal-to-noise ratio, and weak signals also lose
  whole frames (dropouts). Frames take their airtime (10 bits a byte at 4800
  baud) and the link is half duplex, so frames that overlap a device's own
  transmission are lost. Clean to about 8 cm on axis, marginal at 9 cm,
  nothing beyond about 10 cm: assumptions until a board is measured.
- **Battery** (`web/power-model.js`, generated). The C side counts time
  awake, asleep, with IR on and with the panel busy, and full/partial
  refreshes. The page charges them at the model's per-state figures:

  ```
  charge = awake_s × awake_mA + asleep_s × sleep_uA / 1000 + ir_s × ir_mA
         + full × full_refresh_mAs + partial × partial_refresh_mAs
  ```

  `awake_mA` is the MCU's average over the measured use (running plus WFI,
  RTC I2C), `ir_mA` the LED at its measured duty plus the receiver. The panel
  shows each cell's charge, voltage and remaining life, and fast-forward
  skips days at a usage profile (5, 20 or 60 uses a day), typical or
  worst-case currents, exactly as `hardware/power/power.py` counts them.

## Deployment

`.github/workflows/sim.yml` builds, runs the firmware host tests and these
tests on every push and pull request, and deploys `sim/dist` to GitHub Pages
from `main`. If `sim/web/CNAME` exists it is published as the site's `CNAME`.
