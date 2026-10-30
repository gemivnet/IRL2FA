# Hardware options

This page records why each part was picked and what the alternatives were.
Prices and stock are LCSC/JLCPCB figures from **2026-10-29**. Stock moves:
check the [BOM](bill-of-materials.md) links before ordering. Anything not
confirmed against a primary source is marked *unverified*.

## Summary of changes from the 2025 design

| Block | 2025 design | Now | Why |
|-|-|-|-|
| MCU | ATtiny1616 (16 KB flash, 2 KB RAM) | **STM32U083KCU6** | 2 KB of RAM can't hold a 250×122 framebuffer (3.9 KB). Secrets would have sat in plain text on an external EEPROM. |
| Storage | M95P08 SPI EEPROM | internal flash (256 KB) | 512 TOTP entries + 64 contacts fit in 72 KB; flash readout protection covers them |
| RTC | RV-3028-C7 | **RV-3032-C7** | RV-3028 has 0 stock at LCSC; the RV-3032 is temperature compensated (±2.5 ppm from −40 to 85 °C) |
| Display | PDi E2213KS0E1 (212×104) | **Good Display GDEY0213B74** (250×122) | Open SSD1680 controller, 0.42 s partial refresh, 1 µA deep sleep. The PDi booster MOSFET has 0 stock. |
| FPC connector | Molex 503480-2400 | **Hirose FH34SRJ-24S-0.5SH(50)** | Molex 0 stock; Hirose is dual-contact with ~10k in stock |
| Joystick | TE 2434804-1 | **Alps SKRHABE010** | TE 0 stock (18-21 week lead time) and no published pinout |
| Battery holder | Renata NL5077-LF (through-hole) | **MYOUNG MY-2450-01** (SMT clip, 5.7 mm) | JLC can place it. 1.8 mm lower than the BS-24-B4AA016 moulded holder used at first, and 9× its stock |
| Pairing | typed 20-symbol code (2^46) | **IR link + X25519** | see [pairing-protocol.md](pairing-protocol.md) |

## MCU

