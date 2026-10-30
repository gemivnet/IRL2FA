# Firmware review against the silicon

A review of the STM32U0 port (`firmware/port/stm32u0/`), the parts of
`firmware/core/` that touch hardware behaviour, and the Renode platform and
models (`firmware/renode/`), against the manufacturers' documents rather than
against the emulator. The question asked: will this firmware run on the first
board that is powered up, and keep running? Renode passing is not evidence of
that, because its peripheral models are permissive and several are ours.

Date: 2026-10-30. Branch `review-firmware`.

## Sources

| Document | Version | How it was read |
|-|-|-|
| RM0503, STM32U0 reference manual | HTML transcription of the current revision at docs.dirba.io/rm0503-stm32u0 (chapters 2-5, 7, 8, 11, 12, 14, 21, 33, 34, 36, 38, 39) | st.com refuses downloads from this environment; every value used below was cross-checked against ST's own generated sources (next rows) where those carry it |
| ST CMSIS device header `stm32u083xx.h` | STM32CubeU0 v1.3.0, vendored in `firmware/port/stm32u0/vendor/` (Apache-2.0) | ground truth for every register address, offset, bit and IRQ number |
| ST HAL/LL for STM32U0 (`stm32u0xx_hal_rng.c`, `_ll_rcc.h`, `_ll_utils.c`, `_hal_flash.c`, `_hal_gpio_ex.h`) | github.com/STMicroelectronics/stm32u0xx-hal-driver, master | clock selector encodings, flash latency table, RNG bring-up, AF numbers |
| ST open pin data, `STM32U083KCUx.xml` and `GPIO-STM32U073x_gpio_v1_0_Modes.xml` | github.com/STMicroelectronics/STM32_open_pin_data | UFQFPN32 pinout and per-pin AF numbers (what STM32CubeMX uses) |
| AN4230 Table 3, STM32U0 RNG NIST configuration | as carried by Zephyr `dts/arm/st/u0/stm32u0.dtsi` (`nist-config = <0xf00d00>`, `health-test-config = <0xaac7>`) | AN4230 itself is on st.com |
| SSD1680 datasheet | Rev 1.4, April 2021 | PDF |
| Good Display GDEY0213B74 specification | as published by Good Display | PDF |
| Micro Crystal RV-3032-C7 Application Manual | Rev 1.3, May 2023 | PDF |
| Vishay VEMT2523SLX01 datasheet | Rev 1.0, 05-Apr-13 (doc 84167) | PDF |
| NXP UM10204 I2C-bus specification | section 3.1.16 (bus clear) | from knowledge of the document |

**Obtained later:** the device errata sheet ES0602 Rev 3 (February 2024; saved
by the owner, not redistributed) and the STM32U083 datasheet (LCSC's copy of
the Chinese edition; VREFINT on p.45). See "Errata ES0602, entry by entry" at
the end.

## Findings, by severity

### F1 (critical): one torn flash write bricks the device in a reset loop

**Evidence.** RM0503 3.3.2: flash words are 64 data + 8 ECC bits; "When two errors
are detected, a flag ECCD is set ... In this case, an NMI is generated." RM0503
3.3.5, caution: "any ongoing memory operation that is interrupted by a physical
event (such as a reset or a power off) may result in flash memory data
corruption, and/or inconsistent ECC bits. The target address may trigger an ECC
interrupt or an NMI when accessed later on ... manage ECC events in the
firmware."

The record store is designed around exactly that event: `core/store.c` journals
page rewrites and treats torn PIN-log entries as noise, and
`tests/test_powercut.c` cuts the power at every flash operation. But the port
had no NMI handler: `NMI_Handler` was a weak alias of `Default_Handler`, which
calls `NVIC_SystemReset()`. `store_mount()` runs at every boot and reads the
journal header, the PIN log (`store_pin_fails()`) and every slot's type byte.
So a cell pulled while a PIN-log entry or a page was being written leaves a
double-word that raises an NMI on every boot, and the device resets for ever.
Reflashing the firmware does not help (the store pages are kept by design); at
RDP level 1 the only way out is the RDP regression mass erase, which destroys
every secret on the device. The host power-cut tests cannot see this (the
virtual flash returns garbage, not an exception), and Renode did not model ECC.

