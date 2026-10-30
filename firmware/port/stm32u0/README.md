# IRL2FA firmware: STM32U083 port

This directory turns the portable core (`firmware/core/`) into firmware for the
IRL2FA board: an STM32U083KCU6 (Cortex-M0+, 256 KB flash, 40 KB SRAM) with a
Good Display GDEY0213B74 e-paper panel (SSD1680), a Micro Crystal RV-3032-C7 RTC,
a five-way joystick and a plain-UART infrared link.

| File | What it does |
|-|-|
| `main.c` | Bring-up, the `app_poll()` loop, Stop 2 hand-off, the debug block |
| `hal_stm32u0.c` | `core/hal.h`: clocks, SysTick, GPIO/EXTI, I2C1, SPI1, USART1 (IR), TRNG, flash store, ADC (battery), Stop 2 |
| `epd_ssd1680.c` | E-paper driver: load switch, reset, full and partial refresh, rotation |
| `rv3032.c` | RTC driver: BCD time, "time valid" marker, CLKOUT off, countdown wake-up |
| `pins.h` | **Generated** from `hardware/pins.yaml` by `hardware/tools/gen_pins.py`. Do not edit. |
| `startup_stm32u083.c`, `stm32u083kc.ld` | Vector table, C runtime start, memory layout |
| `CMakeLists.txt`, `cmake/arm-none-eabi.cmake`, `build.sh`, `size_report.py` | Build |
| `vendor/` | The few ST/Arm headers the port includes (see below) |

## Building

```sh
firmware/port/stm32u0/build.sh
```

This builds a small Docker image (`debian:bookworm-slim` with Debian's
`gcc-arm-none-eabi` 12.2, newlib-nano, CMake) on first use and writes
`firmware/build/stm32u0/irl2fa.{elf,bin,hex,map}` plus `size-report.txt`. The
build uses `-Os -flto -Wall -Wextra -Werror`, and the size report fails the
build if the image passes 176 KB of flash or leaves less than 4 KB of RAM for the
stack. `NO_DOCKER=1` uses a host toolchain instead.

CMake options: `-DIRL2FA_DEBUG_HOOK=OFF` drops the debug block (see Emulation),
and `-DEPD_ROTATE_180=ON` flips the display if a prototype shows it upside down.

### Memory layout

| Region | Address | Size | Use |
|-|-|-|-|
| Flash, code | `0x08000000` | 176 KB | vectors, code, constants |
| Flash, store | `0x0802C000` | 80 KB = 40 x 2 KB pages | `core/store.c` via `hal_flash_*` |
| SRAM | `0x20000000` | 40 KB | data, bss (about 20 KB), stack |

The linker script refuses an image that would overlap the store. A firmware update
that programs only the image's pages (all the tools below do that by default)
keeps the store, so codes and contacts survive an update at RDP level 0.

### Pins

Every pin the port touches comes from `pins.h`, which is generated from
`hardware/pins.yaml`. After changing the YAML:

```sh
.venv/bin/python hardware/tools/gen_pins.py --docs   # pins.h + docs/pin-assignments.md
```

CI regenerates both and fails if the committed copies differ. Unused pins are
configured as analog inputs (lowest leakage). The joystick directions come from
the `joystick:` section through the generated `JOY_UP`/`JOY_DOWN`/... macros.

## Hardware notes the code relies on

- **Clock:** HSI16 at 16 MHz, voltage range 1, zero flash wait states. No crystals.
  Stop 2 wakes on HSI16 (`RCC_CFGR.STOPWUCK`). SysTick gives `hal_millis()` while
  awake; it stops in Stop 2, so the millisecond count pauses while asleep.
