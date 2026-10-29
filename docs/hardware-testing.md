# Hardware testing and CI

Anyone can change IRL2FA, and people order boards straight from
`hardware/fab/`. So every push and pull request runs a set of checks that
simulate and validate the hardware. A change that breaks the circuit, the
pinout, the battery budget or the fit in the case fails CI before it can reach
someone's order.

| Check | What it catches | Where |
|-|-|-|
| **ERC** | unconnected pins, power pins with no driver, conflicting outputs | `kicad-cli sch erc` |
| **DRC + schematic parity** | clearance/width violations against JLCPCB's standard process, unrouted nets, PCB out of sync with the schematic | `kicad-cli pcb drc --schematic-parity`, rules in `hardware/irl2fa.kicad_pro` |
| **Netlist vs pinout** | an MCU pin moved in the schematic but not in `hardware/pins.yaml`, and so not in the firmware; missing decoupling, pull-ups or LCSC numbers; single-pin nets | `hardware/tests/test_netlist.py` |
| **SPICE benches** | the e-paper booster no longer reaching ±20 V or exceeding part ratings; a coin-cell sag that would brown out a refresh; load-switch inrush; reverse-battery leakage; IR LED current out of range | `hardware/spice/*.cir`, limits in `hardware/spice/checks.yaml`, run by `hardware/tests/test_spice.py` |
| **Power budget** | a part change that pushes worst-case battery life below 1 year | `hardware/power/model.json` (datasheet currents) × `hardware/power/measured.json` (on-times measured in Renode), `hardware/tests/test_power.py` |
| **Measured power profile** | a firmware change that keeps the device awake longer, refreshes the panel more often, talks to the RTC more, or leaves IR on: the release firmware runs a scripted use and a pairing in Renode, and the measured timeline must match the committed one (counts exactly, times and charge within 5 %) and still give ≥ 1 year worst case | `firmware/renode/power.robot`, `firmware/renode/models/PowerMonitor.cs`, `hardware/power/power.py --measured firmware/build/renode --check` (in `firmware.yml`) |
| **Fabrication outputs are current** | KiCad files edited without regenerating the Gerbers, drill files, BOM and placement files people order from | `hardware/tools/fab.py --check`, `hardware/tests/test_fab.py` |
| **Gerber sanity** | outline missing or the wrong size, drills outside fab limits, BOM and placement file disagreeing | `hardware/tests/test_fab.py` (gerbonara) |
| **Case fit** | case clearance under 0.3 mm to any part; the joystick hole, screen window or IR windows misaligned | `case/tests/` |
| **Firmware pins** | `firmware/port/stm32u0/pins.h` not regenerated after a pinout change | `firmware.yml` |
| **Firmware on emulated hardware** | the real STM32 binary, run in Renode with modelled SSD1680, RV-3032, joystick and IR link: boot, menus, two devices pairing | `firmware/renode/` |
| **Browser simulator** | the WebAssembly build of the firmware: pairing at 2 cm through half-duplex collisions, retrying without success out of range (14 cm) and with retries at 8.5 cm; refreshes following the firmware's full-after-5-partial rule; presses during a refresh latched as on the board; a scripted use matching the Renode measurement (refresh counts, awake time, charge); the battery panel agreeing with `power.py`; RFC 6238 codes | `sim/test/sim.test.mjs` (in `sim.yml`) |

Pull requests also get before/after renders of the schematic and both PCB
sides as a build artifact, for visual review.

## Running the checks locally

Everything runs in Docker, so you don't need KiCad installed:

```sh
docker build -t irl2fa-hw hardware/ci
docker run --rm -v "$PWD":/w -w /w irl2fa-hw kicad-cli sch erc --severity-error hardware/irl2fa.kicad_sch
docker run --rm -v "$PWD":/w -w /w irl2fa-hw kicad-cli pcb drc --schematic-parity --severity-error hardware/irl2fa.kicad_pcb
docker run --rm -v "$PWD":/w -w /w irl2fa-hw python3 -m pytest -v hardware/tests
```

Or run `make check` at the repository root, which runs these plus the
firmware, simulator and case checks.

## After you change the hardware

1. Edit `hardware/irl2fa.kicad_sch` / `irl2fa.kicad_pcb` in KiCad 9.
2. If you moved an MCU pin, update `hardware/pins.yaml`, then run
   `python3 hardware/tools/gen_pins.py`, which regenerates the firmware pin
   header and `docs/pin-assignments.md`.
3. If you changed a power-relevant part, update its current in
   `hardware/power/model.json` and run `python3 hardware/power/power.py --update`,
   which regenerates `docs/power-calculations.md`, its timeline chart and the
   simulator's `sim/web/power-model.js`.
4. Regenerate the fabrication files:
   `docker run --rm -v "$PWD":/w -w /w irl2fa-hw python3 hardware/tools/fab.py`.
5. Commit everything together. CI checks all of the above.

## After you change the firmware's behaviour

If a firmware change moves the measured power profile on purpose (a new
screen that redraws more often, a different idle timeout), CI's power check
fails with the numbers that moved. Re-measure and commit the result:

```sh
make power   # cross-build, run the Renode suites, rewrite hardware/power/measured.json + generated files
```

## What the simulations do not cover

These checks are models. They catch regressions and gross mistakes; they do
not replace bringing up a real board.

- **SPICE models** are fitted to datasheet headline figures. The SSD1680's
  booster control loop is approximated (a fixed switching pattern with a
  22 V enable), and the panel's load profile is assumed. Each bench's header
  states its assumptions.
- **The IR link's optics.** The browser simulator models range and angle with
  inverse-square loss, the LED's and phototransistor's angular lobes, a
  Gaussian bit-error rate, frame dropouts, real airtime at 4800 baud and
  half-duplex collisions (`sim/web/irl2fa-device.js`, `IR_LINK`). Its constants
  are set so the link is clean to about 8 cm and dead past about 10 cm, from
  the link budget in
  [research/parts-research-2026-10-29.md](research/parts-research-2026-10-29.md)
  §2.3. Ambient light is not modelled, and the range needs confirming on a
  real board. Renode's IR link (a UART hub) is lossless and instantaneous.
- **Power.** Renode measures how long each part is on, not how much current it
  draws: the currents are datasheet figures (two of them estimates), the
  e-paper is a flat 3.5 mA for its BUSY time, and instructions count as one
  cycle each. [power-calculations.md](power-calculations.md) lists what
  remains an assumption.
- **Renode** models the peripherals at register level, as far as the firmware
  uses them. Analog behaviour, Stop 2 current and the panel's waveforms are
  not modelled. See `firmware/renode/README.md`.