**Fix** (`hal_stm32u0.c`). A real `NMI_Handler`: if the NMI happens while a
store read is "armed" and the error is not in system flash, it clears
`FLASH_ECCR.ECCD` (and a stale `ECCC`, which would otherwise stop ECCR recording
the next error), notes the hit and returns; any other NMI still resets. Every
read of the store (`hal_flash_read`, and the read-back verification after an
erase or a program) now goes double-word by double-word through
`store_dword()`, which arms the handler, reads, issues `DSB`/`ISB` so a pending
NMI is taken before the flag is examined, and on a hit returns zeros for that
double-word, counts a flash error and resets the flash cache (RM0503 3.3.2: a
re-read may otherwise be served from the buffer without the error). Zeros are
never mistaken for erased flash (0xFF) nor for a valid record or PIN-log entry,
so the store treats the double-word as the torn data it is designed to recover
from.

**Regression test.** The Renode flash model now models ECC faults:
`sysbus.flash_ctrl InjectEccError <address>` makes every CPU read of that
double-word set ECCD and raise an NMI, until its page is erased, and the fault
survives an MCU reset. The Renode platform now aliases flash at 0, as the chip
does, so a reset (firmware or test initiated) boots the image again. New test
*Survives A Torn Flash Double-Word At Boot* tears the first PIN-log entry and
resets the MCU: the old firmware reset 47 times in 10 s and never reached a
screen; the new one boots to the menu, counts the error and works.

### F2 (high): a key held down kept the device awake and refreshing until the cell was flat

**Evidence.** `core/app.c`: UP/DOWN (and LEFT/RIGHT on the text and clock
editors) auto-repeat every 90 ms for as long as they are held, each repeat a
display refresh, and `app_poll()` only goes to sleep when `ui.held == 0`.
`hal_sleep()` also refused to enter Stop 2 while any contact was closed. A key
fob lives in a pocket; a joystick pressed by a key or a phone stays pressed. The
result was a panel refresh every 90 ms (SSD1680 operating current ~1 mA from
VCI plus the MCU awake) or, for the centre key, the MCU in Sleep mode with
SysTick at 1 kHz for ever, plus the 40 kohm pull-up current through the closed
contact. Either way a CR2450 lasts days instead of years.

**Fix.** `core/app.c`: a key held longer than `HOLD_LIMIT_MS` (20 s, longer than
any real hold: a full list or a clock field from 0 to 59 at 90 ms per step)
stops repeating and stops counting as activity, so the normal idle timeout puts
the device to sleep. `hal_stm32u0.c`: `hal_sleep()` still refuses to sleep for
a fresh press (a latched edge, a pending EXTI, a press held back by the
filter), but ignores a contact that has been closed without a new edge for more
than `BTN_STUCK_MS` (15 s). That line cannot wake the device until it is
released and pressed again, which is what a person does.

**Regression test.** Host test `test_app_stuck_key_sleeps` holds DOWN, then
CENTER, and requires the app to reach `APP_SLEEP` within a minute with a
bounded number of refreshes. The old core never slept.

### F3 (high): I2C1 left enabled in Stop 2

**Evidence.** RM0503 33.5 (I2C in low-power modes): "Only I2C3 supports wake-up
from Stop 2 mode. The other I2C instances must be disabled before entering
Stop 2 mode." RM0503 4.3.8: "All the peripherals which cannot be enabled in
Stop 2 mode must be either disabled by clearing the Enable bit in the
peripheral itself, or put under reset state." `hal_sleep()` entered Stop 2 with
`I2C1_CR1.PE = 1`. What the silicon does in that case is undocumented (extra
current, or an I2C state machine that comes back confused and costs the RTC
reads after wake-up, which would show as "time lost"); it is outside the
specification either way.

**Fix.** `I2C1->CR1 &= ~PE` before the WFI, `|= PE` after (TIMINGR is kept).