- **Sleep:** Stop 2 (`PWR_CR1.LPMS = 010`, `SLEEPDEEP`, WFI), with ULP sampling of
  the BOR enabled. Wake sources are falling edges on the five joystick lines and on
  RTC_INT (EXTI). A press that lands while the firmware is deciding to sleep is
  noticed and is reported as the press that woke the device. I2C1 is disabled
  (`PE = 0`) for the duration: RM0503 33.5 allows only I2C3 to stay enabled in Stop 2.
  A joystick contact that has been closed for more than 15 s without a new press
  edge (a key jammed in a pocket) does not keep the device awake; the core stops
  auto-repeating such a key after 20 s (`HOLD_LIMIT_MS`).
- **E-paper:** powered only during an update, through the P-FET switch on
  `EPD_PWR_N` (low = on). The order is: supply on, wait 10 ms, raise CS/RST, hardware
  reset, SW reset `0x12`, configure, write both RAMs, update `0x22`/`0x20`, wait for
  BUSY (high = busy) to fall, deep sleep `0x10`, then drive SCK/MOSI/CS/DC/RST low
  and switch the supply off, so no line back-powers the unpowered panel. BUSY is
  analog while the panel is off.
  SPI1 runs at fPCLK/4 = 4 MHz with SCK/MOSI at medium output speed.
  `DISPLAY_FULL` uses display mode 1 (`0x22 = F7`). `DISPLAY_FAST` uses display mode
  2 (`0x22 = FF`), the panel's partial waveform. Mode 2 needs the old image in
  the RED RAM (`0x26`), and that RAM is lost with the supply, so the port keeps a
  copy of the last shown frame (3.9 KB) and writes it back each time.
- **Orientation:** the panel RAM is portrait (122 sources x 250 gates, 16 bytes
  per gate line). The board mounts the panel with the FPC leaving the left short
  edge and FPC pin 1 at the top. That is Good Display's drawing as printed, and it
  is the portrait view turned 90 degrees clockwise, giving
  `RAM x = ly, RAM y = 249 - lx` for landscape pixel `(lx, ly)`. Which corner is
  RAM (0, 0) comes from Good Display's documentation and demo convention. It has
  not been checked on a real panel; if it turns out wrong, `EPD_ROTATE_180` fixes it.
- **RTC:** the RV-3032 has no Unix-time counter, so the port uses the BCD
  registers. The time counts as valid only if PORF and VLF are clear *and* the
  port's marker (`"IRLT"`) is in the RTC's user RAM. `hal_rtc_set()` writes the
  marker. A power loss clears the RAM and sets PORF, so the core then shows the
  clock screen. CLKOUT ships enabled (32.768 kHz); the port clears it in the
  configuration EEPROM once (NCLKE=1, EECMD 11h) and also disables PORIE/VLIE.
  At power-on the RTC needs ~66 ms (tPREFR, EEbusy set) to load that EEPROM into
  its registers, so `rv3032_init()` waits for EEbusy to clear before reading them.
  If SDA is stuck low at boot (an MCU reset in the middle of an RTC read), the port
  clocks the bus free before enabling I2C1.
  `hal_sleep(wake_at)` arms the periodic countdown timer (1 Hz, or 1/60 Hz beyond
  4095 s) to pull INT low.
- **IR:** USART1 at 4800 baud, 8N1, plain UART (not IrDA SIR; the phototransistor
  is too slow for 3/16-bit pulses), `TXINV` and `RXINV` set so the idle line means
  "LED off". `hal_ir_enable(true)` powers the phototransistor (`IR_RX_PWR` high).
  TX and RX are interrupt-driven ring buffers. When IR is off, TX is a GPIO
  driven low and RX is analog.
- **Entropy:** the TRNG, clocked from HSI48. Both are switched on only while
  generating, in the STM32U0 NIST configuration of AN4230 (`RNG_CR = 0x00F00D00`,
  `RNG_HTCR = 0xAAC7`; the reset value is not that configuration), with clock-error
  detection on and seed-error recovery (`CONDRST`, RM0503 21.3.7). If the TRNG
  fails, the device resets rather than return weak randomness.