| Part | Flash / RAM | Sleep | Notes |
|-|-|-|-|
| **STM32U083KCU6** ✔ | 256 KB / 40 KB | Stop 2 ~0.7-1 µA, Standby 68 nA @ 3 V | TRNG, **AES**, flash readout protection, USART with TX/RX inversion. [LCSC C22459164](https://www.lcsc.com/product-detail/C22459164.html) ≈ $4.71 @100. [Datasheet DS14548 family](https://www.st.com/resource/en/datasheet/stm32u083kc.pdf) |
| STM32U073KCU6 | 256 KB / 40 KB | same | Pin-for-pin identical in KiCad's ST-generated symbols, but no AES and more expensive ([C22445363](https://www.lcsc.com/product-detail/C22445363.html) ≈ $6.17) |
| STM32L072KZ | 192 KB / 20 KB | 0.86 µA Stop+RTC | Older; 0 stock |
| STM32L031K6 | 32 KB / 8 KB | 0.68 µA | Needs external flash |
| ATtiny1616 | 16 KB / 2 KB | 0.1 µA | Far too small |
| MSPM0L1306 | 64 KB / 4 KB | 1 µA | No TRNG, no RTC |

The STM32U083 is the only part in this list that holds every secret
internally, has a hardware RNG, and sleeps at nanoamp levels. Stock at LCSC is
thin (tens of pieces), so the U073 is a drop-in fallback. DigiKey and Mouser
carry both.

## Display (2.13", wide, not tall)

| | **GDEY0213B74** ✔ | GDEM0213B74WT | PDi E2213KS0E1 |
|-|-|-|-|
| Resolution | 250×122 | 250×122 | 212×104 |
| Controller | SSD1680 (open datasheet) | SSD1680 | undisclosed |
| Supply | 2.2-3.7 V | 2.2-3.7 V | 2.3-3.6 V |
| Full / partial refresh | 3 s / 0.42 s | 2 s / partial | 2.4 s / none |
| Average / peak current | 3.5 mA / not stated | 4.2 mA / 40-50 mA | 2.3 mA / 21 mA |
| Temperature | 0 to 50 °C | **−25 to 50 °C** | −15 to 60 °C |
| Price | $4.95 ([Good Display](https://www.good-display.com/product/391.html)) | unverified | ~$5 |

The GDEM0213B74WT fits the same footprint and booster. Use it if the device
will live on a keychain in a cold climate. All three use the same 24-pin
0.5 mm FPC pinout (pins 5-7, 19-20 and 22 differ in name) and the same
booster topology, so the board can take a PDi panel by changing two booster
values (0.47 Ω, 10 µH) and the BUSY polarity in firmware.

## RTC

| Part | Current | Accuracy | Stock |
|-|-|-|-|
| **RV-3032-C7** ✔ | 160 nA | ±2.5 ppm from −40 to 85 °C (≈ ±79 s/year) | [C5127802](https://www.lcsc.com/product-detail/C5127802.html), 533 |
| RV-3028-C7 | 45 nA | ±1 ppm at 25 °C only (~−8 ppm at 10 °C) | 0 at LCSC |
| STM32 internal RTC + 32 kHz crystal | +~200 nA | ±20 ppm (≈ 10 min/year) | crystal is a Basic part |
| PCF85063A + crystal | 220 nA | crystal-limited | plenty |

A 30-second TOTP step tolerates about 30 s of clock error. The RV-3032 gets
there in about 139 days in the worst case and much later at room
temperature, so set the clock a few times a year.

## Battery

Requirement: at least 1 year, and as small as possible. The footprint is set
by the 59×29 mm glass, not by the cell. See [power-calculations.md](power-calculations.md).

| Cell | Capacity | Device thickness | Life (normal use, typical / worst) |
|-|-|-|-|
| CR2032 | 225 mAh | ~9.2 mm | ~1.2 / 0.7 years (worst case misses the 1-year goal) |
| **CR2450** ✔ | 540 mAh usable | **11.0 mm** (case model) | **2.8 / 1.6 years** |
| AAA + boost converter | ~1100 mAh | ~15-16 mm | longer, but thicker and needs a boost converter. Alkaline cells leak. |

### Battery holder

The holder sets the device's thickness. The case is the holder height plus
5.3 mm: 2.8 mm front stack, 0.8 mm board, 0.1 mm keep-out margin, 0.4 mm
clearance and 1.2 mm floor. The table compares the CR2450 holders a JLCPCB parts
search for "CR2450" returns, from their MYOUNG drawings. Stock and prices are
from LCSC on 2026-10-29. The clips are the lowest.

| Holder | Height | Type | LCSC | Stock | Price (50+) | Notes |
|-|-|-|-|-|-|-|
| **MYOUNG MY-2450-01** ✔ ("CR2450-W") | **5.70 mm** | sprung plate; the PCB pad is the − contact | [C964845](https://www.lcsc.com/product-detail/C964845.html) | 5355 | $0.17 | Ni-plated phosphor bronze, 36.8 mm over the feet, 23.2 mm deep. The cell slides in from the open end. [Drawing MY-CP-0106](https://datasheet.lcsc.com/datasheet/pdf/26b02de8ee838f915c56d5652dea3d54.pdf?productCode=C964845) |
| MYOUNG MY-2450-05-J | 5.70 mm | same clip, 30.5 mm legs | [C7525439](https://www.lcsc.com/product-detail/C7525439.html) | 885 | $0.11 (5+) | 5 mm shorter over the feet, but a sixth of the stock |
| MYOUNG MY-2450-03 ("CR2450-P") | 5.75 mm | shallow clip, 15.4 mm deep | [C2988622](https://www.lcsc.com/product-detail/C2988622.html) | 2965 | $0.29 (30+) | Its plate covers only 15.4 mm of the 24.5 mm cell |
| MYOUNG BS-24-B4AA016 (first design) | 7.50 mm | moulded base, lips and side spring | [C964809](https://www.lcsc.com/product-detail/C964809.html) | 586 | $0.35 (100+) | Gold contacts, but 1.8 mm taller (13.0 mm case) |
| MYOUNG BS-24-B4AK014 / B5AM007 / A1AJ004 | 7.6 / 9.85 / 9.2 mm | moulded (A1AJ004 is through-hole) | [C964813](https://www.lcsc.com/product-detail/C964813.html), [C964811](https://www.lcsc.com/product-detail/C964811.html), [C964806](https://www.lcsc.com/product-detail/C964806.html) | 1247 / 255 / 4080 | $0.29-0.42 | All taller than the first holder |

A clip cannot get much lower: the cell is 5.0 mm, the plate 0.25 mm, and its
tongues need travel. With the clip, the cell's negative face lies on a bare
12 mm pad, so the board is ordered in **ENIG** (gold) for a lasting contact.

**Rejected: sinking the cell into a board cutout.** A 24.5 mm hole in the 31 mm
board would leave 3.25 mm strips above and below it for every track between the
display half and the MCU half. The front of the board is covered by the display
glass on 0.2 mm adhesive, so the cell cannot come through to the front. The most
it could save is the 0.8 mm board, less a contact strip across the hole. Both
contacts would then have to be side or bridge springs, such as the MY-ZJ-126
side contact ([C5457113](https://www.lcsc.com/product-detail/C5457113.html), 5.0 mm high),
and LCSC has no bridge part. The 0.8 mm board already takes 0.2 mm off without
any of that.

## Joystick

| Option | Notes |
|-|-|
| **Alps SKRHABE010** ✔ ([C139794](https://www.lcsc.com/product-detail/C139794.html), 6.8k stock, $0.50 @100) | 4 directions + push, 200k cycles, guide bosses. Pins 1=A 2=Centre 3=C 4=B 5=Common 6=D. The directions sit at 45° to the body, so the footprint is rotated 45° on the board. |
| HRO K1-5202UA-01 ([C136710](https://www.lcsc.com/product-detail/C136710.html)) | Same pad pattern at about 45 % of the price; second source |
| TE 2434804-1 | 5-way but 0 stock and no pinout published |
| 3 tact switches / rotary encoder | Rejected: the requirement is a single 5-position control |

## IR pairing link

| Option | Cost | Notes |
|-|-|-|
| **VSMB2948SL + VEMT2023SLX01** ✔ (side-looking pair) | ~$0.95 | 18-28 mA LED drive (R8 = 68 Ω), 4.8 kbaud NRZ UART. The phototransistor (20-45 µs edges) is the speed limit. It is wired as an emitter follower, which avoids the Miller slow-down. The VEMT2023SLX01 ([C2843576](https://www.lcsc.com/product-detail/C2843576.html), Extended) is the daylight-filter version of the VEMT2523SLX01 (same package, pads and lead order): with the clear-lens part, room light could hold the receiver "on" ([electrical review](electrical-review.md) IR-1). Worst-case range on axis about 3.5 cm (`hardware/spice/ir_rx.cir`). Emitter stock is low (176): the Everlight IR12-21C is a fallback with less range. |
| Vishay TFBS4711 IrDA transceiver | ~$4.80 | 115 kbit/s, 10 nA shutdown, but low stock and costly |
| 38 kHz receiver (TSOP75238) | ~$1.35 | Only ~1-2 kbit/s and needs ≥ 2.5 V |

## Passives worth calling out

| Part | Choice | Why |
|-|-|-|
| Bulk capacitor | 2 × 47 µF X5R 0805 ([C16780](https://www.lcsc.com/product-detail/C16780.html)) | MLCC leakage is in the nA range. Tantalum/polymer parts are specified up to 6-63 µA, which would dominate sleep current. |
| Reverse-battery and panel switch P-FETs | AO3401A ([C15127](https://www.lcsc.com/product-detail/C15127.html), Basic) | IGSS 100 nA max. The DMP2035U's 10 µA gate leakage would drain the cell. |
| IR LED switch | AO3400A ([C20917](https://www.lcsc.com/product-detail/C20917.html), Basic) | Vth ≤ 1.45 V still turns on fully from a 2.5 V cell. A 2N7002 (Vth up to 2.5 V) may not. |
| Booster | 47 µH ANR3015 (0.36 A Isat), NX3008NBK ([C179399](https://www.lcsc.com/product-detail/C179399.html)), 3 × MBR0530, 2.2 Ω, 1 MΩ, 1 µF/50 V 0603 | Good Display reference circuit. The 50 V 0603 parts keep their capacitance at ±20 V bias. The switch is Solomon's suggestion (SSD1680 Table 13-1), not Good Display's Si1308EDL: its Vgs(th) is 1.1 V max against the controller's 1.3 V limit, where the Si1308EDL's is 1.5 V ([electrical review](electrical-review.md) EPD-1). 30 V, 400 mA, 2.1 Ω max at 2.5 V; the booster's 0.19 A peak and 21.7 V are inside it. |
| IR LED resistor | R8 = 68 Ω 0402 ([C25131](https://www.lcsc.com/product-detail/C25131.html), Extended) | 28 mA from a fresh cell, 18 mA from a flat one: about 20 % more range than 100 Ω (IR-2), with 53-59 mW in R8 against its 62.5 mW, only while transmitting. JLCPCB has no Basic 68 Ω 0402; 75 Ω ([C25133](https://www.lcsc.com/product-detail/C25133.html), "Preferred") would save the feeder fee for about 13 % instead of 20 % more range. |

The full research notes behind this page, with a source for every figure, are
in [research/parts-research-2026-10-29.md](research/parts-research-2026-10-29.md).
The [BOM](bill-of-materials.md) has the final part list.
