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
| **Silkscreen** | labels or markings overlapping each other, printed on pads or clipped by the board edge (DRC errors, not warnings) | `silk_overlap`, `silk_over_copper`, `silk_edge_clearance` in `hardware/irl2fa.kicad_pro`; labels placed by `gen_pcb.py --step silk` |
| **Part orientation** | a direction-sensitive part turned the wrong way while its symmetric pads still pass DRC: the side-looking IR LED and phototransistor must face out through the top edge | `hardware/tests/test_orientation.py` |
| **Footprints vs datasheets** | a pad, pitch, exposed pad, hole or pin 1 that disagrees with the manufacturer's land pattern; symbol pins and pads that do not match; a new footprint with no datasheet entry (the test fails until one is added) | `hardware/tests/footprints.yaml` (dimensions, datasheet URL and page per part), `hardware/tests/test_footprints.py` |
| **Netlist vs pinout** | an MCU pin moved in the schematic but not in `hardware/pins.yaml`, and so not in the firmware; missing decoupling, pull-ups or LCSC numbers; single-pin nets | `hardware/tests/test_netlist.py` |
| **Electrical vs datasheets** | a symbol whose pin names disagree with the part's datasheet; an MCU pin or alternate function that does not exist on that pin; two wake pins on one EXTI line; an e-paper FPC pin wired against Good Display's reference circuit, or J1's pins not level with the panel's after the fold; a MOSFET, diode, LED or phototransistor wired the wrong way round; a capacitor rated below the voltage across it; booster values off the reference; RTC pins left in a state its manual forbids. Open findings of [the electrical review](electrical-review.md) are `xfail` tests named after their ID | `hardware/tests/electrical_ref.yaml` (datasheet facts with page references), `hardware/tests/test_electrical.py` |
| **SPICE benches** | the e-paper booster no longer reaching ±20 V or exceeding part ratings; a coin-cell sag that would brown out a refresh; load-switch inrush; reverse-battery leakage; IR LED current out of range; the IR receiver missing VIH/VIL at the UART sample point | `hardware/spice/*.cir`, limits in `hardware/spice/checks.yaml`, run by `hardware/tests/test_spice.py` |
| **Power budget** | a part change that pushes worst-case battery life below 1 year | `hardware/power/model.json` (datasheet currents) × `hardware/power/measured.json` (on-times measured in Renode), `hardware/tests/test_power.py` |
| **Measured power profile** | a firmware change that keeps the device awake longer, refreshes the panel more often, talks to the RTC more, or leaves IR on: the release firmware runs a scripted use and a pairing in Renode, and the measured timeline must match the committed one (counts exactly, times and charge within 5 %) and still give ≥ 1 year worst case | `firmware/renode/power.robot`, `firmware/renode/models/PowerMonitor.cs`, `hardware/power/power.py --measured firmware/build/renode --check` (in `firmware.yml`) |
| **Assembly at JLCPCB** | a placement file that is right for our footprints but not for JLCPCB's (a SOT-23, diode, RTC, clip or IR part turned or mirrored by JLCPCB's own footprint and its back-side angle convention, or placed off-centre); a CPL frame that does not match the Gerbers; a new part with no correction entry; paste on the cell contact; QFN exposed-pad paste out of range; fine-pitch mask dams below JLCPCB's 0.1 mm; untented vias; an MPN that is not the LCSC part; JLCPCB stock short of 10 boards; README order steps missing an option | `hardware/tests/test_assembly.py`, JLCPCB footprints in `hardware/tests/jlc_footprints.yaml`, corrections in `hardware/tools/fab.py` (`JLC`) |
| **Fabrication outputs are current** | KiCad files edited without regenerating the Gerbers, drill files, BOM and placement files people order from | `hardware/tools/fab.py --check`, `hardware/tests/test_fab.py` |
| **Gerber sanity** | outline missing or the wrong size, drills outside fab limits, BOM and placement file disagreeing | `hardware/tests/test_fab.py` (gerbonara) |
| **Case fit** | case clearance under 0.3 mm to any part; the joystick hole, screen window or IR windows misaligned | `case/tests/` |
| **Firmware pins** | `firmware/port/stm32u0/pins.h` not regenerated after a pinout change | `firmware.yml` |
| **Firmware on emulated hardware** | the real STM32 binary, run in Renode with modelled SSD1680, RV-3032, joystick and IR link: boot, menus, two devices pairing; and what the silicon requires but a plain emulator would not notice: peripherals left on in Stop 2, a torn flash double-word (ECC NMI) at boot, panel power-up timing, SPI rate, RTC EEPROM refresh at power-on, the RNG configuration ([firmware-hardware-review.md](firmware-hardware-review.md)) | `firmware/renode/` |
| **Hardware constants** | a hand-written register offset, bit, base address or IRQ number (Renode platform and models, vector table, linker memory map) that disagrees with ST's CMSIS header for the STM32U083 | `firmware/tools/check_hw_constants.py` (in `firmware.yml`) |
| **Browser simulator** | the WebAssembly build of the firmware: pairing at 2 cm in under 6 s (listen before talk), at 8 cm with bit errors, retrying at 14 cm and giving up after a minute; the refresh policy; a second press during a refresh not lost; the Renode power script replayed from the same RTC time with the same refresh sequence; the battery panel agreeing with `power.py`; RFC 6238 codes | `sim/test/sim.test.mjs` (in `sim.yml`) |
| **Pairing protocol, formally** | a change to the commit/reveal or key derivation that lets a man in the middle learn the key, or choose his key after seeing an honest one | `formal/pairing.spthy` (Tamarin), [formal-verification.md](formal-verification.md), `formal.yml` |
| **Power cuts in every store write** | a power cut after any flash operation of adding, renaming or deleting a record, saving settings, setting, changing or removing the PIN, wiping, a PIN attempt or compacting the PIN log, that leaves anything other than exactly the old or the new state | `firmware/tests/test_powercut.c` (each cut done three ways: operation dropped, half done, or leaving noise) |
| **Fuzzing** | crashes, memory errors and undefined behaviour in the IR frame parser and pairing state machine, the store on a corrupted flash image, base32 decoding and the whole app driven by random buttons, IR bytes, reboots and power cuts | `firmware/fuzz/` (libFuzzer + ASan + UBSan, 60 s per harness from committed seeds) |
| **Constant time** | a branch or memory index that depends on a secret in PIN verification, MAC/tag and commitment compares, X25519, HKDF and the SAS derivation | `firmware/verify/ct_check.c` (valgrind memcheck, secrets marked undefined) |
| **Coverage** | host tests that stop exercising `firmware/core` (minimum 92 % of lines, 78 % of branches); HTML report as a build artifact | `make -C firmware coverage` (llvm-cov) |
| **Static analysis** | cppcheck and clang-tidy findings; zero allowed, every suppression justified in place or in `firmware/.clang-tidy` | `make -C firmware lint` |

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
firmware, verification, formal model, simulator and case checks.