- **Flash store:** page erase and double-word programming with unlock/lock,
  BSY1/CFGBSY polling, error-flag checks, and read-back verification. Failures
  increment a counter shown in the debug block. Double-words that are all `0xFF`
  are skipped, so they stay erased.
- **Flash ECC:** a program or erase cut by a power loss can leave a double-word
  with inconsistent ECC bits; reading it raises an NMI (RM0503 3.3.2, 3.3.5). Store
  reads are armed for this: the NMI handler clears `FLASH_ECCR.ECCD` and returns, and
  the store sees zeros for that double-word (a torn record, which it already
  handles). Any other NMI resets the device.
- **Battery:** VDDA, which is the cell, is measured through VREFINT on ADC channel
  12 as `3000 mV x VREFINT_CAL / reading`, averaged over 4 samples and cached for
  10 s.

## Flashing (ST-Link + Tag-Connect TC2030)

The board exposes SWD on a TC2030 footprint (no legs: use a **TC2030-CTX-NL**
cable; with an STLINK-V3MINIE use the **TC2030-CTX-STDC14** variant). TC2030
pin-out: 1 VTref, 2 SWDIO, 3 NRST, 4 SWCLK, 5 GND, 6 SWO (not connected).
ST-Links do not power the target, so **fit the CR2450**. The probe senses the
target voltage on pin 1.

**STM32CubeProgrammer** (a release that lists the STM32U0 series):

```sh
STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst -w firmware/build/stm32u0/irl2fa.elf -v -rst
```

**OpenOCD** (a build with `target/stm32u0x.cfg`, i.e. 0.12.0+dev from 2024 or later;
the U0 uses the `stm32l4x` flash driver):

```sh
openocd -f interface/stlink.cfg -c "transport select hla_swd" -f target/stm32u0x.cfg \
        -c "program firmware/build/stm32u0/irl2fa.elf verify reset exit"
```

**probe-rs** (check the exact chip name with `probe-rs chip list | grep -i U083`):

```sh
probe-rs download --chip STM32U083KCUx --connect-under-reset firmware/build/stm32u0/irl2fa.elf
probe-rs reset --chip STM32U083KCUx
```

