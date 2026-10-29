# Design review (October 2026)

This review was written when the project restarted. It records what the 2025
design got wrong and what changed as a result, so the reasoning behind the
current design stays visible.

## Where the 2025 design stood

- **Schematic.** 7 parts placed and 24 wires; most MCU pins were unconnected.
- **PCB.** Footprints dropped onto a 31.5 × 24 mm outline. That is smaller
  than the 59 × 29 mm display glass, and the CR2450 holder alone is 30 mm wide.
- **Documentation.** The pin assignment and power documents were empty, and
  the BOM total was blank.
- **Firmware.** None.
- **Licensing.**
  - The repository had no licence, so nobody could legally reuse it.
  - Its footprints and 3D models came from SamacSys/Mouser, whose terms
    forbid redistributing them as a library.
  - Those files have been removed from the git history.

## Problems found

### 1. The MCU was too small

The ATtiny1616 has 16 KB of flash and 2 KB of RAM.

- **RAM.** A 250×122 1-bit framebuffer is 3.9 KB, so it doesn't fit.
- **Secret storage.** Secrets were to live on an external SPI EEPROM in plain
  text. Anyone who opened a lost device could read every TOTP secret with a
  clip-on programmer.
- **Code size.** HMAC-SHA1/SHA256, X25519, a 2048-word list and the fonts don't
  fit in 16 KB.

**Fix: STM32U083KCU6** (256 KB flash, 40 KB RAM).

- Secrets live in internal flash, sealed with XChaCha20-Poly1305, behind
  readout protection.
- An optional PIN derives an Argon2i key that wraps the master key, and ten
  wrong PINs erase the store.

### 2. Pairing was brute-forceable

The README claimed "95 trillion combinations" for a 20-symbol joystick code.
That figure is 5^20 ≈ 2^46.

- A few observed verification words are enough to test every seed offline:
  roughly GPU-days of work.
- Typing a 128-bit secret with a joystick would take about 55 symbols.

**Fix: an IR link with an X25519 key agreement.**

- The exchange uses commit/reveal so a man in the middle can't grind keys.
- Both screens show a six-symbol short authentication string that the two
  people compare.
- The result is a 256-bit contact key, and nobody types anything.

Details: [pairing-protocol.md](pairing-protocol.md).

### 3. The display could not have worked

E-paper panels in this family need an external boost converter (inductor,
MOSFET, three Schottky diodes, about ten capacitors) to make their ±20 V gate
rails. None of it was in the schematic, and nothing cut the panel's power.

**Fix:**

- The Good Display reference booster and a P-FET load switch.
- A SPICE bench checks that the rails come up and stay inside part ratings.

### 4. Missing basics

| Missing | Added |
|-|-|
| Programming connector | Tag-Connect TC2030 SWD pads (no part to buy) |
| I2C pull-ups | 4.7 kΩ on SCL/SDA |
| Bulk capacitance for refresh current peaks | 2 × 47 µF X5R |
| Reverse-battery protection | AO3401A P-FET (no diode drop) |
| Decoupling | 100 nF per supply pin plus 4.7 µF, 1 µF + 10 nF on VDDA |
| Battery-voltage measurement | VREFINT measured by the ADC (no divider, no leakage) |

### 5. The display refresh plan would have drained the battery

The original text said the words change every 15 seconds. Refreshing the
e-paper that often around the clock costs about 1 mAh a day in refreshes
alone.

**Fix:** the device sleeps and draws nothing on screen until you press the
joystick. It refreshes only while you are looking at it, and blanks to a
neutral sleep screen afterwards. It also redraws only when the content changes, with partial refreshes for the countdown bars. Result: about 2.8 years typical (1.6 worst case) on a CR2450 at 20 uses a day, as measured on the emulated firmware
([power-calculations.md](power-calculations.md)).

### 6. Clock drift was not considered

Air-gapped means no network time, and TOTP fails once the clock is about 30 s
off.

**Fix:**

- The RV-3032-C7 is temperature-compensated (±2.5 ppm, at most about 79 s a
  year).
- The UI makes setting the time easy.
- Contact words accept the previous 15 s window.

### 7. Parts that couldn't be bought

The RTC, FPC connector, joystick and battery holder all had zero stock at the
assembler, or no pinout published. Each was replaced with an in-stock
equivalent that JLCPCB can assemble. See [hardware-options.md](hardware-options.md).

## Process changes

- **Hardware CI.** ERC, DRC with schematic parity, netlist-versus-pinout
  checks, SPICE benches, the power budget, and a check that the committed
  Gerbers match the KiCad files: [hardware-testing.md](hardware-testing.md).
- **Firmware.**
  - Portable C core with RFC test vectors.
  - The same code runs in a [browser simulator](../sim/) and on the device.
  - The device firmware is also exercised on an emulated STM32 in Renode.
- **Single sources of truth.** `hardware/pins.yaml` (pinout) and
  `hardware/board.yaml` (mechanics) feed the firmware, the tests and the case.
