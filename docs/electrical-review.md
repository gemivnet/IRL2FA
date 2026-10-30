# Electrical review (October 2026)

This review checks every circuit block on the board against the manufacturers'
own documents rather than our own files. It was prompted by the IR LED and the
phototransistor: both were placed facing into the board while ERC, DRC and every
test passed. The question for each block was "what would the datasheet say is
wrong here?"

**Result.** The review found **no would-not-work defect**. It found four
**degraded** issues: the IR link in bright light, the IR range with worst-case
parts, refreshes from a nearly flat or cold cell, and the booster switch's gate
threshold. It also found six **cosmetic** issues. The checks that can run
automatically are now in `hardware/tests/test_electrical.py`, with the
datasheet facts they use in `hardware/tests/electrical_ref.yaml`. The SPICE
benches gained an IR receiver bench and corrected inductor data.

Sources were fetched on 2026-10-30. Where a manufacturer's site refused a
direct download, the copy LCSC serves for the same part number was used. That
copy is the manufacturer's own PDF, and its document number and revision are
quoted.

## Sources

| Ref | Document |
|-|-|
| GD | Good Display GDEY0213B74 specification rev 1.0 (2021-03-17), [product page](https://www.good-display.com/product/391.html). p.7 mechanical drawing, p.8 pin assignment, p.9 BS1, p.29 reference circuit |
| SSD | Solomon Systech SSD1680 rev 0.14 (Jun 2019). Table 11-1/11-2 p.40-41, Table 13-1 / Fig. 13-1 p.43 application circuit |
| HRS | Hirose FH34SRJ catalogue (Nov.1.2018), [LCSC C324726 PDF](https://datasheet.lcsc.com/datasheet/pdf/4fa13630ce538d3df6cea685e5f75b4e.pdf?productCode=C324726). p.1-2 contact type, p.3 dimensions, p.4 land pattern and FPC |
| ST | ST DS14463 rev 2 (STM32U083xC), [LCSC C22459164 PDF](https://datasheet.lcsc.com/datasheet/pdf/1d8e3bb2642efa1578790415a746a5f0.pdf?productCode=C22459164). Fig. 3 p.25, Tables 13/14 p.36-38. Also DS14548 rev 2 (STM32U073, same die family, English): Fig. 13 p.42, Table 22, Table 64 p.82 |
| RV | Micro Crystal RV-3032-C7 Application Manual rev 1.3 (May 2023). 2.2 p.10, 8.1/8.2 p.140-141, EEPROM C0h NCLKE p.45 |
| AO1 | AOS AO3401A rev 3.1 (Dec 2023), [LCSC C15127 PDF](https://datasheet.lcsc.com/datasheet/pdf/fee353dd1e9e0bc90b295f14f381aa4c.pdf?productCode=C15127) p.1-2 |
| AO0 | AOS AO3400A rev 3, [LCSC C20917 PDF](https://datasheet.lcsc.com/datasheet/pdf/130876b936da464599428c58fd8de8f6.pdf?productCode=C20917) p.1-2 |
| SI | Vishay Si1308EDL doc 63399 rev C, [vishay.com](https://www.vishay.com/docs/63399/si1308edl.pdf) p.1-2 |
| APV | APV ANR series catalogue (rev 2026-08-05), [LCSC C7427088 PDF](https://datasheet.lcsc.com/datasheet/pdf/a15e54507aca7b688df779462a1a4487.pdf?productCode=C7427088) p.5 |
| MBR | JSCJ MBR0530, [LCSC C77336 PDF](https://datasheet.lcsc.com/datasheet/pdf/8d647b709d5440b4976f135071ba6493.pdf?productCode=C77336) |
| VSMB | Vishay VSMB2948SL doc 83498 rev 1.1 (Mar 2025). p.2 characteristics, p.5 package |
| VEMT | Vishay VEMT2523SLX01 doc 84167 rev 1.0, [LCSC C511451 PDF](https://datasheet.lcsc.com/datasheet/pdf/d26101f6a3931a556e377ed787fa1100.pdf?productCode=C511451). p.1-3 characteristics, p.4 package, p.5 lead table |
| VEMT2 | Vishay VEMT2023SLX01 doc 84166, [vishay.com](https://www.vishay.com/docs/84166/vemt2023slx01.pdf): the same package with a daylight filter |
| SEM | Samsung Electro-Mechanics MLCC catalogue, [LCSC C16780 PDF](https://datasheet.lcsc.com/datasheet/pdf/02336ea48ea44ca18c72517dd3cb7b47.pdf?productCode=C16780). p.4 part numbering, p.53 reliability (insulation resistance) |
| ALPS | Alps SKRHABE010 product sheet, [LCSC C139794 PDF](https://datasheet.lcsc.com/datasheet/pdf/053eeebef0364c26ade981172656431e.pdf?productCode=C139794). p.2 outline and land pattern, p.3 circuit diagram |
| TAG | Tag-Connect TC2030-CTX datasheet (6-pin TC2030 footprint pinout) and TC2030-IDC-NL footprint drawing |
| MY | MYOUNG MY-CP-0106 (MY-2450-01), [LCSC C964845 PDF](https://datasheet.lcsc.com/datasheet/pdf/26b02de8ee838f915c56d5652dea3d54.pdf?productCode=C964845) |
| LCSC | LCSC product parameters for every BOM line (`wmsc.lcsc.com/ftps/wm/product/detail?productCode=…`), 2026-10-30 |

## Block-by-block

| Block | What was checked | Source | Verdict |
|-|-|-|-|
| **E-paper FPC pinout** | All 24 J1 pins against the panel's pin table and reference circuit. NC pins 1, 4 and 19 stay open. TSCL/TSDA (6, 7) stay open. BS1 (8) is tied to GND for 4-wire SPI. VDDIO (15) and VCI (16) share VEPD. Every `C` pin (5, 18, 20-24) has its own 1 µF to GND | GD p.8, p.9 note 5-5, p.29 | OK. `test_fpc_pins_match_panel_reference_circuit` |
| **FPC fold and pin 1** | Panel drawing, front view: the FPC leaves the left edge with pin 1 at the top, 8.15 + 0.50 mm below the glass top, so y = 9.55 mm. The fold through the slot is 180° about an axis along the FPC's width, so each pin keeps its y. On the routed board, J1 pad 1 is at y = 9.55 and pad 24 at 21.05, so every pad is level with its panel pin (0.00 mm offset). J1's fitting nails (the FPC entry side) face the slot. Hirose contact No.1 is at +x in the top view (p.3/p.4), as in `footprints.yaml` | GD p.7, HRS p.3-4 | OK. `test_fpc_pin1_lands_on_connector_pin1` |
| **FPC contact face** | After a 180° fold the FPC's contact face points away from the board. The FH34**SRJ** is a **top and bottom** contact part ("RJ: Top and Bottom contact", HRS p.2 ordering code, field 3). Hirose's FPC drawing shows contact No.1 landing on the same physical position whichever face carries the contacts. The panel therefore mates whichever way its contacts face. A bottom-contact-only substitute would not mate | HRS p.1-2, p.4 | OK. `test_fpc_connector_accepts_either_contact_face` pins the dependency |
| **E-paper booster** | Topology and values against GD p.29: L1 47 µH, Q3 Si1308EDL, D1-D3 MBR0530, R6 1 MΩ (GDR pull-down), R7 2.2 Ω (RESE), C10/C11 4.7 µF, eight 1 µF panel capacitors. Diode directions: D3 SW→PREVGH, D2 NEG→GND, D1 PREVGL→NEG. Si1308EDL pins 1 G, 2 S, 3 D. Stress: Vds max 22.3 V against 30 V (Q3, D1-D3), MBR0530 Vr 30 V | GD p.29, SI p.1, SPICE `epd_boost.cir` | OK. See **EPD-1** (gate threshold) and **C-3** (C11 margin) |
| **L1 rating** | ANR3015T470M: Isat 0.36 A, Irms 0.36 A, DCR 1.625 Ω max (APV p.5). The project claimed "500 mA", copied from GD's "refer to NR3015: Io = 500 mA". Good Display's own NR3015T470M is 0.32 A Isat, so the ANR part is as good or better than the reference. The bench now uses the real DCR and checks the peak against 0.36 A (0.247 A simulated) | APV p.5 | OK. Bench corrected (**C-1**) |
| **Panel supply / load switch** | Q2 AO3401A pins 1 G, 2 S, 3 D. The source is on VBAT, so the body diode cannot feed the panel when the switch is off. R5 100 kΩ holds the gate at the source during reset. Vgs(th) is −1.3 V max, so it is fully on at a 2.2 V cell. Inrush: VBAT dips to 2.69 V at Rint 40 Ω (`load_switch.cir`). With the panel off, the firmware drives SPI/RST/DC/CS low and BUSY analog, so the panel is not back-powered (`hal_stm32u0.c:160-170`) | AO1 p.1-2 | OK |
| **Capacitor ratings** | Rated voltage decoded from each Samsung part number (SEM p.4) and checked against the highest voltage across the part (SSD Table 11-1/11-2, SPICE). Every panel and booster capacitor is ≥ 25 V, as GD p.29 asks. Dielectrics are X5R/X7R. LCSC parameters match each MPN | SEM p.4, SSD, GD p.29, LCSC | OK. `test_capacitor_voltage_ratings`. See **C-3** |
| **Bulk capacitors C1/C2** | 2 × 47 µF 6.3 V X5R at 3 V. Samsung's insulation-resistance guarantee is 10 GΩ or 500 MΩ·µF, whichever is smaller (100 MΩ·µF for the marked high-capacitance items). For 47 µF that is ≥ 2.1 MΩ, so ≤ 1.4 µA per capacitor and 2.8 µA for both, worst case at 3 V. `hardware/power/model.json` already carries 2.8 µA worst and 0.1 µA typical. Typical leakage is nA. The worst-case figure is a guarantee, not an expectation | SEM p.53 | OK as modelled. Measure at TP1 on the first boards |
| **Reverse-battery P-FET Q1** | Drain on VBAT_RAW (cell +), source on VBAT, gate on GND. The P-FET's body diode (anode = drain, cathode = source) conducts cell→load at insertion. The channel then shorts it at Vgs = −VBAT. A reversed cell gives Vgs = 0 and a reverse-biased diode. Battery_Cell pin 1 (+) = clip legs = VBAT_RAW. At 2.2 V, Vgs = −2.2 V against Vth −1.3 V max; Rds(on) is 85 mΩ max at −2.5 V, so a 40 mA refresh peak drops a few mV | AO1 p.1-2, MY | OK. `test_reverse_battery_pfet`, `reverse_battery.cir` |
| **MCU pinout** | Every pins.yaml pin name against UFQFPN32 Fig. 3 of the U083 datasheet: PF2-NRST on 4, VDDA/VREF+ on 5, VDD(USB) on 17, PF3-BOOT0 on 31, EP to GND | ST Fig. 3 p.25 | OK. `test_pins_yaml_pinout_matches_datasheet` |
| **MCU alternate functions** | PA5 AF5 SPI1_SCK, PA7 AF5 SPI1_MOSI, PA9 AF7 USART1_TX, PA10 AF7 USART1_RX, PB6 AF4 I2C1_SCL, PB7 AF4 I2C1_SDA, PA13/PA14 AF0 SWDIO/SWCLK. Checked in the U083 (Chinese) and U073 (English) tables, which are identical. Each pair shares one peripheral instance | ST Tables 13/14 p.36-38 | OK. `test_alternate_functions_exist_on_their_pins`, `test_peripheral_signals_share_one_instance` |
| **EXTI / wake** | Wake sources PA0, PA1, PA2, PA3, PA15 and PB4 are on EXTI lines 0, 1, 2, 3, 15 and 4, with no shared line. EPD_BUSY (PB1, line 1) is polled, not on EXTI. IR_RX (PA10) is not a wake source. GPIO EXTI lines wake from Stop 2 | ST, pins.yaml, `hal_stm32u0.c` | OK. `test_exti_lines_do_not_collide` |
| **MCU power / reset / boot** | VDD (1, 17) and VDDA (5) on VBAT, VSS (16, 32, EP) on GND. VBAT carries 2 × 100 nF, 4.7 µF, 1 µF and 10 nF, as Fig. 13 asks (n × 100 nF + 4.7 µF on VDD; 10 nF + 1 µF on VDDA). NRST has 100 nF. BOOT0 (PF3) has a 10 kΩ pull-down. PC14/15 are unused: the backup-domain pins must not source current. The IR_RX_PWR load is ≤ 0.33 mA (VDD/10 kΩ) | ST Fig. 13 (DS14548 p.42) | OK. `test_mcu_supply_and_decoupling`, `test_lse_pins_not_used_as_outputs` |
| **SWD (J3)** | TC2030 pinout: 1 VCC (VBAT), 2 SWDIO, 3 nRESET, 4 SWCLK, 5 GND, 6 SWO (NC). The stock footprint is placed on B.Cu and mirrored, which is right because the cable is applied from the back | TAG | OK. `test_symbol_pins_match_datasheet` |
| **RTC RV-3032-C7** | Pins 1 VBACKUP, 2 SDA, 3 INT, 4 EVI, 5 VSS, 6 VDD, 7 CLKOUT, 8 SCL. VBACKUP is unused and goes to VSS through 10 kΩ, as the manual requires. EVI is tied to GND ("must not be left floating"). CLKOUT is left open, which the manual allows for a push-pull output. 100 nF on VDD. I2C pull-ups go to the always-on VBAT, so the bus idles high in Stop 2. INT is open drain with the MCU pull-up, retained in Stop 2. CLKOUT ships enabled at 32.768 kHz; the firmware sets NCLKE (`rv3032.c:140-143`) | RV p.10, p.140-141 | OK. `test_rtc_unused_pins`. See **C-6** |
| **Joystick SKRHABE010** | 1 A, 2 Center, 3 C, 4 B, 5 Common, 6 D. Common to GND, the other five to EXTI pins with internal pull-ups (25-55 kΩ, ST Table 64). Contacts are rated 10 µA/1 V minimum, which a 3 V pull-up covers | ALPS p.3, ST Table 64 | OK. See **C-5** (frame tabs) |
| **IR emitter** | VSMB2948SL: Lead I = cathode, which is footprint pad 1 and the LED's K. From the drawing, viewed into the lens the cathode is on the left. On the back side at 180° that puts K at x = 54.85 and A at x = 58.15, the pads that carry IR_LED_K and IR_LED_A. AO3400A pins 1 G, 2 S, 3 D, low side, with R9 holding the gate low in reset. IF is 12.5-19 mA (`ir_led.cir`) against 100 mA abs max. R8 dissipates 36 mW against 62.5 mW. The LED's 15 ns rise is irrelevant at 4800 baud | VSMB p.2, p.5; AO0 | OK. See **IR-2** (range) |
| **IR detector** | VEMT2523SLX01: Lead I = collector = pad 1 = IR_RX_PWR; emitter = pad 2 = IR_RX, with R10 10 kΩ to GND. Emitter follower with the collector at a fixed voltage, so no Miller slow-down. Dark current 100 nA max gives 1 mV. tr/tf 20-35 µs (Fig. 4) against a 208 µs bit: the new `ir_rx.cir` reaches 2.06 V / 0.006 V at the bit centre for worst-case parts 2 cm apart (VIH 1.75 V / VIL 0.75 V at 2.5 V) | VEMT p.2-5, ST Table 64 | Timing and polarity OK. See **IR-1** and **IR-2** |
| **Battery and brown-out** | CR2450 fresh ≤ 3.3 V is within MCU 3.6 V, SSD1680 3.7 V and RV-3032 5.5 V maxima. SSD1680 VCI min is 2.2 V. With a 40 Ω (half-used) cell and 40 mA panel peaks, VBAT dips to 2.22 V (`battery_pulse.cir`) | SSD Table 11-1 | Marginal. See **PWR-1** |
| **Every two- and three-terminal part** | BOM values, ratings and packages against LCSC's parameters for every line. Thick-film 0402 resistors are 62.5 mW. R7 sees about 30 mW during booster start-up and R8 36 mW while transmitting. The SKRH, TC2030, RV-3032, Battery_Cell, LED, D_Schottky, Q_Photo_NPN and all FET symbols have the pin names of their datasheets | LCSC, each datasheet | OK. `test_symbol_pins_match_datasheet` |

### What `test_footprints.py` covers, and what is now added

`test_footprints.py` checks pad positions, sizes and pin-1 corners against
the manufacturers' land patterns. It also checks that each footprint pad has a
symbol pin of the same number.

It did **not** check that a symbol pin's *name* or function matches the
datasheet. Footprint pad 3 of Q1 is the drain only because the KiCad AO3401A
symbol says so. It also did not check that the pins sit where the *mating part*
(the panel's FPC) puts its pins. Both are now covered:

- `test_symbol_pins_match_datasheet` checks pin names.
- `test_fpc_pin1_lands_on_connector_pin1` checks the FPC position.

Neither test would have caught the IR orientation bug by itself;
`test_orientation.py` does that.

## Findings

### Would not work

None found.

The FPC, every polarised part, every MOSFET's terminals and body diode, every
MCU alternate function and the EXTI wake lines all match their primary sources.

### Degraded

**IR-1: the IR detector has no daylight filter, so ambient light can hold the receiver "on".**

- **Evidence.**
  - VEMT2523SLX01 has a "clear epoxy dome lens… sensitive to visible and near infrared radiation", 470-1090 nm (VEMT p.1).
  - The "0" level must stay below VIL = 0.3 × VDD, which is 0.75 V at 2.5 V. With R10 = 10 kΩ, that allows at most 75 µA of photocurrent from ambient light (`ir_rx.cir`, RESULT `iamb_max`).
  - At the typical 2.7 mA per mW/cm² (VEMT p.2), 75 µA is about 0.03 mW/cm² of 950 nm-equivalent light on the lens.
  - Estimate (not from a datasheet): 500 lx of white LED light is about 0.15 mW/cm². At a relative sensitivity of 0.3-0.5 in the visible, that is already 0.05-0.08 mW/cm² equivalent. Daylight and halogen light are orders of magnitude more.
- **Effect.** When light falls on the lens, the USART sees a permanent start bit or break, and pairing fails. Shading the edge with a hand usually recovers it.
- **Fix (choose one).**
  1. Replace Q5 with **Vishay VEMT2023SLX01** (VEMT2). It is the same side-view package and the same lead assignment: the VEMT p.5 tape table lists VEMT2023SLX01 next to VEMT2523SLX01, both Lead I = collector. It adds a daylight-blocking filter matched to 830-950 nm emitters. It is a BOM-only change (design.py Q5 `value`/`mpn`/`lcsc`); footprint, symbol and routing stay. Its LCSC code and stock are **unverified**. RS lists it as discontinued; Farnell/element14 stock it.
  2. Keep the part and make the case's IR window from IR-pass (visible-black) material.
- `test_ir_detector_has_daylight_filter` is xfail until option 1 is applied.
- **Status: fixed** in [`2b8301f`](https://github.com/gemivnet/IRL2FA/commit/2b8301f) with option 1. Q5 is the VEMT2023SLX01, [LCSC C2843576](https://www.lcsc.com/product-detail/C2843576.html) (on 2026-10-30: LCSC 471, JLCPCB 505 with 455 available, Extended). Its datasheet (doc 84166 rev 1.0) has the same drawing 6.544-5410.01-4 ("VEMT2x23SLX01") and Lead I = collector, so the pads stay (footprint renamed `PhotoTransistor_Vishay_VEMT2023SLX01_SideView`). Ica is the same 1.3 / 2.7 / 4.1 mA at 950 nm and ICEO 100 nA max; the spectral range is 790-970 nm (Fig. 5). JLCPCB's EasyEDA footprint for it is drawn turned 180° from the VEMT2523SLX01's, so `fab.JLC` corrects it by 180, checked by `test_assembly.py`. `ir_rx.cir` uses its Fig. 4 edges. The xfail is gone.

**IR-2: with worst-case parts the IR link reaches about 3 cm, not 5.**

- **Evidence.**
  - At a flat cell the LED carries 12.5 mA (`ir_led.cir`).
  - VSMB2948SL Ie is ≥ 10 mW/sr at 100 mA (VSMB p.2), so about 1.25 mW/sr at 12.5 mA.
  - VEMT2523SLX01 Ica is ≥ 1.3 mA per mW/cm² (VEMT p.2).
  - On axis, 2 cm gives 0.41 mA, and the bench reaches 2.06 V at the sample point.
  - 3 cm gives 0.18 mA and 1.79 V, against VIH 1.75 V: the edge of the range.
  - pcb-design.md quoted 2-5 cm (corrected on this branch).
- **Effect.** Pairing works held edge to edge (as designed) but not reliably at 4-5 cm with low-end parts.
- **Fix.**
  - The documented range is corrected to about 3 cm worst case (pcb-design.md, done).
  - If more range is wanted, set R8 = 68 Ω. That gives about 28 mA fresh and 18 mA flat, √1.45 ≈ +20 % range, and 53 mW in R8, inside the 0402's 62.5 mW. Raise `ir_led.cir`'s `iled_fresh` limit to 0.035 A with it.
- No test was added: the range is a requirement call, not a datasheet violation.
- **Status: applied** in [`2b8301f`](https://github.com/gemivnet/IRL2FA/commit/2b8301f). R8 is 68 Ω, UNI-ROYAL 0402WGF680JTCE, [LCSC C25131](https://www.lcsc.com/product-detail/C25131.html) (LCSC 187 k, Extended): JLCPCB has no Basic 68 Ω 0402, so this adds an Extended part (75 Ω C25133 is "Preferred", for ~13 % instead of ~20 % more range). `ir_led.cir`: 28.0 mA at 3.2 V, 18.1 mA at 2.5 V; R8 dissipates 53 mW at 3.2 V and 59 mW at 3.3 V against 62.5 mW, only while transmitting; the LED's limit is 100 mA. `checks.yaml` allows 0.035 A. `ir_rx.cir` with 18.1 mA now checks 3 cm (2.02 V against VIH 1.75 V); 3.5 cm still reads (1.86 V) and 4 cm does not (1.45 V): about 3.5 cm worst case. The new `ir_burst.cir` checks the cell: a 90 % duty burst from a half-used cell (40 Ω) keeps VBAT ≥ 2.34 V and the LED at 15.9 mA at the end of a 9-bit run. The power budget uses 25 / 35 mA.

**PWR-1: refreshing from a weak or cold cell can brown out the panel.**

- **Evidence.** `battery_pulse.cir` gives 2.22 V minimum at Rint 40 Ω against the SSD1680's VCI minimum of 2.2 V (SSD Table 11-1). CR2450 internal resistance rises further with depth of discharge and cold.
- **Effect.**
  - The SSD1680 can reset mid-refresh: a half-drawn screen, or BUSY never clearing.
  - The MCU (1.71 V minimum) keeps running, so nothing reports the problem.
  - The firmware reads VBAT through VREFINT but only displays it (`core/app.c:230-231, 802`).
- **Fix (firmware).**
  - Measure VBAT during the first refresh phase (or right after the load switch turns on) and skip or warn below about 2.35 V.
  - Put a timeout on BUSY.
  - No hardware change: the 2 × 47 µF are already sized for the 40 mA peak.

**EPD-1: Q3's gate threshold is at the top of what the controller vendor allows.**

- **Evidence.**
  - Si1308EDL Vgs(th) is 0.6-1.5 V (SI p.2). Good Display's reference allows ≤ 1.5 V (GD p.29).
  - Solomon's own application circuit asks for "Vgs(th) = 0.9 V (typ), 1.3 V (max)" and suggests Si1304BDL or NX3008NBK (SSD Table 13-1).
  - The 2.2 Ω RESE resistor raises the source by about 0.5-0.8 V at 0.25-0.36 A peak current.
  - GDR's high level is not specified in the SSD1680 datasheet. If it follows VCI, a 2.3 V cell leaves about 1.5-1.8 V of Vgs at peak current: at the threshold of a max-Vth part.
- **Effect.** At end of life or in the cold, the booster may not reach VGH = 20 V, and the panel then refreshes faintly or not at all. At 3 V it works; this is Good Display's own reference part.
- **Fix.**
  - First, on a first board, run a full refresh from a bench supply at 2.3 V and confirm PREVGH ≥ 20 V.
  - If it fails, fit **Nexperia NX3008NBK,215** (LCSC C179399, 30 V, Vgs(th) 1.1 V, Solomon's suggested part). It is SOT-23, so Q3's footprint changes from SOT-323_SC-70 to SOT-23 with the same 1 G / 2 S / 3 D pinout, and it needs a re-route.
- **Status: fixed** in [`2b8301f`](https://github.com/gemivnet/IRL2FA/commit/2b8301f), without waiting for the bench test. Q3 is the NX3008NBK,215 (C179399; on 2026-10-30 LCSC 3490, JLCPCB 3493 with 3301 available, Extended). Nexperia data sheet (5 Nov 2022): Vgs(th) 0.6-1.1 V; VDS 30 V against the booster's 21.7 V; ID 400 mA (IDM 1.6 A) against a 0.19 A peak; Rds(on) 2.1 Ω max at 2.5 V; VGS ±8 V. Pinning 1 G, 2 S, 3 D (Table 2) on the stock KiCad SOT-23 at the old Q3 spot (symbol `Transistor_FET:Q_NMOS_GSD`); JLCPCB's EasyEDA SOT-23 takes the usual 180 correction. `epd_boost.cir` models it as a MOSFET at worst-case threshold and on-resistance, with RESE raising its source: +20.8 / −21.2 V, 20 V in 24.6 ms, 0.19 A peak. The 2.3 V refresh stays a first-board check (`pcb-design.md` item 7): with the bench's fixed on-time even an ideal switch falls short at 2.3 V, so it cannot stand in for the controller's current-mode regulation.

### Cosmetic

**C-1: L1's rating is wrong in the design data.**

- design.py says "47 uH 500 mA booster inductor".
- `checks.yaml` limited the peak to 0.45 A "L1 rated 500 mA".
- The bench modelled 0.6 Ω DCR.
- ANR3015T470M is Isat 0.36 A, Irms 0.36 A and 1.625 Ω max (APV p.5).
- **Fixed on this branch** in `epd_boost.cir` and `checks.yaml`: the simulated peak is 0.247 A, and the rails still reach +21.7 / −22.2 V in 21 ms.
- **To apply:** change the design.py description to "47 uH 0.36 A booster inductor". This regenerates the schematic description field.
- **Status: fixed** in [`2b8301f`](https://github.com/gemivnet/IRL2FA/commit/2b8301f).

**C-2: two datasheet links point at the wrong documents.**

- design.py Q5 links `vishay.com/docs/81596/vemt2500x01.pdf`, which is the VEMT2500X01 series, a different part. **To apply:** use `https://www.vishay.com/docs/84167/vemt2523slx01.pdf`.
- D4 links doc 84176, but the VSMB2948SL datasheet is doc 83498 (`https://www.vishay.com/docs/83498/vsmb2948sl.pdf`, as footprints.yaml already cites).
- **Status: fixed** in [`2b8301f`](https://github.com/gemivnet/IRL2FA/commit/2b8301f). D4 links doc 83498; Q5, now the VEMT2023SLX01 (IR-1), links its doc 84166.

**C-3: C11 (the booster's flying capacitor) runs near its rating.**

- It sees up to about 23.2 V (SW at PREVGH + Vf while D2 clamps NEG at −0.4 V) on a 25 V X5R 0805.
- That is 93 % of rating, and X5R loses most of its capacitance at that bias.
- Good Display's reference uses the same 4.7 µF/25 V, and the bench passes with 3 µF effective, so it works.
- **Optional:** a 50 V X7R 0805 of ≥ 2.2 µF gives margin. Pick one with an LCSC code at re-route time.
- **Status: not applied.** It is optional: the part is inside its rating and the bench passes with 3 µF effective. Whether a 2.2 µF 50 V 0805 keeps more than that at 23 V needs the DC-bias curves this review could not fetch, so a blind swap could lower the effective capacitance of Good Display's reference value. Left for measurement on the first boards.

**C-4: VDDA/VREF+ decoupling.**

- Pin 5 is VDDA and VREF+ combined. Fig. 13 asks for 10 nF + 1 µF on VDDA and 100 nF + 1 µF on VREF+.
- C6 (1 µF) and C7 (10 nF) sit at pin 5, and C3 (100 nF) is 2 mm away on the same net.
- VREFBUF is unused and the ADC only reads VREFINT, so no change is needed.

**C-5: the joystick's metal frame tabs (SW1 MP) float.**

- **To apply:** tie them to GND. This gives finger ESD on the frame a path that avoids the contact pins. It is a schematic change: connect SW1 pin MP to GND.
- **Status: fixed** in [`2b8301f`](https://github.com/gemivnet/IRL2FA/commit/2b8301f).

**C-6: RTC EVI is tied straight to GND.**

- The manual's example uses 10 kΩ to VSS "to keep functional test possible" and notes that EVF is set at power-on (RV p.140, note 6).
- It works as built, but the firmware should ignore EVF.
- **Optional:** fit a 10 kΩ from EVI to GND instead of the direct tie.
- **Status: not applied.** Optional, and only for Micro Crystal's factory test; the manual allows the direct tie.

## Required design changes (for the re-route)

| ID | Change | Files |
|-|-|-|
| IR-1 | Q5 → VEMT2023SLX01 (BOM only; first confirm stock and LCSC code), **or** an IR-pass window in the case | design.py Q5, BOM, `electrical_ref.yaml` already lists it; remove the xfail |
| EPD-1 | Only if a 2.3 V bench refresh fails: Q3 → NX3008NBK,215 (C179399), SOT-23 footprint | design.py Q3, PCB |
| C-1, C-2 | Description and datasheet URL corrections | design.py |
| C-5 | SW1 MP → GND | schematic |
| IR-2 | Documentation: worst-case range 2-3 cm; optionally R8 = 68 Ω | docs, design.py R8, `ir_led.cir` limit |
| PWR-1 | Firmware: gate refreshes on VBAT measured under load; BUSY timeout | firmware (outside this review's scope) |

## Could not be verified

- **Samsung DC-bias curves.** The bias derating of C1/C2, C11 and C12-C19 comes from the general X5R behaviour, not from the part's curve: Samsung's characteristic-data site was not fetched. The benches assume 25 µF of 47 µF at 3 V, 3 µF of 4.7 µF on the flying capacitor and 0.7 µF of 1 µF on the rails.
- **ST's STM32U0 hardware-development application note.** The st.com download timed out. Decoupling was checked against the datasheet's own power-supply figure instead (DS14548 Fig. 13).
- **SSD1680 GDR drive level and current-sense threshold.** Neither is published, so EPD-1 is a risk to measure, not a proven failure.
- **Which face of the GDEY0213B74 FPC carries the contacts.** The drawing does not state it unambiguously. It does not matter with the dual-contact FH34SRJ (see above), but it would matter with any other connector.
- **Physical direction of the joystick's A/B/C/D contacts.** The Alps drawing names the contacts but does not tie each letter to a direction of travel unambiguously. That is still a first-board check (pcb-design.md item 4), fixed in pins.yaml if wrong.
- **VEMT2023SLX01 at LCSC.** Its LCSC code and stock were not found: LCSC's search API refused the request. (Since found: C2843576, see IR-1.)
- **Ambient-light figures in IR-1.** These are engineering estimates. The datasheet gives sensitivity only at 950 nm.