The firmware spends most of its time in Stop 2. Connect under reset (`mode=UR`,
`--connect-under-reset`, or OpenOCD's `reset_config srst_only` with NRST wired)
so the probe gets in before the core sleeps.

**The very first time a blank chip is programmed, pull the cell (or otherwise
power-cycle the board) afterwards.** A virgin STM32U0 has the `FLASH_ACR.EMPTY`
flag set, and that flag is only re-evaluated when the option bytes load: at a
power-on reset or `OBL_LAUNCH`, not at a system reset or NRST (RM0503 2.5.4). Until
then every reset, including the `-rst` / `reset` at the end of the commands
above, boots the ST bootloader in system memory instead of the firmware: the board
looks dead, and the bootloader reconfigures some GPIOs. Alternatively clear the
flag from the debugger before the reset (OpenOCD: `mmw 0x40022000 0 0x10000`,
which clears bit 16 of `FLASH_ACR`).

## Readout protection (RDP level 1)

The store's master key sits in flash. Without a PIN it is not wrapped by
anything, so RDP is what keeps someone with a debugger from reading the secrets.
Set **level 1** after flashing:

```sh
# STM32CubeProgrammer: 0xAA = level 0, 0xCC = level 2, any other value = level 1
STM32_Programmer_CLI -c port=SWD mode=UR -ob RDP=0xBB
# OpenOCD (stm32l4x driver; `stm32l4x lock 0` sets level 1)
openocd -f interface/stlink.cfg -f target/stm32u0x.cfg -c "init; reset halt; stm32l4x lock 0; reset run; exit"
```

The new option bytes load on the next power-on reset. Pull the battery, or use
`-ob ... -rst` / `OBL_LAUNCH`.

**Program option bytes only from a solid supply.** RM0503 3.4.2 (the caution
under the option byte programming sequence): if the supply drops or a reset
happens while the option bytes are being rewritten, the mismatching values that
load afterwards "force a secure configuration that might permanently lock the
device". A CR2450 near the end of its life, or one sagging under the debugger's
load, is not a safe supply: fit a fresh cell, or power the board from a bench
supply on the battery contacts, before changing RDP.

Consequences of level 1:

- The debugger can still connect (to the running core, or under reset), but
  reading or writing the main flash through SWD, from code loaded into RAM by
  a debugger, or from the system bootloader is refused (RM0503, readout
  protection; check there which RAM/backup areas are covered as well). The
  device itself runs normally.
- **Going back to level 0 mass-erases all of flash, including the store:** every
  code and contact is lost. That includes firmware updates: to reflash a level 1
  device you regress to level 0, which wipes it, and then flash again. Export
  nothing, because there is no export. Plan updates as "re-enrol codes", or
  ship development units at level 0.
- Level 1 does not protect against invasive or fault-injection attacks.
  Published voltage-glitch attacks bypass RDP1 on several STM32 families. A PIN
  (Argon2-wrapped master key) is what protects secrets on a stolen device.
- **Do not use level 2** unless you mean it. It disables SWD for good. The U0
  can regress with a password (the OEM keys in `FLASH_OEMxKEY`), but only if one
  was provisioned before; see RM0503. Without one, level 2 is permanent.

## Vendored code

Only the headers the port includes, from **STM32CubeU0 v1.3.0**
(https://github.com/STMicroelectronics/STM32CubeU0, tag `v1.3.0`, commit
`b42482973bdd79b8bd6403091938828be0063a13`):

| Directory | Files | Upstream (submodule commit) | Licence |
|-|-|-|-|
| `vendor/cmsis_device_u0/` | `stm32u0xx.h`, `stm32u083xx.h`, `system_stm32u0xx.h` | `cmsis_device_u0` `e3a627c6a5bc4eb2388e1885a95cc155e1672253` | Apache-2.0 (`LICENSE.md`) |
| `vendor/stm32u0xx_ll/` | `stm32u0xx_ll_gpio.h`, `stm32u0xx_ll_adc.h` | `stm32u0xx-hal-driver` `1ffd154dcea8257be8981d639db0142a19167241` | BSD-3-Clause (`LICENSE.md`) |
| `vendor/cmsis_core/` | `cmsis_compiler.h`, `cmsis_gcc.h`, `cmsis_version.h`, `core_cm0plus.h`, `mpu_armv7.h` | Arm CMSIS 5.9.0 as shipped in `Drivers/CMSIS` of the same Cube release | Apache-2.0 (`LICENSE.txt`) |

The files are unmodified. The startup code and linker script are original and do
not use ST's templates.

## Emulation (Renode)

`firmware/renode/run.sh` builds this port and runs `firmware/renode/irl2fa.robot`
in the `antmicro/renode` Docker image. **It tests the release image**: nothing
is compiled differently for emulation. The tests find the screen the core is on
through the debug block `irl2fa_debug` (`main.c`), a 40-byte RAM structure that
holds poll and sleep counters, error counters and `app_screen_name()`. Nothing
secret is in it. It is read by symbol with `sysbus ReadBytes`, without
semihosting.

Renode has no STM32U0 platform. `firmware/renode/stm32u083.repl` builds one from
Renode's generic models (Cortex-M0+ with NVIC/SysTick, `STM32_GPIOPort`,
`STM32F7_USART`, `STM32F7_I2C`, `STM32SPI`, memories) and the models in
`firmware/renode/models/`.

**Modelled** (behaviour the firmware depends on):

- RCC: oscillator ready flags follow their enables; SWS follows SW.
- PWR: plain registers; voltage scaling always reports ready.
- FLASH interface: KEYR unlock sequence, LOCK, page erase (fills the page with
  0xFF), EOP. Programming is the CPU writing the flash array, as on silicon.
- EXTI: the U0 register map, with per-line port selection through EXTICR,
  rising and falling pending flags, IMR, and the three NVIC lines.
