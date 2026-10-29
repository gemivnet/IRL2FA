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
| **Footprints vs datasheets** | a pad, pitch, exposed pad, hole or pin 1 that disagrees with the manufacturer's land pattern; symbol pins and pads that do not match; a new footprint with no datasheet entry (the test fails until one is added) | `hardware/tests/footprints.yaml` (dimensions, datasheet URL and page per part), `hardware/tests/test_footprints.py` |
| **Netlist vs pinout** | an MCU pin moved in the schematic but not in `hardware/pins.yaml`, and so not in the firmware; missing decoupling, pull-ups or LCSC numbers; single-pin nets | `hardware/tests/test_netlist.py` |
| **SPICE benches** | the e-paper booster no longer reaching ±20 V or exceeding part ratings; a coin-cell sag that would brown out a refresh; load-switch inrush; reverse-battery leakage; IR LED current out of range | `hardware/spice/*.cir`, limits in `hardware/spice/checks.yaml`, run by `hardware/tests/test_spice.py` |
| **Power budget** | a part change that pushes worst-case battery life below 1 year | `hardware/power/model.json`, `hardware/tests/test_power.py` |
| **Fabrication outputs are current** | KiCad files edited without regenerating the Gerbers, drill files, BOM and placement files people order from | `hardware/tools/fab.py --check`, `hardware/tests/test_fab.py` |
| **Gerber sanity** | outline missing or the wrong size, drills outside fab limits, BOM and placement file disagreeing | `hardware/tests/test_fab.py` (gerbonara) |
| **Case fit** | case clearance under 0.3 mm to any part; the joystick hole, screen window or IR windows misaligned | `case/tests/` |
| **Firmware pins** | `firmware/port/stm32u0/pins.h` not regenerated after a pinout change | `firmware.yml` |
| **Firmware on emulated hardware** | the real STM32 binary, run in Renode with modelled SSD1680, RV-3032, joystick and IR link: boot, menus, two devices pairing | `firmware/renode/` |
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
- **PIN confirmation used `memcmp`.** It now uses a constant-time compare
  (`ct_equal` in `firmware/core/ct.h`).

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
- **The formal model** is symbolic (perfect cryptography) and the SAS bound
  of 1/15,625 per attempt is argued from it, not mechanised; see
  [formal-verification.md](formal-verification.md).
- **The constant-time check** runs host (x86-64) code. The Cortex-M0+ build
  uses a different compiler and has no data cache, so it is evidence, not
  proof, for the device.
- **Renode** models the peripherals at register level, as far as the firmware
  uses them. Analog behaviour, Stop 2 current and the panel's waveforms are
  not modelled. See `firmware/renode/README.md`.
