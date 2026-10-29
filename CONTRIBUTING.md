# Contributing to IRL2FA

Thanks for helping. IRL2FA is meant to be built by people who have never
seen this repository before, so every change has to leave `main` in a state
someone can order from.

## Ground rules

- **`main` must always be orderable.** CI simulates and checks the
  electronics, regenerates the fabrication files and compares them with the
  committed ones ([docs/hardware-testing.md](docs/hardware-testing.md)). A
  pull request that fails a check is not merged.
- **Pins live in one place.** If you move an MCU pin, edit
  `hardware/pins.yaml`, then run `python3 hardware/tools/gen_pins.py`, which
  updates the firmware header and the docs.
- **Mechanics live in one place.** `hardware/board.yaml` drives the case. If
  you move the joystick, IR parts or holes, update it and run
  `python3 case/tools/gen_dims.py`.
- **Library parts must be redistributable.** Don't add symbols, footprints or
  3D models from SamacSys, SnapEDA, Ultra Librarian or EasyEDA. Use KiCad's
  stock libraries, or draw the part from its datasheet as described in
  [hardware/lib/README.md](hardware/lib/README.md).
- **Filenames must be valid on Windows.** No `< > : " / \ | ? *`, no trailing
  dots or spaces.

## Development setup

| Area | Tools | Commands |
|-|-|-|
| Firmware core | a C compiler | `make -C firmware test` |
| STM32 build | Docker | `firmware/port/stm32u0/build.sh` |
| Emulation | Docker | `firmware/renode/run.sh` |
| Simulator | Docker, Node 20+ | `sim/build.sh && node --test sim/test` |
| Hardware | Docker (KiCad 9 inside) | see [docs/hardware-testing.md](docs/hardware-testing.md) |
| Case | Docker (OpenSCAD inside), Python | `case/build.sh && python -m pytest case/tests` |

`make check` at the root runs all of them.

## Pull requests

- Keep them focused, and describe what you tested on real hardware, if you
  have any.
- Hardware PRs: CI attaches before/after renders of the schematic and both
  board sides. Look at them.
- Firmware PRs: add or update tests in `firmware/tests/`. The core must stay
  portable C11 with no hardware access outside `hal.h`.
- Security-relevant changes (crypto, pairing, storage) need a clear rationale
  in the PR description and an update to [docs/pairing-protocol.md](docs/pairing-protocol.md).

## Reporting security issues

Please don't open a public issue for a vulnerability. Use GitHub's private
vulnerability reporting (Security tab → "Report a vulnerability").

## Licences

By contributing you agree that your contribution is licensed under the
licence of the part of the project it touches:

- AGPL-3.0-or-later for firmware and the simulator
- CERN-OHL-S-2.0 for hardware
- CC-BY-SA-4.0 for the case