**Regression test.** The Renode PWR model now audits every Stop 2 entry (WFI
with SLEEPDEEP and LPMS = 010): EXTI pending bits (RM0503 Table 28: Stop 2 entry
is ignored otherwise), LPR (Stop 1 instead), I2C1/SPI1/USART1/ADC/RNG enabled,
and floating digital inputs. `Device Healthy` requires no violation, and
*Sleeps In Stop 2 And Wakes On The Joystick* requires that the audit ran. The
old firmware fails with "Stop 2 entered with I2C1 enabled".

### F4 (medium): the first boot after programming a blank chip runs the ST bootloader, not the firmware

**Evidence.** RM0503 2.5.4: a virgin device has `FLASH_ACR.EMPTY` set, which
selects the system-memory bootloader; "The EMPTY flag is updated by hardware
only during the loading of option bytes ... after programming of a virgin
device, a power on reset or setting of OBL_LAUNCH bit in FLASH_CR register is
required to clear the EMPTY flag (the system reset has no impact on this
flag)." Every flashing recipe in the port README ends with a system reset
(`-rst`, `reset`, `probe-rs reset`). On a new board that reset starts the
bootloader: the display stays blank and the board looks dead, which is exactly
the first-power-up failure this review is meant to prevent, except that nothing
is wrong.

**Fix.** README, Flashing: after the first programming of a blank chip, pull
the cell (power-on reset), or clear `FLASH_ACR.EMPTY` from the debugger
(`mmw 0x40022000 0 0x10000`) before the reset. Not testable in emulation.

### F5 (medium): SPI clock 8 MHz through low-speed outputs

**Evidence.** `spi_open()` ran SPI1 at fPCLK/2 = 8 MHz (RM0503 36.6.1, BR = 000),
while `pin_config()` sets every pin, the SPI's SCK and MOSI included, to
OSPEED = 00 (low speed). The SSD1680 accepts 20 MHz on writes (SSD1680 Table
12-1, 25 ns minimum high/low), but its inputs need 0.8 VDDIO (Table 11-1) and
the clock half period at 8 MHz is 62 ns. On ST's parts of the same I/O
generation the low-speed setting is specified only to a few MHz into 50 pF, and
less at the low end of a coin cell's voltage; the board drives the panel
through its FPC. The U083 datasheet's I/O AC table could not be downloaded (see
Sources), so this is a margin finding, not a demonstrated failure.

**Fix.** SPI1 at fPCLK/4 = 4 MHz and SCK/MOSI at medium speed while the SPI is
open. The cost is about 8 ms per refresh against 420 ms of partial update.

**Regression test.** *Drives The Panel SPI Within Its Limits* reads SPI1_CR1.BR
and GPIOA_OSPEEDR during the first refresh. The old firmware fails (BR = 0).

### F6 (medium): RV-3032 configuration read before the chip had loaded it

**Evidence.** RV-3032-C7 application manual 4.6.1 and 4.6.7: at power-on the
configuration EEPROM is copied into the RAM mirror C0h-CAh, which takes
tPREFR ~66 ms with EEbusy set; until then those registers hold their power-on
value 00h. The MCU reaches `rv3032_init()` a few milliseconds after the cell is
inserted, read PMU = 00h (NCLKE = 0, CLKOUT on) whatever the EEPROM held, and
rewrote the configuration EEPROM (46 ms, tUPDATE) from that stale value on every
cell change. Harmless today because the only non-zero field is NCLKE, but any
future PMU setting (backup switchover, trickle charge) would have been
overwritten at every battery change.

**Fix.** `rv3032_init()` waits for EEbusy to clear (the existing `ee_idle()`)
before reading anything.

**Regression test.** The RV3032 model now has the 66 ms power-on refresh
(EEbusy, configuration reading 00h until loaded, EEPROM non-volatile across
power-ons) and counts configuration reads made before it finished. An MCU
reset no longer resets the RTC model (the RTC keeps its own supply);
`PowerOnReset` is a cell change. *Writes The RTC Configuration EEPROM Only Once*
power-cycles RTC and MCU together: the old firmware writes the EEPROM again.

### F7 (medium): option-byte programming on a coin cell can lock the part permanently

**Evidence.** RM0503 3.4.2, caution: "Upon an option byte programming failure
(for any reason, such as loss of power or a reset during the option byte change
sequence), the mismatch values of the option bytes are loaded after reset. Those
mismatch values force a secure configuration that might permanently lock the
device. ... only program option bytes in a safe environment-safe supply". The
README's RDP level 1 recipe runs on the board's CR2450.