- RNG: CONDRST, DRDY, and DR from a seeded PRNG. Each emulated device gets its
  own seed. Configuration fields and `RNG_HTCR` only take writes with CONDRST set;
  words drawn outside the AN4230 configuration are counted, and the tests require
  none.
- PWR: a Stop 2 entry audit. Every WFI with SLEEPDEEP and LPMS = Stop 2 checks
  for pending EXTI bits, LPR, peripherals that may not stay enabled in Stop 2
  (I2C1, SPI1, USART1, ADC, RNG) and floating digital inputs; the tests require
  no violation.
- Every register offset and bit the models use is in `U0Map`
  (`models/STM32U0_System.cs`), and `firmware/tools/check_hw_constants.py`
  (part of `make firmware` and CI) checks those, the platform's base addresses
  and IRQ numbers, the vector table and the linker memory map against
  `vendor/cmsis_device_u0/stm32u083xx.h`.
- ADC: regulator, calibration, ADEN/ADRDY, CHSELR/CCRDY, single conversions;
  VREFINT reads as 1.212 V at VDDA = 3.0 V.
- SSD1680: SPI commands with D/C# and CS#, both RAMs, data entry modes, windows
  and address counters, SW and HW reset, deep sleep, BUSY timing (3 s full,
  0.42 s partial). It also checks that the old image is in RAM 0x26 for partial
  updates, that no command is sent while BUSY is high, that no line is high
  while the panel is unpowered, that the supply was on for 10 ms before the
  first command and that RES# was held low for 10 ms. It reconstructs the shown frame, which can be
  dumped as PBM or PNG.
- RV-3032: register pointer, BCD time on virtual time, status flags (PORF at
  power-on), user RAM, configuration RAM and EEPROM update, the 66 ms power-on
  EEPROM refresh (EEbusy, configuration registers reading 00h until it is done),
  the countdown timer and the INT pulse. An MCU reset (`machine RequestReset`)
  leaves it running; `PowerOnReset` is a cell change.
- Joystick: buttons with pull-up semantics. IR: two machines' USART1 joined by
  `emulation CreateUARTHub`.

**Not modelled / not tested by emulation:**

- **Stop 2 itself.** WFI with SLEEPDEEP halts the emulated core until an
  interrupt arrives, and the firmware disables SysTick first. So "asleep until
  the joystick" is real behaviour, but the clock gating, the regulator modes and
  the current draw are not emulated. Renode wakes from WFI on a pending interrupt
  even with PRIMASK set, as Cortex-M silicon does, so the emulation build needs no
  special flag.
- **Electrical and timing detail:** baud rate, USART inversion (TXINV/RXINV are
  accepted but not modelled), I2C timing and NACK on the wire, SPI clock, GPIO
  pull-ups, analog mode, current consumption.
- **IR physics:** the UART hub delivers bytes instantly, without loss,
  collisions, or a device hearing its own echo. Real half-duplex IR will collide;
  the pairing protocol's retransmissions and jitter are meant to absorb that, but
  only hardware testing will show how well.
- **Panel waveforms, temperature and ghosting.** The model does not emulate the
  orientation; it shares the firmware's mapping. The tests therefore prove the
  driver is self-consistent and that the controller protocol is right, not
  which corner is up on a real panel.
- **Flash:** programming errors (writing a non-erased double-word, alignment)
  and timing; a CPU write lands even without PG set. The firmware's own read-back
  verification is what the tests check. ECC *is* modelled for injected faults
  (`sysbus.flash_ctrl InjectEccError <address>`: ECCD and an NMI on every read of
  that double-word until its page is erased), which is how a torn write is
  tested.
- **RTC wake-up timer:** it is modelled, but the core never asks for a timed
  wake-up (`app_wake_at()` returns 0), so no test exercises it.
- **TRNG statistics, AES, option bytes / RDP, watchdogs, the LSE/LSI, DBGMCU.**
