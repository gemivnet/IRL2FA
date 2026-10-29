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
| **Power budget** | a part change that pushes worst-case battery life below 1 year | `hardware/power/model.json`, `hardware/tests/test_power.py` |
| **Fabrication outputs are current** | KiCad files edited without regenerating the Gerbers, drill files, BOM and placement files people order from | `hardware/tools/fab.py --check`, `hardware/tests/test_fab.py` |
| **Gerber sanity** | outline missing or the wrong size, drills outside fab limits, BOM and placement file disagreeing | `hardware/tests/test_fab.py` (gerbonara) |
| **Case fit** | case clearance under 0.3 mm to any part; the joystick hole, screen window or IR windows misaligned | `case/tests/` |
| **Firmware pins** | `firmware/port/stm32u0/pins.h` not regenerated after a pinout change | `firmware.yml` |
| **Firmware on emulated hardware** | the real STM32 binary, run in Renode with modelled SSD1680, RV-3032, joystick and IR link: boot, menus, two devices pairing | `firmware/renode/` |

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
3. If you changed a power-relevant part, update `hardware/power/model.json`
   and regenerate `docs/power-calculations.md`
   (`python3 hardware/power/power.py --markdown > docs/power-calculations.md`).
4. Regenerate the fabrication files:
   `docker run --rm -v "$PWD":/w -w /w irl2fa-hw python3 hardware/tools/fab.py`.
5. Commit everything together. CI checks all of the above.

## What the simulations do not cover

These checks are models. They catch regressions and gross mistakes; they do
not replace bringing up a real board.

- **SPICE models** are fitted to datasheet headline figures. The SSD1680's
  booster control loop is approximated (a fixed switching pattern with a
  22 V enable), and the panel's load profile is assumed. Each bench's header
  states its assumptions.
- **The IR link's optics** (range, alignment, ambient light) are not
  simulated. The research behind the part choice is in
  [research/parts-research-2026-10-29.md](research/parts-research-2026-10-29.md),
  §2.3.
- **Renode** models the peripherals at register level, as far as the firmware
  uses them. Analog behaviour, Stop 2 current and the panel's waveforms are
  not modelled. See `firmware/renode/README.md`.