The firmware verification pack runs in its own image
(`firmware/verify/Dockerfile`: clang 18, libFuzzer, llvm-cov, valgrind,
cppcheck, clang-tidy):

```sh
make verify                       # all of it, in Docker
make formal                       # the Tamarin model
docker run --rm -v "$PWD":/w -w /w/firmware irl2fa-verify make fuzz-store FUZZ_TIME=600
```

A fuzzer crash leaves its input in `firmware/build/verify/crash-<target>-<sha1>`;
reproduce it with `./firmware/build/verify/fuzz_<target> <file>`, fix the bug,
and add a regression test to `firmware/tests/`. After changing a harness's input
format, regenerate the seeds with `python3 firmware/fuzz/make_seeds.py`.

### What the verification pack found

- **PIN attempt count reset by a power cut.** When the PIN attempt log filled
  up, compacting it erased the page and then rewrote the pending failures. A
  power cut between the two reset the wrong-PIN count to zero. The compaction
  now goes through the page journal like every other rewrite
  (`test_store_pinlog_compaction`).
- **The tenth wrong PIN could be survived.** If the power failed after the
  tenth attempt was logged but before the wipe ran, the next boot did not
  wipe, and a later correct PIN still unlocked. Mounting now finishes the wipe
  (`test_store_tenth_fail_cut`).
- **Torn log entries counted as failures.** An entry left half-written or
  noisy by a power cut (for example after a correct PIN, or while a new PIN's
  log was erased) counted as a wrong PIN, so an interrupted erase could even
  trigger a wipe. Only complete attempt entries count now.
- **A man in the middle could make his failures look like glitches.** The
  formal model showed that after finishing with one device he knows both SAS
  values, and when they differ he can hold back his REVEAL to the other,
  which then waited forever on "Exchanging keys...". It now gives up after
  15 s and shows *Pairing failed*, and the SAS screen tells people to cancel
  on both devices if the other shows no symbols (`test_app_pair_reveal_timeout`).
- **PIN confirmation used `memcmp`.** It now uses a constant-time compare
  (`ct_equal` in `firmware/core/ct.h`).

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
- **The formal model** is symbolic (perfect cryptography) and the SAS bound
  of 1/15,625 per attempt is argued from it, not mechanised; see
  [formal-verification.md](formal-verification.md).
- **The constant-time check** runs host (x86-64) code. The Cortex-M0+ build
  uses a different compiler and has no data cache, so it is evidence, not
  proof, for the device.
- **Renode** models the peripherals at register level, as far as the firmware
  uses them. Analog behaviour, Stop 2 current and the panel's waveforms are
  not modelled. See `firmware/renode/README.md`.
