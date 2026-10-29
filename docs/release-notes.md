## IRL2FA v0.1.0: first complete design

The first release with everything needed to build an IRL2FA. The design is
simulated and checked end to end, but **no board has been built yet**: treat
this as a prototype release and please [file a build report](https://github.com/gemivnet/IRL2FA/issues/new?template=build_report.yml).

**Try it first:** the [browser simulator](https://irl2fa.com/) runs the same firmware.

### Downloads

| File | What it is |
|-|-|
| `irl2fa-gerbers-*.zip` | Gerbers and drill files for JLCPCB, PCBWay or similar (2 layers, 0.8 mm, ENIG) |
| `irl2fa-bom-jlcpcb.csv`, `irl2fa-cpl-jlcpcb.csv` | JLCPCB assembly BOM and placement (both sides) |
| `irl2fa-bom.csv`, `irl2fa-ibom.html` | full BOM and interactive BOM for hand assembly |
| `irl2fa-schematic.pdf`, `irl2fa.step` | schematic and board 3D model |
| `irl2fa-stm32u083-*.bin` / `.hex` / `.elf` | firmware, flashed over SWD with a Tag-Connect TC2030-NL |
| `irl2fa-case-stl-*.zip`, `irl2fa-all.3mf` | 3D-printable case (front, back, battery door, joystick cap) |

### What's in it

- **Hardware.**
  - STM32U083 MCU, 2.13″ 250×122 e-paper (Good Display GDEY0213B74), RV-3032 temperature-compensated RTC.
  - Alps 5-way joystick, IR pairing link, CR2450 cell.
  - About 2.8 years typical (1.6 worst case) battery life at 20 uses a day, measured on the emulated firmware.
- **Firmware.**
  - Person-to-person verification words.
  - In-person IR pairing: X25519 with commit/reveal and a 6-symbol comparison string. The devices listen before they talk on the half-duplex link, and give up after a minute if nobody answers.
  - The screen is redrawn only when its content changes; countdown bars use partial refreshes, so a code or the words left on screen no longer cost a full refresh every 30 s.
  - No lost presses: a press made while the e-paper refreshes is handled afterwards, two presses as two.
  - TOTP (SHA1/256/512, 6/8 digits, 30/60 s, 512 accounts).
  - Encrypted storage with an optional joystick PIN.
- **Verification.**
  - RFC test vectors and SPICE benches.
  - Netlist checks against the shared pin map.
  - A check that the fab outputs are current, and case-fit tests.
  - Renode emulation of the real firmware pairing two virtual devices.

### Known open points (need a real board)

- E-paper image orientation (`-DEPD_ROTATE_180=ON` flips it).
- IR link range and reliability at 4800 baud.
- Measured sleep current, against the 1.2 µA budget.
- Joystick direction mapping after the 45° rotation.