**Fix.** README, Readout protection: set RDP with a fresh cell or a bench supply
on the battery contacts. Since the firmware updates
([firmware-updates.md](firmware-updates.md#locking-the-debug-port)), the
owner can also set it on the device, Settings → Lock debug port. It is
never automatic. The bootloader writes the option bytes only then, for a
signed application, and only if the cell measures ≥ 2.9 V twice (2.96 V
without the VREFINT calibration); otherwise it declines and says so. The
application never writes option bytes.

### F8 (low): TRNG ran in a configuration ST never characterised

**Evidence.** RM0503 21.7.1: RNG_CR resets to 0x00800D00 (RNG_CONFIG1 = 0x08).
RM0503 21.6.2 Table 115 lists the tested configurations: A (NIST, values in
AN4230), B (RNG_CONFIG1 = 0x18) and C (RNG_CONFIG1 = 0x0F); the reset value is
none of them. AN4230 Table 3 gives the STM32U0 NIST configuration as RNG_CR =
0x00F00D00 with RNG_HTCR = 0xAAC7. The port, like ST's HAL_RNG_Init, kept the
reset value. The health tests still run, so this is not weak output by itself,
but the entropy claim rests on a configuration ST validated.

**Fix.** Write RNG_CR = 0x00F00D00 together with CONDRST, RNG_HTCR = 0xAAC7 while
CONDRST is set (RM0503 21.7.5: HTCR only takes writes then), then clear
CONDRST. Also: the seed-error recovery now follows RM0503 21.3.7 (CONDRST,
wait, then clear SEIS), and a word read across a seed error is discarded
(SEIS re-checked after reading DR, as ST's HAL does).

**Regression test.** The RNG model now only changes its configuration fields
and HTCR while CONDRST is set, and counts words drawn outside the AN4230
configuration; `Device Healthy` requires none. Every old-firmware test fails it.

### F9 (low): the e-paper's 10 ms supply-settle and reset pulse could be 9.x ms

**Evidence.** SSD1680 section 9 (and the GDEY0213B74 reference flow): supply
VCI, wait 10 ms; the reference code holds RES# low for 10 ms.
`board_delay_ms(n)` counted n SysTick edges from an arbitrary point inside a
millisecond, so it returned after n-1 to n ms; the emulator measured RES# low
for 9.999 ms.

**Fix.** `board_delay_ms(n)` waits for n + 1 tick edges: at least n ms.

**Regression test.** The SSD1680 model checks 10 ms from VCI to the first
command and RES# low for at least 10 ms (`PowerUpTimingViolations`, in `Device
Healthy`). Every old-firmware test fails it.

### F10 (low): a reset in the middle of an RTC read left the I2C bus stuck

**Evidence.** If the MCU resets while the RV-3032 is driving SDA low in a read
(a fault, a brown-out during a refresh), the RTC holds SDA until it gets clocks;
it only gives up after its 950 ms bus timeout (application manual 4.5).
`rv3032_init()` runs within milliseconds of reset, reads nothing, and the time
is marked invalid until the user sets the clock again.

**Fix.** `i2c_bus_clear()` before I2C1 is enabled: with the pins as open-drain
GPIOs, if SDA is low, clock SCL up to nine times until it is released, then
generate a STOP (UM10204 3.1.16). No emulator test: Renode's I2C models cannot
hold SDA low.

## What was checked, per peripheral

Verdict: **OK** = matches the source; **Fixed** = finding above, fixed;
**Board** = can only be confirmed on hardware.

### Memory map, startup, linker

| Checked | Source | Verdict |
|-|-|-|
| Flash 256 KB at 0x08000000, 2 KB pages, 128 pages; store = pages 88-127 (0x0802C000-0x0803FFFF), PNB field 7 bits | RM0503 3.3.1 table; header `FLASH_SIZE_DEFAULT`, `FLASH_CR_PNB` | OK |
| SRAM 40 KB contiguous: SRAM1 32 KB at 0x20000000 + SRAM2 8 KB at 0x20008000; stack top 0x2000A000 | RM0503 2.3 Table 5; header `SRAM1_SIZE_MAX`, `BKPSRAM2_BASE` | OK (now checked automatically) |
| SRAM parity: option `RAM_PARITY_CHECK` (0 = on); with parity on, reads of never-written RAM raise NMI + HardFault | RM0503 2.3, 3.7.7 | OK: factory default is off and nothing sets it. If it is ever enabled, the startup code must clear all SRAM first |
| Vector table: 16 system + 32 device entries, order and names | header `IRQn_Type` | OK (now checked automatically) |
| Reset_Handler: .data copy, .bss zero, init_array, no FPU/VTOR setup needed on M0+ booting from flash | Armv6-M; RM0503 2.5 | OK |
| Boot: flash aliased at 0, EMPTY flag | RM0503 2.5, 2.5.4 | Fixed (F4, procedure) |
| Option bytes / RDP: only the bootloader writes them, and only when the owner chooses Settings → Lock debug port, on a healthy cell (firmware-updates.md); RDP1 recipe | RM0503 3.4.2, 3.5.1 | OK; Fixed (F7, procedure) |
| UID at 0x1FFF6E50, flash size at 0x1FFF6EA0 | RM0503 39 | OK |

### RCC, clocks, flash latency

| Checked | Source | Verdict |
|-|-|-|
| Reset clock MSI 4 MHz; switch to HSI16 with SW = 001, wait SWS | RM0503 5.2.4, 5.4.3 | OK |
| Range 1 is the reset VOS (PWR_CR1 reset 0x0000 0208); 16 MHz in range 1 needs 0 WS (0 WS up to 24 MHz) | RM0503 4.4.1, 3.3.3 Table 9; `LL_SetFlashLatency` | OK |
| STOPWUCK = 1: wake from Stop on HSI16 | RM0503 5.4.3 | OK |
| HSI48 for the RNG: CLK48SEL = 11 selects HSI48 (RNG and USB share the field) | RM0503 5.4.21; `LL_RCC_RNG_CLKSOURCE_HSI48` | OK |
| I2C1 and USART1 kernel clocks: CCIPR reset 0 = PCLK (16 MHz) | RM0503 5.4.21 | OK |
| SysTick 16 MHz / 1000 | Armv6-M | OK |

### PWR, Stop 2, wake-up

| Checked | Source | Verdict |
|-|-|-|
| LPMS = 010 Stop 2, LPR = 0, SLEEPDEEP, WFI with PRIMASK set | RM0503 4.3.8, Table 28, 4.4.1 | OK |
| All EXTI pending bits clear at entry, else entry ignored | RM0503 4.3.8 Table 28 note | OK (EXTI ISR clears FPR1; now audited by the model) |
| Peripherals that cannot run in Stop 2 disabled | RM0503 4.3.8, 33.5 | Fixed (F3: I2C1) |
| ENULP (sampled BOR in Stop 2) | RM0503 4.2.1 (BOR) | OK (a fast supply drop between samples is not detected; acceptable for a coin cell) |
| Wake on HSI16 and clock tree re-applied | RM0503 4.3.8 | OK |
| SRAM and registers retained in Stop 2 | RM0503 4.3.8 | OK |

### GPIO, EXTI, pins

| Checked | Source | Verdict |
|-|-|-|
| Every package pin number and name in `hardware/pins.yaml` (UFQFPN32) | ST open pin data `STM32U083KCUx.xml` | OK, no pins.yaml change needed |
| AF numbers: PA5/PA7 SPI1 AF5, PA9/PA10 USART1 AF7, PB6/PB7 I2C1 AF4 | ST open pin data GPIO modes; HAL `GPIO_AF5_SPI1`, `GPIO_AF7_USART1`, `GPIO_AF4_I2C1` | OK |
| EXTICR port codes (PA = 0, PB = 1, ... PF = 5), 8 bits per line | RM0503 12.5.6 | OK |
| EXTI lines 0, 1, 2, 3, 4 (PB4), 15: no line shared by two pins; NVIC lines EXTI0_1, EXTI2_3, EXTI4_15 | RM0503 12; header IRQn | OK |
| Unused pins analog; SWD, NRST/PF2, BOOT0/PF3 left alone; PA11/PA12 remap left at reset | RM0503 7.3.4; pinout | OK |
| No floating inputs in Stop 2 (BUSY and IR_RX analog while their parts are unpowered) | RM0503 7.3 | OK (now audited by the model) |
| IR LED off (TX low) and receiver unpowered in sleep; panel lines low and supply off | code | OK |
| Joystick pull-ups conduct only while a contact is closed | Alps SKRH (common pin to GND) | OK; a jammed key: Fixed (F2) |
| Joystick contact bounce: Alps does not specify it for the SKRH; 25 ms debounce with latched edges | `core/buttons.c` | Board |

### FLASH store

| Checked | Source | Verdict |
|-|-|-|
| KEYR sequence, LOCK | RM0503 3.3.5 | OK |
| Page erase: BSY1/CFGBSY, clear errors, PER + PNB, STRT, wait CFGBSY | RM0503 3.3.6 | OK |
| Programming: double-word only, aligned, two word writes, PG; no reprogramming of a non-erased double-word; skip all-0xFF | RM0503 3.3.7 | OK (matches `core/store.c`'s 8-byte, aligned, write-once use) |
| HSI16 started automatically for program/erase | RM0503 3.3.6 note | OK |
| ECC and interrupted operations | RM0503 3.3.2, 3.3.5 | Fixed (F1) |

### I2C1 and the RV-3032-C7

| Checked | Source | Verdict |
|-|-|-|
| TIMINGR 0x30420F13 = 100 kHz at a 16 MHz kernel clock (PRESC 3, SCLDEL 4, SDADEL 2, SCLH 0x0F, SCLL 0x13) | RM0503 33.4.10 Table 194 | OK |
| Open drain, external 4.7 k pull-ups | pins.yaml | Board (electrical review) |
| I2C1 disabled in Stop 2 | RM0503 33.5 | Fixed (F3) |
| Address 0x51 (7-bit) | AM 6.6 | OK |
| Registers 00h-07h, 0Bh/0Ch timer, 0Dh status (PORF b1, VLF b0, TF b4, AF b3), 0Eh (EEF b3, EEbusy b2), 10h control 1 (TE b3, EERD b2, TD b1:0), 11h control 2 (TIE b4), 3Fh EECMD, 40h user RAM, C0h PMU (NCLKE b6), C1h offset (PORIE b7, VLIE b6) | AM 3.x register tables | OK |
| Time read in one burst 00h-07h (counters frozen during the access); writing Seconds clears the hundredths | AM 4.5, 4.5.1 | OK |
| PORF set at power-on and must be cleared before it means anything; VLF | AM 3.7 status | OK |
| Countdown timer start order TE, TIE, TF to 0, TD, value, TIE, TE 0->1 | AM 4.8.2 | OK |
| EEPROM update: EERD = 1, write RAM mirror, EECMD 11h, wait EEbusy, EERD = 0; VDD >= 1.6 V during the write | AM 4.6.3, 4.6.9, 4.6.10 | OK |
| Power-on refresh of the configuration (tPREFR 66 ms) | AM 4.6.1, 4.6.7 | Fixed (F6) |
| Bus left held by a reset mid-read | AM 4.5; UM10204 3.1.16 | Fixed (F10) |
| Temperature compensation: automatic, nothing to configure; backup switchover left disabled (BSM = 00) | AM 3.20.1 | OK (VBACKUP wiring: electrical review) |

### SPI1 and the SSD1680 / GDEY0213B74

| Checked | Source | Verdict |
|-|-|-|
| Mode 0 (CPOL = CPHA = 0), MSB first, 8-bit, write only; D/C# set before each byte group and only changed with BSY = 0 | SSD1680 6.1, 12.1 | OK |
| Clock rate and pin speed | SSD1680 Table 12-1, 11-1; RM0503 36.6.1 | Fixed (F5) |
| Power-up: VCI, 10 ms, HW reset, SW reset 0x12, 10 ms, BUSY low | SSD1680 9.1; GDEY0213B74 flow | Fixed (F9: delays were up to 1 ms short) |
| BUSY high = busy; never send while BUSY | SSD1680 pin description | OK |
| 0x01 = F9 00 00: 250 gates (MUX + 1), GD = SM = TB = 0 | SSD1680 8.1 | OK |
| 0x11 = 03, X window 0..15 bytes (122 sources padded to 128), Y 0..249 | SSD1680 8.2, 0x44/0x45 | OK |
| 0x21 = 00 80 (source output S8-S167, as in Good Display's code for this panel) | SSD1680 0x21 | OK |
| 0x3C border: 05 (GS transition, LUT1) full; 80 (VCOM) partial | SSD1680 0x3C | OK |
| 0x18 = 80 internal temperature sensor | SSD1680 0x18 | OK |
| 0x22 = F7 (load temperature, LUT mode 1, display, analog + osc off) full; FF (mode 2) partial; old image in RAM 0x26 for mode 2 | SSD1680 0x22 table | OK |
| Deep sleep 0x10 01 then supply cut; RAM lost, so a hardware reset and full init every update | SSD1680 0x10 | OK |
| Full refresh after 5 fast/partial updates | GDEY0213B74 spec, notes | OK for content changes; countdown-bar ticks are partial without limit (a core policy choice, not a hardware fault) |
| Which corner is RAM (0, 0) on this mounting | GDEY0213B74 drawing | Board (`EPD_ROTATE_180` exists for this) |

### USART1 and the IR link

| Checked | Source | Verdict |
|-|-|-|
| BRR = 16 MHz / 4800 = 3333 (0.01 % error), OVER8 = 0, 8N1 | RM0503 34.5.8 | OK |
| TXINV/RXINV written only while UE = 0; idle low = LED off | RM0503 34.8.3 | OK |
| VEMT2523SLX01 edges ~20-40 us (at RL = 100 ohm; slower at low photocurrent) against a 208 us bit sampled at its middle (3-sample majority) | Vishay 84167 Fig. 4; RM0503 34.5.7 | OK with margin; Board (range, ambient light, echo) |
| USART1 disabled before Stop 2 (`hal_ir_enable(false)`) | RM0503 34.5.22 | OK |
| RX overrun during a 22 ms page erase (CPU stalls on flash) | RM0503 3.3.5 | OK: the pairing protocol retransmits |

### RNG

| Checked | Source | Verdict |
|-|-|-|
| Kernel clock HSI48 <= 48 MHz, CLKDIV 0 | RM0503 21.3.6 (RNG clocking), 21.6.2 | OK |
| Configuration | RM0503 21.6.2 Table 115; AN4230 Table 3 | Fixed (F8) |
| Polling: SEIS/CEIS clear and DRDY before reading DR; DR != 0 | RM0503 21.3.5 | OK; tightened (F8) |
| Seed error recovery | RM0503 21.3.7 | Fixed (F8, order) |
| Failure -> reset, never weak output | code | OK |

### ADC (battery)

| Checked | Source | Verdict |
|-|-|-|
| ADVREGEN, 20 us regulator start-up, calibration with ADEN = 0 | RM0503 14.4.2, 14.4.3; `LL_ADC_DELAY_INTERNAL_REGUL_STAB_US` | OK |
| CKMODE 01 = PCLK/2; SMP 160.5 cycles = 20 us | RM0503 14.4.5, 14.4.9 | OK |
| VREFINT on channel 12, VREFEN, 12 us start-up; VREFINT_CAL at 0x1FFF6EA4 measured at 3.0 V | RM0503 14.10; `LL_ADC_CHANNEL_VREFINT`, `VREFINT_CAL_ADDR` | OK |
| ADC disabled before Stop 2 | RM0503 4.3.8 | OK (audited) |

### Not used, confirmed off

IWDG/WWDG (not started; `IWDG_SW` option default = software start), AES, LPTIM,
RTC/TAMP of the MCU (the RTC is external), DMA, USB: none is enabled, so none
can keep the device out of Stop 2 or reset it.

## The emulator, made stricter

| Model change | Catches |
|-|-|
| `U0Map`: every offset/bit/base the models use, checked against the CMSIS header by `firmware/tools/check_hw_constants.py` (in `make firmware` and the firmware CI job), together with the platform's base addresses, IRQ lines and tagged regions, the vector table and the linker memory map | a model register map that disagrees with the silicon (165 values) |
| Flash aliased at 0; MCU reset (`machine RequestReset`) boots the image again | firmware resets were not testable before |
| Flash ECC fault injection with NMI | F1 |
| PWR Stop 2 entry audit | F3; EXTI pending at entry; LPR; floating inputs |
| RNG configuration and HTCR write rules, AN4230 check | F8 |
| SSD1680 power-up timing | F9 |
| RV-3032 power-on EEPROM refresh; RTC not reset by an MCU reset | F6 |
| SPI rate and pin speed checked by a test | F5 |

All of these fail on the firmware as it was on `main` and pass now.

## Only a real board can tell

- Current in Stop 2 (expected a few uA): the emulator counts time, not current.
- SPI edges at 4 MHz into the panel FPC across the cell's voltage range.
- The panel's orientation (`EPD_ROTATE_180`), ghosting and refresh times when cold.
- IR range, ambient-light immunity, and each device hearing its own LED.
- Joystick bounce and the mapping of A/B/C/D to directions.
- RV-3032 accuracy and CLKOUT really off (a 32 kHz square wave on the pin).
- That a cell pulled during a write really leaves an ECC-bad double-word that is
  then handled (the emulator injects one; the silicon makes one when it will).
- BOR behaviour of the CR2450 under the panel's inrush.

## Errata ES0602, entry by entry

Every limitation in ES0602 Rev 3 (silicon revision A, the only one), against
what the firmware does:

| Entry | Limitation | Applies? | Action |
|-|-|-|-|
| 2.1.1 | PC13 transitions disturb the LSE | No: the LSE is unused (the RTC is the external RV-3032; PC14/PC15 are spare) | none |
| 2.1.2 | VREFINT_CAL left unprogrammed (0xFFFF) on some parts | **Yes**: the battery reading divided by it, so a blank value read every cell as full (clamped to 3.6 V), defeating low-battery warnings and any "cell healthy" gate | **Fixed**: `hal_battery_mv` falls back to the datasheet's typical 1.212 V (1654 counts) when the value is 0 or 0xFFFF |
| 2.2.1 | DMA flag clear race | No DMA used | none |
| 2.3.1 | ADC overrun flag missed | No: single conversions, polled, overrun not used | none |
| 2.4.x | DAC | Not used | none |
| 2.5.x | TIM one-pulse / compare / OC clear | Not used | none |
| 2.6.x | LPTIM stuck interrupt, flags | Not used | none |
| 2.7.1 | MCU RTC alarm flag in debug | The MCU RTC is not used | none |
| 2.8.1 | I2C sampling when tSU;DAT < one kernel clock | No: I2CCLK is HSI16 (62.5 ns), above the 4 MHz / 10 MHz minimums for standard and fast mode | none |
| 2.8.2 | Spurious BERR in master mode | **Yes**: `i2c_wait` aborted the transfer on BERR, so a spurious flag failed an RTC read | **Fixed**: BERR is cleared and the transfer continues; NACK and arbitration loss still fail |
| 2.9.1 | USART data corrupted by a glitch in the second half of the stop bit | Possible on the IR line (ambient light, own echo) | Covered: every IR frame carries a CRC and is resent until acknowledged (pairing.c); corrupted bytes cost a retry, never wrong data |
| 2.9.2-2.9.4 | Smartcard, ABREN, ONEBIT noise flag | Not used (8N1, 3-sample majority, no auto-baud) | none |
| 2.10.1 | LPUART transmitter jitter at low BRR | No: IR is on USART1 from HSI16 (ratio 3333) | none |
| 2.11.1 | SPI BSY stuck when disabled while TXE = 0 | Checked: `spi_close` waits for TXE (each byte waits for RXNE) and BSY = 0 before clearing SPE, which is the workaround | none |
| 2.11.2 | SPI slave BSY | Master only | none |
| 2.12.x | USB | Not used | none |

## Still to check

- **DS14548 I/O AC characteristics:** the exact low/medium-speed limits behind
  F5. The fix is conservative either way.
