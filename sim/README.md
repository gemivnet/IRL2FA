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
| `sim_main.c` | Exported C API (`sim_*`): lifecycle, buttons, time, display, IR, energy counters, and sim-only helpers (demo codes, word/code readback, text probe for the tests). |
| `build.sh` | Builds `dist/`: `irl2fa.mjs` + `irl2fa.wasm` (`-sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,node`) and copies `web/`. |
| `web/irl2fa-device.js` | `Device` (one WASM instance) and `World` (two devices in lock-step simulated time, IR between them), the energy model and a reference TOTP. Shared by the page and the tests. |
| `web/index.html`, `app.js`, `style.css` | The page. No framework, no build step, no network access beyond its own files. |
| `web/power-defaults.json` | Default energy model. If `hardware/power/model.json` exists, `build.sh` copies it to `dist/power-model.json` and any keys it has with the same names override the defaults. |
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

The tests pair two devices over IR (clean and with 10% of frames dropped),
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

## Energy model

The C side counts time awake, time asleep, time with the IR transceiver on and
full/fast refreshes as simulated time passes (`sim_advance`). The page turns
those counts into charge with the model:

```
charge = awake_s * awake_mA + asleep_s * sleep_uA / 1000 + ir_s * ir_mA
       + full * full_refresh.mA * full_refresh.s + fast * fast_refresh.mA * fast_refresh.s
```

and shows µAh used, the average current, battery life if you kept using the
device the way you just did, and a projection for the profile in the model
(20 uses a day of 70 s awake, which includes the 60 s idle time-out, with 2 full
and 6 fast refreshes each).

## Deployment

`.github/workflows/sim.yml` builds, runs the firmware host tests and these
tests on every push and pull request, and deploys `sim/dist` to GitHub Pages
from `main`. If `sim/web/CNAME` exists it is published as the site's `CNAME`.
