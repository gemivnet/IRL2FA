## IRL2FA v0.2.0: reviewed against the datasheets, signed updates, simple mode

The first release whose board files have been checked against every
manufacturer's datasheet and drawing, and the first with signed firmware.
**No board has been built yet**: treat it as a prototype and please
[file a build report](https://github.com/gemivnet/IRL2FA/issues/new?template=build_report.yml).
v0.1.0 is superseded: do not order from it.

**Try it first:** the [browser simulator](https://irl2fa.com/) runs the same firmware.

### Downloads

| File | What it is |
|-|-|
| `irl2fa-gerbers-*.zip` | Gerbers and drill files: 2 layers, **0.8 mm, ENIG** |
| `irl2fa-bom-jlcpcb.csv`, `irl2fa-cpl-jlcpcb.csv` | JLCPCB assembly BOM and placement (both sides; rotations corrected for JLCPCB's footprints) |
| `irl2fa-bom.csv`, `irl2fa-ibom.html` | full BOM and interactive BOM for hand assembly |
| `irl2fa-schematic.pdf`, `irl2fa.step` | schematic and board 3D model |
| `irl2fa-factory-*.bin` / `.hex` | **factory image** (bootloader + signed app) for the first flash over SWD, or for JLCPCB's programming service |
| `irl2fa-*.irl2fa` | **signed update** for devices already running IRL2FA, sent over IR ([how](https://github.com/gemivnet/IRL2FA/blob/main/docs/firmware-updates.md)) |
| `irl2fa-stm32u083-*.elf`, `boot-stm32u083-*.elf`, `firmware-size-report.txt` | debug builds and the flash budget |
| `irl2fa-case-stl-*.zip`, `irl2fa-all.3mf` | 3D-printable case (front, back, battery door, joystick cap) |

Order following [Build one](https://github.com/gemivnet/IRL2FA#build-one): JLCPCB needs a
1 × 2 panel with edge rails for two-sided assembly, and the board must be ENIG.

### What changed since v0.1.0

**Four reviews against primary sources** (electrical, assembly, mechanical,
firmware against the silicon and ST's errata) found and fixed defects that
every earlier check had passed:

- **Board.**
  - The display flex is 14.3 mm wide where it goes through the board, so the slot is longer now.
  - The side-looking IR LED and phototransistor faced into the board; they now face out of the top edge.
  - The ground pours were not connected to ground.
  - Power nets now get their wider tracks.
  - The IR receiver has a daylight filter (VEMT2023SLX01), and the IR LED gets more current (R8 = 68 Ω).
  - The display booster switch has a lower gate threshold (NX3008NBK).
  - No tracks or vias run where the coin cell slides or rests.
  - The silkscreen meets JLCPCB's minimum.
  - Five SWD test pads let JLCPCB flash the firmware.
  - Board size: 77 × 31 × 0.8 mm. Case: 80.9 × 34.9 × 11.0 mm.
- **Assembly.**
  - JLCPCB would have placed 9 of 14 oriented parts wrong and put the FPC connector 1.25 mm off its pads.
  - The placement file now uses JLCPCB's footprints and back-side convention, checked pad by pad.
- **Case.** The joystick cap now grips the stem, and the glass rib no longer presses on the flex.
- **Firmware against the silicon.**
  - A battery pulled during a flash write could leave the device in an endless reset loop.
  - A stuck key could drain the cell.
  - I2C1 was left on in Stop 2.
  - The RTC configuration was read before the chip had loaded it.
  - Two ST errata affected us: a blank VREFINT calibration value, and a spurious I2C bus error.

**New in the firmware.**

- **Signed updates over IR.**
  - Updates come from another IRL2FA or from a USB-serial IR adapter.
  - Only images signed with the release key are accepted, and never an older version.
  - Codes and contacts are kept.
  - A battery pull at any point leaves the old or the new firmware bootable.
- **Simple mode** for someone who should only check callers.
  - The device shows "Susan will say: *word*" in large type.
  - Only up and down work.
  - A family PIN, entered after a 10 s hold, protects the settings.
- **Lock debug port** in Settings. Readout protection is never switched on automatically.
- **Power.** Battery life is measured on the emulated firmware: about **2.8 years typical, 1.6 years worst case** at 20 uses a day.

**Verification added:**

- footprints against datasheet land patterns
- a formal (Tamarin) proof of the pairing protocol
- fuzzing
- power cuts at every flash write
- constant-time checks
- 97 % code coverage
- part orientation
- JLCPCB placement
- interference between the case and the board
- a Renode audit of every Stop 2 entry

### Still to confirm on the first boards

- display orientation
- IR range and ambient-light rejection
- sleep current against the budget
- joystick directions
- a full refresh from a 2.3 V supply
- coin-cell insertion through the door

The checklist is in [docs/pcb-design.md](https://github.com/gemivnet/IRL2FA/blob/main/docs/pcb-design.md#things-to-verify-on-the-first-boards).
