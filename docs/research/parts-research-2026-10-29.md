# IRL2FA hardware research (2026-10-29)

> Raw research notes kept for traceability. Some recommendations here were
> superseded; [hardware-options.md](../hardware-options.md) and the
> [BOM](../bill-of-materials.md) are the current decisions.

Compiled from four parallel research passes. Each claim carries a source URL in the section it comes from. Anything not confirmed against a primary source is marked UNVERIFIED. Stock and prices are live JLCPCB/LCSC data from 2026-10-29, in USD. Raw section files sit next to this file (part-*.md). Downloaded datasheets are in (local download folder), ../dl-mcu, ../dl-alt, ../dl-navir and ../dl-passives.

## Decision-relevant findings (read first)

| Area | Finding | Recommendation |
|---|---|---|
| MCU | STM32U073KCU6 exists: 256 KB flash, 40 KB RAM, UFQFPN32 5x5 mm, 0.5 mm pitch, 1.71-3.6 V, TRNG, no AES. Datasheet is DS14548 (DS14581 is the U031 datasheet). LCSC C22445363, about $6.17 at qty 100, 16 in stock, Extended. | **STM32U083KCU6** (LCSC C22459164, about $4.71 at qty 100, 41 in stock). Same package and memory, adds hardware AES. Check its pin table before swapping. Stock is thin for both parts. |
| MCU wake | The 32-pin package has only 3 WKUP pins (PA0, PA1, PA2). | Diode-OR or matrix the switch onto one WKUP pin, or use Stop 2 (about 1 uA with RTC) and EXTI. |
| MCU pins | Pin 17 is drawn as VDDUSB but listed as VDD in the table. | Tie it to VDD with its own 100 nF. |
| RTC | RV-3028-C7 TA-QC (C3019759 and C3304278) has **0 stock** at LCSC/JLC. It is 45 nA but ±1 ppm only at 25 °C, with no temperature compensation. | **RV-3032-C7** (C5127802, 533 in stock, about $2.32 at qty 100, 160 nA, ±2.5 ppm from -40 to 85 °C). Alternatively, buy the RV-3028 from Mouser and consign it. Pin compatibility between RV-3028 and RV-3032 is UNVERIFIED; check before reusing the footprint. |
| Display | E2213KS0E1 is 212x104, 2.3-3.6 V. It has no partial update and no sleep spec (PDi cuts its power instead). Its booster MOSFET MCH3478 has 0 stock at LCSC, it has a vendor-only driver licence, and it is not in GxEPD2. | **Good Display GDEY0213B74** (SSD1680): 250x122, VCI 2.2-3.7 V, 0.42 s partial refresh, 1 uA deep sleep, GxEPD2 support, booster assemblable at JLC. Use GDEM0213B74WT on the same footprint if it must work below 0 °C. |
| Booster | GDEY reference circuit: **47 uH** (not 10 uH), Si1308EDL, 3x MBR0530, 2.2 Ω sense resistor, 1 MΩ gate pull-down, 1 uF/25 V and 4.7 uF/25 V caps. | See the reconciliation note below on diodes. Put the panel VCI (and with it the booster inductor feed) behind a P-FET load switch. |
| FPC connector | Molex 503480-2400 (C122434) has 0 stock. PDi recommends Hirose FH34SRJ-24S. | **Hirose FH34SRJ-24S-0.5SH(50)** (C324726, about 10k in stock, dual contact). Alternatives: XUNPU FPC-05F-24PH20 (C2856805) and JUSHUO AFC01-S24FCA-00 (C262669). No 24-pin 0.5 mm connector is a Basic part. |
| Nav switch | TE 2434804-1 **is** a real 5-way switch (4 directions plus push), but LCSC has 0 stock and DigiKey quotes 18-21 weeks. Its datasheet link is a brochure with no pinout. | **Alps SKRHABE010** (C139794, 6,785 in stock, $0.78 at qty 1 / $0.50 at qty 100). Pinout: 1=A, 2=Center, 3=C, 4=B, 5=Common, 6=D. Directions sit at 45° to the body edges. Cheaper clone with the same pads: HRO K1-5202UA-01 (C136710). |
| IR | Side-looking matched pair: VSMB2948SL emitter (C6604342, only 176 in stock) and VEMT2523SLX01 detector (C511451). The phototransistor's 20-40 us rise/fall time limits the link to about 2.4-9.6 kbps, so **IrDA SIR mode cannot be used** with discrete parts. | Plain UART with inverted TX. LED at about 20 mA through 68-82 Ω and a low-side FET. Phototransistor load 4.7-10 kΩ. The bulk capacitor supplies the pulses. A TFBS4711 IrDA transceiver costs $5-7 with 45 in stock; a 38 kHz receiver needs at least 2.5 V and runs only about 1-2 kbps. |
| Battery holder | NL5077-LF is through-hole, about 30x29 mm, and not sold at LCSC/JLC (Farnell 1216361). JLC lists Renata SMTU2450N-LF (C5443913) and HU2450N-LF (C6060329). | **MYOUNG BS-24-B4AA016** (C964809, SMT, gold contacts, $0.35 at qty 100, 586 in stock). Budget option: MY-2450-01 (C964845). |
| Battery | Renata rates the CR2450 at 3 mA continuous. E-paper refresh peaks at 10-20 mA, and up to 40-50 mA for the WT panel. | Use 2x47 uF X5R 0805 (C16780, Basic) or 4x22 uF MLCC as bulk; leakage is nA typical. Avoid tantalum and polymer (6.3-63 uA max leakage). Check battery voltage before each refresh. |
| P-FETs | DMP2035U IGSS is 10 uA max, a continuous drain when used for reverse-polarity protection. | **AO3401A** (C15127, Basic): IGSS 100 nA max, 85 mΩ max at -2.5 V. |
| KiCad libs | Present: `MCU_ST_STM32U0:STM32U073KCUx`, `Timer_RTC:RV-3028-C7`, `Package_SON:MicroCrystal_C7_SON-8_1.5x3.2mm_P0.9mm`, `Connector:Tag-Connect_TC2030-IDC-NL_2x03_P1.27mm_Vertical`, `Connector:Conn_ARM_SWD_TagConnect_TC2030-NL`, and QFN-32 5x5 footprints. Missing: Molex 503480, Renata 2450/NL5077, any 5-way nav switch footprint or symbol, and side-view IR parts. | Custom footprints are needed for the nav switch, IR parts, coin cell holder and FPC connector (if not using an existing Hirose or Jushuo footprint). |

### Reconciliation notes (where the sections disagree)

- **Booster diodes.** The display section lists B5819W SL (C8598, Basic) as the cheap substitute for MBR0530. The passives section advises against it: the datasheet gives IR at up to 1 mA max at 40 V, and MBR0530T1G is 130 uA. It prefers BAT54/BAT54S (C19726, 2 uA max at 25 V), whose current rating (200 mA) covers the SSD1680 booster's peak. **Resolution:** if the panel VCI and booster are behind a load switch (recommended anyway), diode leakage only matters during refresh, and B5819W is acceptable. If the booster rail stays powered, use BAT54-family diodes. Whether BAT54's 200 mA rating covers the 47 uH inductor's peak current at the SSD1680's 2.2 Ω sense limit is UNVERIFIED; check against the SSD1680 sense threshold before committing.
- **Inductor value.** The passives section picked a 10 uH Sunlord SWPA252012S100MT (C37428) because the brief assumed 10 uH. The GDEY0213B74 reference circuit specifies **47 uH / 500 mA**, for example ANR3015T470M (C7427088). Use 47 uH for the GDEY panel. 10 uH is the PDi E2213KS0E1 value (ATNR4010100MT).
- **Repo state.** When this research started, `hardware/footprints/irl2fa.pretty`, `hardware/symbols/irl2fa.kicad_sym`, the schematic, the PCB and `models/` existed. During the session, git history was rewritten and those files are no longer in the working tree. Only `fp-lib-table`, `sym-lib-table`, `irl2fa.kicad_pro` and `LICENSE` remain. This research did not cause that, and no repo file was edited.

---

## Display: 2.13" e-paper for IRL2FA (researched 2026-10-29)

Downloaded sources are in (local download folder, not committed). Anything marked **UNVERIFIED** could not be confirmed from a primary source.

> **The old PDi link is dead.** `https://www.pervasivedisplays.com/wp-content/uploads/2023/02/1Pxxx-00_tentative_E2213KS0E1_20230417.pdf` now returns PDi's "Page not found" HTML. LCSC hosts the same document (Doc. No. 1Pxxx-00, **Revision "Tentative"**, 2023-04-17, 29 pp): https://datasheet.lcsc.com/datasheet/pdf/7d5790dd591fe47305914f294669b21a.pdf?productCode=C19845154 (product page https://www.lcsc.com/datasheet/lcsc_datasheet_2401222057_Pervasive-Displays-E2213KS0E1_C19845154.pdf). In this section, "PDi DS" means that PDF. PDi has never published a final, non-tentative revision that I could find.

### 1. Pervasive Displays E2213KS0E1

#### 1.1 Core specs (all from PDi DS unless noted)

| Item | Value | Source |
|---|---|---|
| Type | 2.13" a-Si active-matrix TFT EPD, black/white, "wide temperature, embedded fast update" (PDi film **K**, iTC = internal timing controller) | PDi DS §1; film K: https://github.com/PervasiveDisplays/Pervasive_Wide_Small (README: "wide temperature and embedded fast update (film `K`)") |
| Resolution | **212 x 104** (111 dpi), pixel pitch 0.229 x 0.228 mm. This is lower than the 250x122 of the Good Display parts. | PDi DS Table 1-1 |
| Active area | 48.548 x 23.712 mm | PDi DS Table 1-1 |
| Outline | 59.2 x 29.2 x **0.9** mm (excluding FPC and masking film). Max 1.2 mm at the FPC stiffener. Tolerance ±0.3 mm. | PDi DS Table 1-1, Fig 1-1 |
| FPC | 24 pins, 0.5 mm pitch (P0.5 x 23 = 11.50 mm), finger width 0.35 mm, FPC thickness 0.30 ±0.03 mm. FPC tail 15.3 mm from glass, 12.5 mm wide, with a stiffener (3.5 mm). | PDi DS Fig 1-1 |
| Weight | 3.3 ±1 g | PDi DS |
| Interface | SPI with CSB, SCL, SDA (**SDA is "input/output"**), DC, RST_N, BUSY_N, BS. BS is tied to GND. | PDi DS Table 5-2 |
| Supply VDD = VDDIO | **min 2.3 V / typ 3.0 V / max 3.6 V**. Absolute max -0.3 to 6.0 V. | PDi DS Table 3-1, 3-2 |
| Logic levels | VIH ≥ 0.7·VDDIO, VIL ≤ 0.3·VDDIO | PDi DS Table 3-2 |
| Update current | IDD **2.3 mA** typ, **IPEAK 21 mA**, measured at VDDIO = 3.0 V on PDi's test jig | PDi DS Table 3-2 |
| Update energy (my calculation) | The scope plot in Fig 3-2 shows a window of Δt = 2.386 s at a mean of 2.276 mA, so about **5.4 mA·s (≈16 mJ at 3 V) per normal update** | PDi DS Fig 3-2 |
| Refresh time (normal/global) | **2.4 s typ** at 25 °C. Excludes power-on/off time. | PDi DS Table 6-2 |
| Fast update | Supported at 0–50 °C. The DS says to "execute normal update mode when the image gets worse". **Fast-update duration: UNVERIFIED** (not given in the DS). | PDi DS Table 2-1 note 4 |
| Partial update (windowed, no flash) | **Not offered.** The PDi library only exposes normal and fast (global) update for film K. | Pervasive_Wide_Small source (`COG_initial`: `UPDATE_NORMAL` / `UPDATE_FAST` only) |
| Sleep / deep-sleep current | **Not specified.** PDi's method is to cut VDD. The reference circuit labels VDD "Connect to a Transistor Switch to prevent leakage current", and the library's power-off comment reads "clear D/C, RESET, CS, SPI pins to LOW; keep BUSY floating; cut Vcc off". | PDi DS Fig 5-1; `COG_stopDCDC()` in Pervasive_Wide_Small.cpp |
| Operating temp | **-15 to +60 °C** (fast update 0–50 °C). Storage -20 to +60 °C. | PDi DS Table 2-1 |
| Driver IC | Chip-on-glass; PDi does not name it (**UNVERIFIED**). The library drives it with a UltraChip-style command set (0x00 PSR, 0x04 PON, 0x12 DRF, 0x02 POF, 0xE5/0xE0 temperature) and reads PSR values from the COG's OTP using **3-wire SPI reads on SDA**. | https://github.com/PervasiveDisplays/Pervasive_Wide_Small/blob/main/src/Pervasive_Wide_Small.cpp |
| Family | Not "Spectra", which is PDi's BWR/colour line (e.g. E2213JS0C1), and not "Aurora". It is the **film K "wide temperature / embedded fast update" iTC** line. Library screen code: `eScreen_EPD_213_KS_0E`. | Pervasive_Wide_Small; Mouser family page https://www.mouser.com/new/pervasive-displays/pervasive-displays-wide-temperature-e-ink-displays/ |
| Driver licence | PDi libraries are **CC BY-SA 4.0** and say "For exclusive use with Pervasive Displays screens". This is a compatibility question for an AGPL firmware: **check before copying code**. | Pervasive_Wide_Small README |
| GxEPD2 support | **None.** GxEPD2's supported-panel list contains no PDi part. | https://github.com/ZinggJM/GxEPD2/blob/master/README.md |

#### 1.2 FPC pinout (PDi DS Table 5-2)

Pin 1 and pin 24 are marked on the FPC drawing (Fig 1-1).

| Pin | Signal | Type | Function / connection |
|---|---|---|---|
| 1 | NC | – | – |
| 2 | GDR | O | N-channel MOSFET gate drive (booster) |
| 3 | RESE | I | Booster current-sense input |
| 4 | NC | – | – |
| 5 | VDHR | C | Positive source driving voltage, cap to GND |
| 6 | NC | – | – |
| 7 | NC | – | – |
| 8 | BS | I | Interface select, tie to GND |
| 9 | BUSY_N | O | Busy. **Low = busy** (opposite polarity to SSD1680) |
| 10 | RST_N | I | Reset, active low |
| 11 | DC | I | Data/command |
| 12 | CSB | I | Chip select |
| 13 | SCL | I | SPI clock |
| 14 | SDA | I/O | SPI data (bidirectional) |
| 15 | VDDIO | P | I/O supply, tie to VDD |
| 16 | VDD | P | Chip supply |
| 17 | GND | P | Ground |
| 18 | VDDD | C | Internal regulator output, cap to GND |
| 19 | NC | – | – |
| 20 | VDH | C | Positive source voltage, cap |
| 21 | VGH | C | Positive gate voltage, cap |
| 22 | VDL | C | Negative source voltage, cap |
| 23 | VGL | C | Negative gate voltage, cap |
| 24 | VCOM | C | VCOM, cap |

The text labels in the Fig 5-1 schematic graphic are drawn one row off from the pin numbers. Use Table 5-2.

This pinout is essentially the same as the Good Display / SSD1680 24-pin layout: GDR 2, RESE 3, BS 8, BUSY 9, RES 10, DC 11, CS 12, SCL 13, SDA 14, VDDIO 15, VCI 16, GND 17, VDD 18, VGH 21, VGL 23, VCOM 24. The differences are:
- Pins 5/6/7/19: PDi VDHR/NC/NC/NC vs GD VSH2/TSCL/TSDA/VPP.
- Pin 20: VDH vs VSH1.
- Pin 22: VDL vs VSL.
- BUSY polarity.

Both panels also have the same 59.2 x 29.2 mm glass. One footprint can therefore take either panel, with firmware differences.

#### 1.3 Booster: external, required

The panel has **no internal inductor booster**. The COG drives an external boost converter through GDR/RESE (PDi DS Fig 4-1 "EPD Boost Circuit", Fig 5-1). Reference circuit (PDi DS §5.2, Fig 5-1, p.19):

| Ref | Part / value | Notes (from DS) |
|---|---|---|
| L | **10 µH ARLITECH ATNR4010100MT**, ±20 %, 0.8 A | Vcc to drain node |
| Q | **onsemi MCH3478**, SOT-23 (SC-70FL in onsemi's naming), N-ch 30 V / 2 A, gate on GDR | Selection: RDS < 235 mΩ, VDSS = 30 V, spec'd at **VGS = 2.5 V @ 0.5 A** |
| Rsense | **0.47 Ω**, source to GND, sense node to RESE | |
| D x3 | **PANJIT SS2040FL**, SOD-123FL Schottky | Vf as low as possible (0.2–0.4 V), VRRM > 25 V. Forms the VGH (positive) and VGL (negative, via charge pump) rails. |
| Pump cap | **4.7 µF/25 V** | Flying cap of the negative charge pump |
| VDHR, VDDD, VDH, VGH, VDL, VGL, VCOM caps | **1 µF/25 V** each (VDDD drawn as 1 µF/25 V) | 7 shown as 1 µF/25 V around the connector, plus 1 µF/25 V on the VGL pump output |
| VDD decoupling | **0.1 µF/16 V + 1 µF/6.3 V** | |
| Supply | "Connect to Power Switch … Connect to a Transistor Switch to prevent leakage current" | i.e. a high-side load switch from the MCU |
| Connector | "ZIF-24-0.5-1" | |

PDi's EXT3 / EXT3-1 board schematic: **not retrieved (UNVERIFIED)**. The datasheet reference circuit above is the authoritative one.

**Coin-cell concern (my engineering inference).** GDR swings at most to about VDD. At 2.3–2.5 V, MCH3478 (spec'd at VGS = 2.5 V) is marginal. A MOSFET with Vgs(th) ≤ 1 V and RDS(on) specified at 1.8 V is safer. Good Display's choice, Si1308EDL (Vgs(th) ≤ 1.5 V), is the usual pick. I did not verify Si1308EDL's 1.8 V RDS(on).

JLC/LCSC availability of these parts (JLCPCB parts API, queried 2026-10-29):

| Part | LCSC # | JLC tier | Stock | Unit $ |
|---|---|---|---|---|
| MCH3478-TL-W | C891526 | Extended | **0** | 0.107 |
| SI1308EDL-T1-GE3 | C469327 | Extended | 34,662 | 0.27 |
| SS2040FL (PANJIT) | C268712 | Extended | 9,347 | 0.079 |
| B5819W SL (alt. Schottky, 40 V) | C8598 | **Basic** | 573,529 | 0.026 |
| MBR0530 | C77336 | Extended | 51,996 | 0.039 |
| 10 µH 3x3 (ANR3015T100M) | C6364790 | Extended | 107,442 | 0.041 |
| 47 µH 3x3 (ANR3015T470M) | C7427088 | Extended | 13,796 | 0.041 |
| 1 µF 50 V X5R 0603 (CL10A105KB8NNNC) | C15849 | **Basic** | 12.7 M | 0.014 |
| 4.7 µF 25 V X5R 0805 (CL21A475KAQNNNE) | C1779 | **Basic** | 1.9 M | 0.034 |
| 0.47 Ω 0603 1 % | C23411 | Extended | 111,169 | 0.005 |
| 2.2 Ω 0603 1 % | C22939 | **Basic** | 285,800 | 0.003 |
| 1 MΩ 0603 1 % | C22935 | **Basic** | 5.1 M | 0.002 |

ATNR4010100MT could not be found at JLC/LCSC.

#### 1.4 Connector: is Molex 503480-2400 right?

- **PDi recommends** "HRS FH34SRJ 24S or STARCONN 6700S24 or Compatible", 24 pins at 0.5 mm (PDi DS Table 5-1). The 503480-2400 is not named, but it is a valid "compatible" part:
  - It is 0.5 mm pitch, 24 positions, accepts 0.3 mm FPC, right angle, SMT, Easy-On.
  - It has **double-sided (top and bottom) contacts**, so FPC orientation does not matter.
  - Sources: https://www.lcsc.com/product-detail/C122434.html ("Double-Sided Contacts, Top and Bottom Entry"); element14 https://my.element14.com/molex/503480-2400/conn-ffc-fpc-r-a-24pos-1row-0/dp/3499028.
- **Molex 503480-2400 at LCSC: C122434.** It has **0 stock** and costs $1.26 @1 / $0.80 @100 (LCSC API, 2026-10-29). It is Extended at JLC.
- **Better choice: Hirose FH34SRJ-24S-0.5SH(50), LCSC C324726.** This is the part PDi names.
  - **10,105–10,250 in stock**, $0.250 @5 / $0.196 @50 / $0.144 @500, JLC **Extended**.
  - Top and bottom contacts, 1.0 mm height, 0.3 mm FPC.
  - Sources: https://www.lcsc.com/product-detail/FFC-FPC-Connectors_Hirose_FH34SRJ-24S-0-5SH-50_Hirose-HRS-FH34SRJ-24S-0-5SH-50_C324726.html, https://jlcpcb.com/partdetail/C324726, DigiKey https://www.digikey.tw/en/products/detail/hirose-electric-co-ltd/FH34SRJ-24S-0-5SH-50/5132528 ("Contacts, Top and Bottom").
- Clones:
  - JUSHUO AFH34-S24FIA-1H, **C5139830**: 352 in stock, $0.236 @5 / $0.186 @50, Extended, double-sided, flip-lock, 1 mm (https://jlcpcb.com/partdetail/C5139830).
  - Lian Xin XDFC-0522-24P, **C7527527**: 1,060 in stock, $0.159 @5 / $0.125 @50, double-sided flip (LCSC API).
- **No 24-pin 0.5 mm FPC connector is a JLC Basic part.** I found none, so every option costs a JLC Extended-part fee.

#### 1.5 Price / stock (E2213KS0E1)

| Distributor | Price | Stock | Source / date |
|---|---|---|---|
| LCSC C19845154 | $13.20 @1, $5.27 @200 | **0** | LCSC API, 2026-10-29 |
| DigiKey 1452-E2213KS0E1-ND | ~$4.76 @1 | 0 in one snapshot, 16-week lead time | Search-engine snapshot of https://www.digikey.com/en/products/detail/pervasive-displays/E2213KS0E1/18627002. **UNVERIFIED live** (DigiKey returns 403 to fetches). |
| Mouser 341-E2213KS0E1 | ~$5.00–5.45 @1 | ~297 | Search snapshot of https://www.mouser.com/ProductDetail/Pervasive-Displays/E2213KS0E1?qs=Jm2GQyTW%2FbgruuNqURwZxQ%3D%3D. **UNVERIFIED live** (Mouser timed out). |

### 2. Alternatives

#### 2.1 Good Display GDEY0213B74 (current GD part)

Datasheet: https://v4.cecdn.yun300.cn/100001_1909185148/GDEY0213B74.pdf (Rev 1.0, 2021-03-17, 37 pp). The download needs a `Referer: good-display.com` header. Product page: https://www.good-display.com/product/391.html.

| Item | Value |
|---|---|
| Resolution | **250 x 122** (130 dpi), pitch 0.194 mm |
| Active area | 48.55 x 23.7046 mm |
| Outline | 59.2 x 29.2 x **1.0** mm (same glass as the PDi part) |
| Controller | **Solomon SSD1680** (SSD1680Z8 on the drawing) |
| Interface | 4-wire SPI (BS1 = L) or 3-wire 9-bit SPI (BS1 = H). SDA is I/O, but **reads are optional**: the driver does not need OTP readback. |
| VCI | **2.2 / 3.0 / 3.7 V** (DS §6.2). SSD1680 datasheet says the same: "VCI 2.2 to 3.7V". SSD1680 has a built-in VCI low-voltage detect command (0x15, thresholds 2.2–2.6 V). |
| Typ. operating current | 3.5 mA @ 3.0 V (10.5 mW) |
| Full / fast / partial refresh | **3 s / 1.5 s / 0.42 s** @25 °C. GD recommends a full refresh after 5 consecutive fast/partial updates. |
| Sleep / deep sleep | Sleep 20 µA. **Deep sleep 1 µA typ / 5 µA max** (RAM not retained). SSD1680 DS: deep-sleep mode 1 = 1 µA typ / 3 µA max (RAM retained, not accessible), mode 2 = 0.7 / 3 µA. |
| Operating temp | 0 to +50 °C (storage -25 to +70 °C) |
| BUSY | **High = busy** |
| Booster | External: GDR MOSFET + inductor + 3 Schottky, in the same topology as PDi |

GDEY0213B74 reference circuit (DS §12, p.29):

| Ref | Value / part | Requirement (DS) |
|---|---|---|
| L1 | **47 µH, 500 mA** (it is not 10 µH) | "refer to NR3015: Io = 500 mA (max)" |
| Q1 | **Vishay Si1308EDL** | VDS ≥ 30 V, **Vgs(th) ≤ 1.5 V**, RDS(on) ≤ 400 mΩ |
| D1–D3 | **MBR0530** | VR ≥ 30 V, Io ≥ 500 mA, Vf ≤ 430 mV |
| R1 | **1 MΩ** | GDR gate pull-down |
| R2 | **2.2 Ω** | RESE current sense. GD uses 2.2 Ω; PDi uses 0.47 Ω. |
| C3 | 4.7 µF/25 V | Charge-pump flying cap |
| C4 | 4.7 µF/25 V | Input (3.3 V) bulk |
| C2 (VSH2), C5 (PREVGH), C6 (VCI), C7 (VDD), C9 (VSH1), C10 (VSL), C11 (VGL), C12 (VCOM) | **1 µF/25 V** | C1–C12: 0603/0805, X5R/X7R, **≥ 25 V** |
| P1 | 24-pin 0.5 mm FPC | |

The SSD1680 datasheet application circuit (Table 13-1) gives the same circuit with Q1 = Si1304BDL / NX3008NBK (Vgs(th) 0.9 V typ, 1.3 V max), L1 = 47 µH (CDRH2D18 / LDNP-470NC), R1 = 2.2 Ω, C8 = 0.47–1 µF X7R 25 V (> 0.25 µF effective @ 18 V bias), and C0–C1 = 1 µF 6 V or 25 V. Source: https://v4.cecdn.yun300.cn/100001_1909185148/SSD1680.pdf (Rev 0.14, Jun 2019) §11, §13.

Price / where to buy:
- **Good Display shop (buyepaper / buy-lcd.com):** **$4.95** (list $5.82), 3–5 % qty discount, page showed **"0 in stock" / Notify Me** on 2026-10-29. https://www.buy-lcd.com/products/213-inch-250x122-raspberry-pi-epd-electronic-epaper-price-tag
- **Other EU/IN resellers:** Eckstein https://eckstein-shop.de/GooDisplay-Marken-EN__213_1 (price not checked).
- **AliExpress (Good Display official store):** **UNVERIFIED** (not fetched).
- **LCSC:** no listing found (**UNVERIFIED** absent).
- Driver: **GxEPD2 class `GxEPD2_213_B74`** lists both GDEY0213B74 and GDEM0213B74 (README: https://github.com/ZinggJM/GxEPD2). GD also provides STM32/ESP32/Arduino sample code on the product page.

#### 2.2 Good Display GDEM0213B74 / GDEM0213B74WT

- **GDEM0213B74** is in GxEPD2's list as "2.13" b/w 122x250, SSD1680" (GxEPD2 README). It is the predecessor of GDEY0213B74; the "M" vs "Y" in the name probably marks a panel-supplier generation (**UNVERIFIED**). Its datasheet URL on GD's CDN returns an error and I found no current GD product page. **Treat it as superseded/EOL (UNVERIFIED).**
- **GDEM0213B74WT** (wide-temp variant, page https://www.good-display.com/product/614.html, DS https://v4.cecdn.yun300.cn/100001_1909185148/GDEM0213B74WT.pdf, Rev 1.0, 2024-11-28):
  - 250 x 122, SSD1680, 59.2 x 29.2 x 1.0 mm.
  - VCI **2.2–3.7 V**.
  - **Operating -25 to +50 °C.**
  - Typical 4.2 mA (12.6 mW), **peak 40 typ / 50 max mA**, update 2 s @23 °C.
  - Sleep 20 µA, deep sleep 1 / 5 µA.
  - Same reference circuit (Si1308EDL, MBR0530, NR3015-class inductor, ≥ 25 V caps).
  - GD's page says indoor use only, no direct sunlight.

#### 2.3 WeAct Studio 2.13" e-paper module

Repo: https://github.com/WeActStudio/WeActStudio.EpaperModule

- Panel datasheet in the repo: **Yingruida YRD0213BBS800F41HP-M7** (`Doc/2.13 Inch Black&Write/`). The repo ships the SSD1680 datasheet, so the panel is SSD1680-class. **Resolution 250x122 and controller are UNVERIFIED**: the YRD PDF is image-only.
- Module schematic (`Hardware/WeAct-EpaperModule-2.13_2.9 SchDoc.pdf`):
  - On-board **ME6231A33 3.3 V LDO** with solder-bridge bypass.
  - 10 µH inductor, **AO3402** MOSFET, 3x **B5819W SL**, 2.2 Ω sense, 100 kΩ gate pull-down, 1 µF caps, 100 Ω series resistors on the SPI lines.
  - 8-pin 1.25 mm or 2.54 mm header.
- This is a breakout board, not a bare panel. **Unsuitable for the IRL2FA PCB**: wrong form factor, plus an LDO that drops out below ~3.3 V unless bypassed. It is fine as a bench prototype for SSD1680 firmware. Price: **UNVERIFIED** (not fetched).

#### 2.4 Other PDi 2.13" panels

| Part | Resolution / colours | Notes | Source |
|---|---|---|---|
| E2213PS0E1 / PS0E2 | 212x104 B/W | VDD **2.3–3.6 V**. Fast update only optimised for 15–30 °C. Operating 0–50 °C. Refresh 3.8 s. ICC 1.09 mA (PS0E1) / 3.28 mA (PS0E2). Same booster (MCH3478, SS2040FL, ATNR4010100MT 10 µH) and FH34SRJ connector. | DS 1P331-00 rev 01 (2022-08-05): https://atta.szlcsc.com/upload/public/pdf/source/20250627/6E5B20BC8139209C9D4726B38E2D62AD.pdf |
| E2213JS0C1 | 212x104 **black/white/red** ("Spectra") | Slow 3-colour refresh, no fast update (**refresh time UNVERIFIED**). Unsuitable. | https://www.digikey.com/en/products/detail/pervasive-displays/E2213JS0C1/13572392 |
| E2213HS091 / CS091 / FS091 | Older "091" iTC generation | Out of stock at LCSC (C6467925). **Not evaluated.** | https://fat.lcsc.com/product-image/C6467925.html |

#### 2.5 Side-by-side

| | PDi E2213KS0E1 | GD GDEY0213B74 | GD GDEM0213B74WT |
|---|---|---|---|
| Resolution | 212x104 | **250x122** | 250x122 |
| Controller | Undisclosed iTC (OTP read needed) | **SSD1680** (open DS) | SSD1680 |
| Min supply | 2.3 V | **2.2 V** | 2.2 V |
| Booster | External: 10 µH, MCH3478, 3x SS2040FL, 0.47 Ω | External: 47 µH, Si1308EDL, 3x MBR0530, 2.2 Ω, 1 MΩ | Same as GDEY |
| Full / fast / partial | 2.4 s / ? / **none** | 3 s / 1.5 s / **0.42 s** | 2 s / – / partial per SSD1680 |
| Update current | 2.3 mA avg, **21 mA peak** | 3.5 mA avg (peak not stated) | 4.2 mA avg, **40–50 mA peak** |
| Sleep | Not specified; cut power | **1 µA deep sleep** | 1 µA |
| Temp | **-15…+60 °C** | 0…+50 °C | -25…+50 °C |
| Thickness | 0.9 mm | 1.0 mm | 1.0 mm |
| Open driver support | PDi lib only (CC BY-SA, PDi-only clause) | **GxEPD2, many others** | GxEPD2 (B74 class, UNVERIFIED for WT) |
| Price @1 | ~$4.8–5.5 (DigiKey/Mouser), LCSC $13.20 | **$4.95** (GD shop) | UNVERIFIED |
| Availability | Mouser stock ~297 (snapshot); LCSC 0; DigiKey 16 wk | GD shop 0 in stock (restocks typical), widely resold | GD direct |

### 3. Recommendation

**Use the Good Display GDEY0213B74 (SSD1680).** Reasons:

1. **Lowest supply voltage.** The rated VCI minimum is 2.2 V (PDi: 2.3 V). The SSD1680 also has a programmable VCI-low detect (2.2–2.6 V), which lets firmware refuse to refresh on a sagging cell.
2. **True partial refresh (0.42 s, no flash) and 1.5 s fast refresh.** These suit updating only the TOTP digits and countdown. The PDi film-K panel has only global normal/fast updates.
3. **Real deep sleep (1 µA typ, 5 µA max).** A load switch is optional rather than mandatory.
4. **Open documentation and drivers.** The full SSD1680 datasheet is public. GxEPD2 and many other open-source drivers exist. Write-only 4-wire SPI works, so there is no OTP readback over bidirectional 3-wire SPI and no PDi-only licence clause in the way of AGPL firmware.
5. **Higher resolution (250x122 vs 212x104) at about the same price ($4.95)** and identical 59.2 x 29.2 mm glass.
6. **Booster BOM is fully JLC-assemblable:**
   - Si1308EDL C469327 or SI1308EDL C7603347
   - MBR0530 C77336, or B5819W SL C8598 (Basic)
   - 47 µH 3x3 C7427088
   - 1 µF 50 V C15849 (Basic)
   - 4.7 µF 25 V C1779 (Basic)
   - 2.2 Ω C22939 (Basic)
   - 1 MΩ C22935 (Basic)
   - Connector: FH34SRJ-24S-0.5SH(50) **C324726** (10k stock, $0.25)

   The PDi reference MOSFET (MCH3478) is out of stock at JLC/LCSC.

**Caveats:**
- **GDEY0213B74 is rated 0–50 °C.** If cold operation matters (e.g. a keychain in winter), use GDEM0213B74WT (-25 °C) on the same footprint.
- **Peak update current (tens of mA) is the real coin-cell risk** for both vendors. A CR2450 late in life has high internal resistance. Budget a bulk capacitor on the display rail (e.g. 100 µF+ low-leakage) and gate refreshes on a VCI/VBAT check. This is my engineering inference; I did not verify a GDEY0213B74 peak-current figure.
- **Keep the footprint dual-compatible.** Because the pinout and glass match, the same 24-pin footprint and booster can take the PDi part later. Make the sense resistor and inductor value changeable (2.2 Ω/47 µH for GD, 0.47 Ω/10 µH for PDi), and allow for BUSY polarity being opposite in firmware.
- **Replace Molex 503480-2400 (C122434, 0 stock, $1.26) with Hirose FH34SRJ-24S-0.5SH(50) (C324726).** It is the connector both PDi and the GD reference designs fit. Both parts are dual-contact, so FPC orientation is not an issue.
## MCU, RTC and battery research (IRL2FA)

Research date 2026-10-29. Prices and stock come from LCSC's product-detail JSON (`wmsc.lcsc.com/ftps/wm/product/detail?productCode=Cxxxx`, fetched live today). The human-readable page for each part is `https://www.lcsc.com/product-detail/<C#>.html`. Basic/Extended status is marked verified only where a jlcpcb.com/partdetail page was checked. Anything marked **UNVERIFIED** was not confirmed against a primary source.

Primary datasheet used: **STM32U073x8/B/C datasheet, DS14548 Rev 2 (March 2024)**, the 146-page ST PDF mirrored by LCSC at https://datasheet.lcsc.com/datasheet/pdf/694bd8ccd6c702db840f422207f65753.pdf?productCode=C22445363. The ST copy (https://www.st.com/resource/en/datasheet/stm32u073c8.pdf) timed out from this host.
> **Correction:** DS14581 is the **STM32U031x4/6/8** datasheet, not the U073 one. It is confirmed in the footer of the U031 PDF: https://datasheet.lcsc.com/datasheet/pdf/1e7b3bb58b32479a0e6f3b3402c23cae.pdf?productCode=C23172783

---

### 1. STM32U073KCU6: confirmed facts

| Item | Value | Source |
|---|---|---|
| Exists / status | Yes. Active, industrial grade, UFQFPN32 5x5x0.55 mm | https://www.st.com/en/microcontrollers-microprocessors/stm32u073kc.html |
| Core | Cortex-M0+, 56 MHz | DS14548 p.1 |
| Flash / SRAM | 256 KB flash (ECC, RDP L0/L1/L2 with 128-bit password regression, securable/HDP area), 40 KB SRAM (32 KB SRAM1 + 8 KB SRAM2, SRAM2 retainable in Standby) | DS14548 §3.4 |
| Package | UFQFPN32, D = E = 5.00 mm, **pitch e = 0.50 mm**, body height 0.55 mm typ, exposed pad D2 = E2 = 3.60 mm typ (recommended to be soldered to GND) | DS14548 Table 89, §7.2 |
| VDD | 1.71 to 3.6 V. VDDA 1.62 V (ADC/COMP) to 3.6 V. VBAT is internally bonded to VDD on packages without a VBAT pin (UFQFPN32 has none). | DS14548 §3.x "Power supply schemes" |
| GPIO / wake-up pins on the K (32-pin) package | 27 GPIO, **only 3 WKUP pins** | DS14548 Table 1 |
| AES | **No** (U073 has none; the only "AES" mention in DS14548 is in the legal text) | DS14548 |
| TRNG | **Yes** | DS14548 Table 1, §3.23 |
| USART/LPUART | 4 USART + 3 LPUART. **IrDA SIR ENDEC is on USART1 and USART2 only** (not USART3/4; LPUARTs have none) | DS14548 Table 9 |

**Headline low-power figures (first page):** Shutdown 16 nA; Standby 160 nA with RTC and 30 nA without; Stop 2 825 nA with RTC and 695 nA without; Run 52 µA/MHz. These are best-case figures at **VDD = 1.8 V**. The figures at 3.0 V (a CR2450's real voltage), 25 °C typical, are higher. From DS14548 Tables 40 to 42:

| Mode @ 3.0 V, 25 °C | Typ |
|---|---|
| Shutdown, RTC off | 52.5 nA |
| Standby, RTC off, no IWDG, EN_ULP=0 | 68.5 nA |
| Standby + SRAM2 retention (adder) | +90 nA |
| Standby, RTC on LSE quartz low-drive, EN_ULP=1, LPCAL=1 | 260 nA |
| Standby, RTC on LSE quartz low-drive, EN_ULP=0, LPCAL=1 | 320 nA |
| Standby, RTC on LSI | 245 nA |
| Stop 2, RTC on LSE quartz, EN_ULP=0, LPCAL=1 | 985 nA |
| LSE oscillator alone, LSEDRV=00 (low drive) | 250 nA (Table 51) |

Wake from Standby or Shutdown is a reset, so firmware reboots on each button press. That is acceptable here; keep state in the backup registers, or in SRAM2 for +90 nA.

#### Sibling parts compared (LCSC live data 2026-10-29)

| MPN | LCSC # | Pkg | Flash/RAM | AES | Stock | 1+ / 10+ / 100+ (USD) | JLC class |
|---|---|---|---|---|---|---|---|
| **STM32U073KCU6** | [C22445363](https://www.lcsc.com/product-detail/C22445363.html) | UFQFPN-32 5x5 | 256K/40K | no | **16** | 8.92 / 7.63 / 6.17 | Extended ([jlcpcb](https://jlcpcb.com/partdetail/STMICROELECTRONICS-STM32U073KCU6/C22445363)) |
| **STM32U083KCU6** | [C22459164](https://www.lcsc.com/product-detail/C22459164.html) | UFQFPN-32 5x5 | 256K/40K | **yes** (128/256-bit) | **41** | 6.80 / 5.81 / 4.71 | Extended (UNVERIFIED, very likely) |
| STM32U073CCU6 | [C22455185](https://www.lcsc.com/product-detail/C22455185.html) | UFQFPN-48 7x7 | 256K/40K | no | 0 | 2.94 / 2.53 / (260+) 2.01 | — |
| STM32U073CBT6 | [C27035627](https://www.lcsc.com/product-detail/C27035627.html) | LQFP-48 | 128K/40K | no | 0 | 2.07 / 2.03 / 1.98 | — |
| STM32U073RCT6 | [C22445364](https://www.lcsc.com/product-detail/C22445364.html) | LQFP-64 | 256K/40K | no | 1 | 9.63 / … / 9.10 | — |
| STM32U031C8T6 | [C23172783](https://www.lcsc.com/product-detail/C23172783.html) | LQFP-48 | 64K/12K | no | 0 | 2.46 / 2.12 / (250+) 1.68 | — |
| STM32U031K8U6 / U073K8U6 / U073KBU6 | **no LCSC listing found** (searched lcsc.com and jlcpcb.com) | UFQFPN-32 | 64K/12K, 64K/40K, 128K/40K | no | — | — | — |

- STM32U083KC datasheet (Chinese edition, LCSC mirror) confirms the AES hardware accelerator (128/256-bit), the TRNG, and the same Standby/Shutdown figures: https://datasheet.lcsc.com/datasheet/pdf/1d8e3bb2642efa1578790415a746a5f0.pdf?productCode=C22459164
- The STM32U031 (DS14581) has Stop 2 at 630/515 nA (better than U073), Standby 160/30 nA, 64 KB flash, 12 KB SRAM, TRNG, IrDA on USART1/2, and only **4 WKUP pins across the family**. Its UFQFPN32 pin table is a separate table and its **pin-compatibility with U073 UFQFPN32 is UNVERIFIED**. Source: DS14581 PDF above.
- **Recommendation:** at LCSC today, **STM32U083KCU6 is cheaper (USD 4.71 @100 vs 6.17), better stocked (41 vs 16), adds hardware AES, and is the same package and family** as the STM32U073KCU6. If firmware ever encrypts the secret store (for example, AES-wrapping the secrets with a PIN-derived key), the U083 is strictly better. U073 and U083 have the same 256K/40K memory and the same headline power figures; the pinout is expected to match because both are UFQFPN32 in the same sub-family (the U083 datasheet document number was not checked) (**UNVERIFIED**: diff the U083 pin table before swapping). Both LCSC stocks are tiny, so for a JLC build pre-order or consign. 64 KB U031 parts are not stocked at LCSC in the 32-pin package.
- Western distributor pricing for STM32U073KCU6: DigiKey 497-STM32U073KCU6-ND around USD 4.25 @1, about USD 2.24 @2940, ~2,329 in stock, 20-week lead time (search snippet, live page returned 403, **UNVERIFIED**): https://www.digikey.com/en/products/detail/stmicroelectronics/STM32U073KCU6/22332782 ; TME about EUR 2.75 @1 and EUR 1.77 @100, 0 stock: https://www.tme.eu/en/details/stm32u073kcu6/st-microcontrollers/stmicroelectronics/ ; Future Electronics USD 1.58 @2940: https://www.futureelectronics.com/p/4184041 . **Mouser price: UNVERIFIED** (page timed out). STM32U083KCU6: Future Electronics USD 1.45 @2940 (https://www.futureelectronics.com/p/semiconductors--microcontrollers--32-bit/stm32u083kcu6-stmicroelectronics-2184041), DigiKey https://www.digikey.com/en/products/detail/stmicroelectronics/STM32U083KCU6/22334570 (snippet: ~1,308 stock, 30-week lead time, UNVERIFIED).

---

### 2. STM32U073 UFQFPN32 pinout (DS14548 Figure 3 + Table 12)

| Pin | Name | Notes |
|---|---|---|
| 1 | VDD | |
| 2 | PC14-OSC32_IN | LSE in. PC13–15 are on the backup power switch: sink ≤3 mA, ≤2 MHz, **must not source current (no LED drive)** |
| 3 | PC15-OSC32_OUT | LSE out (same limits) |
| 4 | PF2-NRST | NRST (can be reassigned to GPIO PF2 via option byte) |
| 5 | VDDA/VREF+ | VREF+ internally tied to VDDA; VREFBUF must stay disabled |
| 6 | PA0-CK_IN | **WKUP1**, TAMP_IN2 |
| 7 | PA1 | **WKUP3**, TAMP_IN5 |
| 8 | PA2 | **WKUP4**/LSCO |
| 9 | PA3 | |
| 10 | PA4 | DAC1_OUT1 |
| 11 | PA5 | |
| 12 | PA6 | |
| 13 | PA7 | |
| 14 | PB0 | |
| 15 | PB1 | |
| 16 | VSS | |
| 17 | VDD / **VDDUSB (datasheet conflict, see below)** | |
| 18 | PA8 | MCO |
| 19 | PA9 | |
| 20 | PA10 | |
| 21 | PA11 [PA9] | USB_DM; PA9/PA10 can be remapped onto pins 21/22 (SYSCFG_CFGR1) |
| 22 | PA12 [PA10] | USB_DP |
| 23 | PA13 (SWDIO) | internal pull-up at reset |
| 24 | PA14 (SWCLK) | internal pull-down at reset |
| 25 | PA15 | |
| 26 | PB3 | |
| 27 | PB4 | |
| 28 | PB5 | |
| 29 | PB6 | |
| 30 | PB7 | PVD_IN |
| 31 | PF3-BOOT0 | |
| 32 | VSS | |
| EP | exposed pad | "recommended to connect and solder to PCB ground" (Table 89 note 3) |

**Pin 17 discrepancy:** Figure 3 labels pin 17 "VDDUSB". Table 12 lists UFQFPN32 pin 17 as "VDD" and gives VDDUSB "-" for UFQFPN32. The datasheet also says all VDD, VDDA, VDDUSB and VBAT pins "must always be connected". **Tie pin 17 to VDD with its own 100 nF.** That is correct under either reading; USB just won't work below 3.0 V, and it isn't used here.

#### Alternate-function options on the 32-pin package (DS14548 Tables 13/14)

AF numbers: SPI1 = **AF5**, I2C1 = **AF4**, USART1/USART2 = **AF7**, LPUART1 = AF8, IR_OUT (IRTIM) = AF1.

| Function | Pin options (UFQFPN32 pin) |
|---|---|
| SPI1_SCK | PA1 (7), PA5 (11), PB3 (26), all AF5 |
| SPI1_MOSI | PA7 (13), PA12 (22), PB5 (28), all AF5 |
| SPI1_MISO | PA6 (12), PA11 (21), PB4 (27), all AF5 |
| SPI1_NSS | PA4 (10), PA15 (25), PB0 (14), all AF5 |
| I2C1_SCL | PA9 (19) AF4, PB6 (29) AF4 |
| I2C1_SDA | PA10 (20) AF4, PB7 (30) AF4 |
| USART1_TX / RX (IrDA capable) | TX PA9 (19) or PB6 (29), AF7; RX PA10 (20) or PB7 (30), AF7 |
| USART2_TX / RX (IrDA capable) | TX PA2 (8) AF7; RX PA3 (9) AF7 or PA15 (25) AF3 |
| LPUART1 (no IrDA) | TX PA2 AF8, RX PA3 AF8 |
| IR_OUT (TIM16/17 IR modulator) | PA13 AF1 (conflicts with SWDIO) |
| SWD | PA13 = SWDIO (AF0), PA14 = SWCLK (AF0) |
| WKUP | PA0 = WKUP1, PA1 = WKUP3, PA2 = WKUP4 (no others on this package) |

**Suggested non-conflicting allocation:**
- SPI1 for the e-paper on PA5 SCK / PA7 MOSI, with PA4 as CS (or GPIO), PA6 free for DC or BUSY.
- I2C1 on PB6/PB7 for the RV-3028.
- **USART1 on PA9/PA10 in IrDA SIR mode** for the IR LED and phototransistor. PA9/PA10 are also a system-bootloader USART.
- Wake sources PA0/PA1/PA2 for the "center" nav key plus the RTC INT.
- With only **3 WKUP pins and a 5-way switch**, either diode-OR the five keys into one WKUP pin and read them on GPIOs after wake, or use Stop 2 with EXTI on any pin (~1 µA instead of ~0.1 µA).

**Boot / BOOT0:**
- Boot is selected by the PF3-BOOT0 pin and option bits nBOOT_SEL / nBOOT0 / nBOOT1 (DS14548 §3.5).
- The system bootloader runs on USART PA9/PA10 or PA2/PA3, I2C PB6/PB7, SPI PA4–PA7, and USB PA11/12.
- Factory nBOOT_SEL = 1, which makes the BOOT0 pin ignored. The nBOOT0 bit decides the boot source, and an empty-flash check sends a blank part to the bootloader.
- Users report that fresh U073s only honour BOOT0 after nBOOT_SEL is cleared, and ST says the AN2606 pattern 11 is wrong. Sources: https://community.st.com/t5/stm32-mcus-products/stm32u073-can-you-bootload-a-factory-new-device/td-p/776257 , https://ndoo.sg/projects:electronics:stm32:stm32u0_notes .
- Since programming is via SWD, put a 10 kΩ pull-down on PF3 and a test pad. NRST gets 100 nF to GND (standard ST recommendation; **value UNVERIFIED for U0**, see DS14548 "Recommended NRST pin protection").

**Decoupling** (DS14548 Figure 13, power supply scheme): **n × 100 nF + 1 × 4.7 µF on VDD**, and **10 nF + 1 µF on VDDA**. Place them as close as possible to the pins.

**Crystal:** not needed. HSI16/MSI run the core, the RTC is external, and LSE pins 2/3 can be GPIOs (within the PC14/15 limits above).

---

### 3. MCU alternatives

All need SHA-1 HMAC in software; that is trivially small (<2 KB code) on any of these. Secret storage: 512 secrets × ~(20–64 B key + ~32 B label + metadata) ≈ 32–64 KB, so **≥128 KB flash, or external SPI flash, is needed**.

| MCU | Flash / RAM | Deep sleep (typ) | Sleep with timekeeping | Active | LCSC # / price (1+ / 100+) / stock | Fit for coin-cell TOTP |
|---|---|---|---|---|---|---|
| **STM32U073KC / U083KC** | 256K / 40K | Standby 68.5 nA @3 V, Shutdown 52.5 nA | Standby + RTC (LSE) 260–320 nA @3 V | 52 µA/MHz | see §1 | Good: fits ~512 secrets internally, TRNG, IrDA USART, U083 adds AES. Stock thin, expensive at LCSC. [DS14548] |
| STM32L031K6 | 32K / 8K + 1K EEPROM | 0.25 µA Standby | 0.68 µA Stop + RTC + 8 KB RAM | 76 µA/MHz | [C967648](https://www.lcsc.com/product-detail/C967648.html) UFQFPN-32: 4.35 / 4.19, 19 stk | Too little flash for 512 secrets, so external flash needed. ST page: https://st.com/en/microcontrollers-microprocessors/stm32l031k6.html |
| STM32L072KZ | 192K / 20K + 6K EEPROM | 0.29 µA Standby | 0.86 µA Stop + RTC + 20 KB RAM | 93 µA/MHz | [C529428](https://www.lcsc.com/product-detail/C529428.html) (KZU7) UFQFPN-32: 6.51 / 6.19, **0 stk** | Enough flash, TRNG, AES on L082 only; older/higher current, pricey. Datasheet: https://datasheet.lcsc.com/datasheet/pdf/536ccbd6392d4702041e586324d918c6.pdf?productCode=C529428 |
| AVR64DD32 | 64K / 8K / 256 B EEPROM | 650 nA Power-Down (PMODE AUTO) @3 V | +820 nA RTC on OSC32K (≈1.5 µA total) | 2.3 mA @24 MHz | [C5227839](https://www.lcsc.com/product-detail/C5227839.html) TQFP-32: 3.28 / (250+) 2.27, 23 stk | 64K marginal for 512 secrets, so external flash needed. No TRNG. ~10× the sleep current of U0. DS40002315B Table 38-5/38-6 (LCSC mirror https://datasheet.lcsc.com/datasheet/pdf/7c271931982c902221edd2f0e2f1350c.pdf?productCode=C5227839) |
| ATtiny1616 (old design) | 16K / 2K / 256 B EEPROM | 0.1 µA Power-Down @3 V | 0.69–0.71 µA Standby + RTC @3 V | — | [C507118](https://www.lcsc.com/product-detail/C507118.html) QFN-20: 1.10 / 0.69, 1,844 stk | Cheap and in stock, but 16K/2K is far too small, so external flash is mandatory. Datasheet Table 37-5: https://datasheet.lcsc.com/datasheet/pdf/5b1e335dba383225d40b56598b0f3a59.pdf?productCode=C507118 |
| MSPM0L1306 | 64K / 4K | 61 nA Shutdown | 1.0 µA STANDBY with 32 kHz timer (no calendar RTC) | 71 µA/MHz | [C20613927](https://www.lcsc.com/product-detail/C20613927.html) (TRHBR, VQFN-32 5x5): 1.94 / 1.34, 4 stk | Cheapest Cortex-M0+ with IrDA UART, but 4 KB RAM, no TRNG/AES, no RTC; 64K needs external flash. https://www.ti.com/product/MSPM0L1306 |
| RP2040 | external QSPI flash / 264K | DORMANT 0.18 mA typ | — | ~10 mA+ | [C2040](https://www.lcsc.com/product-detail/C2040.html): 1.10 / 0.87, 56,611 stk | **Unsuitable**: ~180 µA dormant would drain a CR2450 in months; flash is external and unprotected. RP2040 datasheet Table 635 (https://datasheet.lcsc.com/datasheet/pdf/3dad1d8f987a4e00bfc677055df1f959.pdf?productCode=C2040) |
| nRF52832 | 512K / 64K | 0.3 µA System OFF | 1.9 µA System ON + RTC wake | — | [C77540](https://www.lcsc.com/product-detail/C77540.html) QFN-48 6x6: 4.03 / 2.84, 50 stk | Overkill: the radio defeats the "air-gapped" claim; larger package; needs an RF-free layout. nRF52832 PS: https://datasheet.lcsc.com/datasheet/pdf/1aa6d7c89b6eb7e9c334420a8d103737.pdf?productCode=C77540 |
| STM32G031K8 (reference) | 64K / 8K | — | — | — | [C432207](https://www.lcsc.com/product-detail/C432207.html): 2.05 / 1.94, 0 stk | Not ultra-low-power; listed only for price reference |

**Takeaway:** STM32U0 (256K) is the only option here that holds ~512 secrets internally with nA-level sleep, a TRNG and an IrDA USART. Every cheaper option needs an external SPI NOR (adds ~1–2 µA standby unless power-gated, plus a part).

---

### 4. RTC options

| Option | Supply current (typ) | Accuracy | Supply | Pkg | LCSC # / price 1+ / 100+ / stock | Class |
|---|---|---|---|---|---|---|
| **RV-3028-C7 32.768kHz 1ppm-TA-QC** | **45 nA @3 V** | ±1 ppm @25 °C (factory calibrated); **not temperature-compensated**, tuning-fork parabola about −0.035 ppm/°C² around 25 °C (datasheet excerpt via search, **coefficient UNVERIFIED in PDF**) | 1.1–5.5 V | 3.2×1.5×0.8 mm C7 | [C3019759](https://www.lcsc.com/product-detail/C3019759.html): 2.49 / 1.63, **0 stock** | Extended ([jlcpcb](https://jlcpcb.com/partdetail/C3019759)) |
| RV-3028-C7 …-TA-**QA** (AEC-Q200 automotive) | 45 nA | same | 1.1–5.5 V | same | [C3304278](https://www.lcsc.com/product-detail/C3304278.html): 1.93 / 1.83, 0 stock today (search snippet earlier showed 1,914) | UNVERIFIED |
| RV-3032-C7 2.5PPM-TA-QC | 160 nA @3 V | **temperature-compensated**: ±1.5 ppm 0–50 °C, ±2.5 ppm −40…85 °C | 1.3–5.5 V | C7, footprint-compatible with RV-3028 (same SMD3215-8P; **pin-compat UNVERIFIED**) | [C5127802](https://www.lcsc.com/product-detail/C5127802.html): 3.49 / 2.32, **533 stock** | Extended ([jlcpcb](https://jlcpcb.com/partdetail/C5127802)) |
| PCF85063ATT/AJ (+ external 32.768 kHz crystal) | 220 nA | crystal-limited (~±20 ppm) | 0.9–5.5 V | TSSOP-8 | [C165608](https://www.lcsc.com/product-detail/C165608.html): 0.82 / 0.51, 6,121 stock | UNVERIFIED |
| PCF85063AT/AAZ | 220 nA | crystal-limited | 0.9–5.5 V | SO-8 | [C2157792](https://www.lcsc.com/product-detail/C2157792.html): 0.86 / 0.53, 3,076 stock | UNVERIFIED |
| AB1805-T3 (+ external crystal) | 14 nA RC mode / ~55 nA XT mode | crystal-limited; RC mode is poor | 1.7–3.6 V | QFN-16 3×3 | [C2649355](https://www.lcsc.com/product-detail/C2649355.html): 4.03 / 2.68, 39 stock | UNVERIFIED |
| MCU internal RTC + 32.768 kHz crystal (Epson FC-135, ±20 ppm, 12.5 pF) | Standby+RTC 260–320 nA @3 V vs 68.5 nA without, so **~+190–250 nA** | ±20 ppm ≈ 1.7 s/day ≈ 52 s/month uncalibrated. Smooth calibration has 0.954 ppm resolution (AN4759) but needs per-unit trimming and drifts with temperature. | — | 3.2×1.5 | crystal [C32346](https://www.lcsc.com/product-detail/C32346.html): 0.17 / 0.10, 303k stock | **Basic** ([jlcpcb](https://jlcpcb.com/partdetail/C32346)) |

Sources:
- RV-3028 product page: https://www.microcrystal.com/en/products/real-time-clock-rtc-modules/rv-3028-c7
- RV-3032 product page: https://www.microcrystal.com/en/products/real-time-clock-rtc-modules/rv-3032-c7
- QA = AEC-Q200 automotive, QC = commercial (from the RV-3028 datasheet, via search excerpt; distributor confirmation: https://doveonline.com/products/rv-3028-c7-32-768khz-1ppm-ta-qa.html)
- LSE current and Standby tables: DS14548 Tables 41 and 51
- Smooth calibration: https://www.st.com/content/st_com/en/technical-documents/an4759-introduction-to-using-the-hardware-realtime-clock-rtc-and-the-tamper-management-unit-tamp-for-stm32-mcus-stmicroelectronics_asset/chunk-005-topic-an4759-smooth-dig-calibration.html
- FC-135 crystal: LCSC C32346. **gm-margin with the U0 LSE low-drive setting is UNVERIFIED**; check against AN2867.

**Drift arithmetic (computed, not sourced):** a 30 s TOTP step with ±1 step verifier tolerance tolerates ~30 s of error.
- 20 ppm uncalibrated reaches 30 s in ~17 days.
- 2.5 ppm (RV-3032 over full temperature) reaches 30 s in ~139 days.
- 1 ppm at 25 °C (RV-3028) reaches 30 s in ~347 days, but at 10 °C the RV-3028 runs about −7.9 ppm (−0.035×15²), so 30 s in ~44 days if left cold.
- **IR pairing can resync time**, so any choice needs periodic resync; the RV-3032's temperature compensation buys the longest interval.

**Power comparison on a U0:** Standby without RTC (68.5 nA) + RV-3028 (45 nA) ≈ **115 nA**, versus internal RTC ≈ 260–320 nA. Both are below the CR2450's own self-discharge (<1 %/yr of 540 mAh ≈ 0.6 µA average), so RTC choice is about **accuracy, not battery life**.

**Stock warning:** the RV-3028 TA-QC (C3019759) is **out of stock at LCSC/JLC today**. The RV-3032-C7 (C5127802, 533 in stock) is the drop-in-sized alternative, at +115 nA for temperature compensation.

---

### 5. Battery and holder

| Cell | Capacity | Continuous / pulse | Size | Notes / source |
|---|---|---|---|---|
| CR2450 (Panasonic) | 620 mAh nominal, 0.2 mA standard drain | "high-rate pulse discharge" (no number) | 24.5 × 5.0 mm | https://energy.panasonic.com/dam/master/pdf/en/datasheet/lithium/CR2450_Datasheet_EN.pdf |
| CR2450 (Energizer) | 620 mAh to 2.0 V @ 7.5 kΩ | pulse test 9.0 mA @2.7 V (300 Ω, 2 s × 12/day) | 24.5 × 5.0 | https://dtsheet.com/doc/1429881/energizer-cr2450---energizer-technical-information |
| CR2450N (Renata) | 540 mAh, 0.8 mA standard, **3.0 mA max continuous**; pulse curves at 10/20/50/100 mA × 100 ms in the engineering spec, no max pulse rating | — | 24.5 × 5.0, −40…85 °C, <1 %/yr self-discharge | https://www.leocom.kr/datasheets/51098_engineering.pdf , https://www.mouser.com/datasheet/2/346/DesignersGuide_2011_Coin_Cells-1518723.pdf |
| CR2032 (Energizer) | 235 mAh to 2.0 V @15 kΩ | pulse tests 6.8 mA (400 Ω) and 23 mA 1 ms on / 14 ms off (120 Ω) | 20 × 3.2 | https://data.energizer.com/pdfs/cr2032.pdf |
| CR2477N (Renata) | 950 mAh, 1.0 mA standard, **2.5 mA max continuous** | — | 24.5 × 7.7 | https://res.ameya360.com/91/80/41/3AD9FEC9F6F6F2AFF6D12559BD_A.pdf (Renata TDS mirror) |
| 1× / 2× AAA alkaline | ~1000–1200 mAh (**UNVERIFIED**, not fetched) | Handles 100 mA pulses easily | 10.5 × 44.5 each | 1×AAA needs a boost converter (quiescent µA-level, which dominates sleep); 2×AAA gives 2.0–3.2 V directly with no boost. Much larger than the 76×34 board allows. |

**E-paper peaks (~10–20 mA) on a coin cell:** Renata and Energizer specs rate CR24xx cells at only 2.5–3 mA continuous, and the internal resistance (~10–30 Ω, rising as the cell discharges and at low temperature) gives 0.2–0.6 V sag at 20 mA (**IR range UNVERIFIED**; read it off the pulse curves in the Renata engineering spec). Standard mitigation:
- a **bulk capacitor** of 100–470 µF low-leakage MLCC/tantalum-polymer, or a supercap, across the cell;
- optionally a **series resistor (10–47 Ω) feeding the reservoir cap** so the cell sees a near-DC load;
- keep the MCU BOR at the 1.7 V level so a sag mid-refresh does not reset.

Mind the bulk cap's leakage current: it must be in the nA range or it dominates the sleep budget. The CR2450 is the right size/capacity trade-off. The CR2477 has more capacity, but it is 2.7 mm thicker and has a lower continuous current rating.

#### CR2450 holders

| Part | Mount | LCSC # | Price 1+ / 100+ | Stock | JLC | Notes |
|---|---|---|---|---|---|---|
| **Renata NL5077-LF** (current design) | **Through-hole**, horizontal, ~30.0 × 29.25 × 8.4 mm, wave-solder, positioning pins; PA66 body, Ni contacts; accepts CR2450N and CR2477N | **not found at LCSC/JLC** | ~USD 1.95–2.09 at Future/Farnell | varies | Not stocked at JLC, so hand-solder or global-sourcing | https://www.futureelectronics.com/p/electromechanical--battery-holders-accessories/nl5077-lf-renata-batteries-1207554 , https://ie.farnell.com/renata/nl5077-lf/holder-battery-horizontal-for/dp/1216361 |
| **MYOUNG BS-24-B4AA016** ("BS-2450-2 Au") | SMT horizontal, **gold-plated** phosphor-bronze | [C964809](https://www.lcsc.com/product-detail/C964809.html) | 0.52 / 0.35 | 586 | likely Extended (UNVERIFIED) | Best SMD pick: gold contacts and full base holder |
| MYOUNG BS-24-B4AA014 | SMT, 2-piece LCP cover, gold | [C7525448](https://www.lcsc.com/product-detail/C7525448.html) | 0.57 / 0.35 (108+) | 238 | UNVERIFIED | Covered type |
| MYOUNG MY-2450-01 ("CR2450-W") | SMT reflow clip, Ni-plated | [C964845](https://www.lcsc.com/product-detail/C964845.html) | 0.20 (5+) / 0.16 (150+) | **5,355** | **Extended** ([jlcpcb](https://jlcpcb.com/partdetail/C964845)) | Cheapest and best stocked; clip style, so the PCB pad forms the negative contact |
| MYOUNG MY-2450-01-R | same, tape & reel | [C964846](https://www.lcsc.com/product-detail/C964846.html) | 0.31 / 0.18 | 456 | UNVERIFIED | |
| MYOUNG BS-24-B4AK023 | SMT horizontal, tin-plated | [C2902329](https://www.lcsc.com/product-detail/C2902329.html) | 0.21 | 221 | UNVERIFIED | Tin plating is less ideal for long-term contact |
| TE BAT-HLD-006-SMT | SMD clip, Ni | [C30044112](https://www.lcsc.com/product-detail/C30044112.html) | 1.05 / 0.67 | 335 | UNVERIFIED | Datasheet drawing suggests ~35.8 × 25.5 mm (**UNVERIFIED**, garbled PDF text) |
| Keystone 3008 / 3008TR (24.5 mm SMD retainer) | SMD | C6405306 / C238100 (from search; **LCSC API returns no product**, likely delisted) | ~0.72 (stale) | 0 | — | https://www.digikey.ca/en/products/detail/keystone-electronics/3008/2137861 |
| Keystone 1025-7 | **Through-hole (PC pin)**, not SMD, despite the brief | C238061 (search hit, LCSC API: no product) | ~1.62 (stale) | — | — | https://octopart.com/part/keystone/1025-7 |
| Linx BAT-HLD-024-SMT | — | **not found at LCSC/JLC** | — | — | — | UNVERIFIED |

**Recommendation:** switch from the through-hole NL5077-LF to **MYOUNG BS-24-B4AA016 (C964809)**, an SMT holder with gold contacts that JLC can place. Use **MY-2450-01 (C964845, Extended, 5k stock)** if cost or stock matters more. Neither holder takes a CR2477, unlike the NL5077. Get exact footprints from the EasyEDA footprint on each LCSC page; the MYOUNG PDFs are image-only.
## Navigation switch and IR pairing parts (research 2026-10-29)

**How the data was gathered.** Stock and prices come from the JLCPCB parts-library API (`selectSmtComponentList`), queried 2026-10-29. Prices are in USD at the JLCPCB assembly price. The qty100 figure is the price bracket that contains 100 pcs. LCSC retail prices may differ slightly. In that API, "expand" means **Extended**. JLCPCB charges a per-unique-part loading fee for Extended parts (about $3 per part type per order, UNVERIFIED for the current amount). **Every part below is Extended**, and the only Basic part found is the plain tact switch in the comparison table. Datasheets were downloaded and read with pdftotext, or rendered to an image where the information was only in a drawing.

---

### 1. Navigation switch

#### 1.1 Check of the old part: TE 2434804-1

* It **is a 5-way switch (4 directions plus center push)**. TE's ALCOSWITCH tactile brochure lists it as "2434804-1, 5 position tactile, 7.5x7.5, Surface mount, Extended top actuator, 100K cycles" ([LCSC-hosted TE brochure](https://wmsc.lcsc.com/wmsc/upload/file/pdf/v2/lcsc/2403111547_TE-Connectivity-2434804-1_C17223891.pdf)). TE's product page gives "Single Pole, Five Throw", NO, a stuffer/plunger actuator 3 mm long, 7.5 x 7.5 mm, and an actuator force of 240-360 gf ([te.com](https://www.te.com/en/product-2434804-1.html)). DigiKey describes it as "Navigation Switch, 2-Axis Digital", "4-Way Directional, Center Select", 100k cycles ([DigiKey](https://www.digikey.com/en/products/detail/te-connectivity-alcoswitch-switches/2434804-1/17829539), via search snippet).
* **Problem 1: availability.** LCSC/JLC list it as C17223891, Extended, with **stock 0** ([LCSC](https://www.lcsc.com/product-detail/multi-directional-switches_te-connectivity-2434804-1_C17223891.html)). DigiKey shows 18-21 weeks lead time (search snippet).
* **Problem 2: documentation.** The Mouser "datasheet" link in the brief is only the marketing brochure. It has no drawing or pinout. TE's customer drawing could not be fetched: Mouser timed out and te.com DocumentDelivery returned HTML.
* **Verdict:** the function is correct, but the part is unsuitable because JLCPCB cannot assemble it from stock and no pinout drawing is readily available. Its 7.5 x 7.5 mm size matches the Alps SKRH class, so the replacements below are near drop-in on board area.

#### 1.2 Recommended part and alternatives

| | **Recommended: Alps SKRHABE010** | Alt A: Alps SKRHAAE010 | Alt B: HRO (Korean Hroparts) K1-5202UA-01 | Alt C: XKB TM-4175-B-A |
|---|---|---|---|---|
| Function | 4-direction + center push | same | same | same |
| LCSC # | [C139794](https://www.lcsc.com/product-detail/multi-directional-switches_alpsalpine-skrhabe010_C139794.html) | [C202423](https://www.lcsc.com/product-detail/multi-directional-switches_alpsalpine-skrhaae010_C202423.html) | [C136710](https://www.lcsc.com/product-detail/C136710.html) | [C465995](https://www.lcsc.com/product-detail/C465995.html) |
| Basic/Extended | Extended | Extended | Extended | Extended |
| Price qty1 / qty100 | $0.776 / $0.501 | $0.904 / $0.562 | $0.352 / $0.226 | $0.798 / $0.506 |
| Stock (2026-10-29) | 6,785 | 521 | 364 (siblings: -02 C145900 611 pcs, -03 C145899 284 pcs) | 959 |
| Body W x D | 7.35 x 7.5 mm | 7.35 x 7.5 mm | 7.40 x 7.50 mm | 7.10 x 7.50 mm (8.65 over terminals) |
| Body height / stem top | 1.8 mm body; about 3.8 mm to stem top in the drawing; Alps variety table gives H = 5 mm | same | H = 5.0 mm (-01), 6.0 mm (-02) | H = 5 mm ("A") or 6 mm ("B") per drawing |
| Land pattern | 6 SMD pads + 2 large mounting pads, pad field 8.5 x 9.9 mm, **two guide-boss holes** (ø0.75 and ø1.05) | same pads, **no guide bosses** (pure SMD, no NPTH) | Same 8.5 x 9.9 pad pattern; holes ø1.0 / ø0.7 | Same 8.5 x 9.9 pattern; holes ø1.10 / ø0.85 |
| Actuator | Short square resin stem (ø3), black. 4-dir force is specified at 4.3 mm height, so it expects a keytop | same | ø3 stem ("keystoke") | Square "plum blossom" stem, accepts a keytop |
| Force (4-dir / center) | 1.23 N / 2.35 N (about 125 / 240 gf) | same | 170 ±35 gf lever / 250 gf push (350 and 450 gf options exist; suffix-to-force mapping is UNVERIFIED) | A: 180 ±50 / 250 ±50 gf; B: 250 ±50 / 360 ±70 gf |
| Travel (4-dir / center) | 0.25 / 0.15 mm | same | not stated | 0.25 / 0.15 mm |
| Life | 200,000 cycles each direction (5 mA 5 V) | same | not stated (UNVERIFIED) | 100,000 cycles |
| Rating | 50 mA 12 V DC max / 10 µA 1 V min | same | not stated | 12 V DC 50 mA |
| Datasheet | [Alps SKRH catalog (EN)](https://tech.alpsalpine.com/cms.media/product_catalog_mu_02_skrh_en_34aeb8c2e3.pdf), [Alps product page](https://tech.alpsalpine.com/e/products/detail/SKRHABE010/), [catalog page with drawing and circuit](https://mikrocontroller.ti.bfh.ch/_static/datasheets/mbedApplicationShield/SKRH-1370966.pdf) | same catalog | [LCSC datasheet](https://www.lcsc.com/datasheet/lcsc_datasheet_2108131730_Korean-Hroparts-Elec-K1-5202UA-01_C136710.pdf) | [LCSC datasheet](https://wmsc.lcsc.com/wmsc/upload/file/pdf/v2/lcsc/2110111230_XKB-Connection-TM-4175-B-A_C465995.pdf) |

The Alps 1M-cycle variants are **SKRHADE010** (with bosses: [C202415](https://www.lcsc.com/product-detail/C202415.html), 2,560 in stock, $0.860 / $0.541) and **SKRHACE010** (no bosses: [C209766](https://www.lcsc.com/product-detail/C209766.html), 1,168 in stock, $1.173 / $0.768). Both have a natural-colour stem, are 7.45 x 7.5 x 1.85 mm, and have 100 mΩ contact resistance ([Alps catalog](https://tech.alpsalpine.com/cms.media/product_catalog_mu_02_skrh_en_34aeb8c2e3.pdf)).

Other 7.5 mm 5-way parts in LCSC stock, not examined in detail:
* G-Switch GT-TCA06F-H050-L1 ([C3019725](https://www.lcsc.com/product-detail/C3019725.html), 955 pcs, $0.554 / $0.342, 7.4 x 7.4)
* Hanbo SKLYABE012 ([C47326484](https://www.lcsc.com/product-detail/C47326484.html), 1,131 pcs, $0.342 / $0.262, 6-pin, 100k cycles, 160/260 gf, [datasheet](https://www.lcsc.com/datasheet/lcsc_datasheet_2504101957_Hanbo-Electronic-SKLYABE012_C47326484.pdf))

**Pinout.** It is the same for all four parts above (checked against each drawing):

| Pin | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|
| Function | Dir A | **Center push** | Dir C | Dir B | **Common** | Dir D |

* Sources: the Alps circuit diagram (A-1, Center-2, C-3 / B-4, Common-5, D-6) in the [SKRH catalog page](https://mikrocontroller.ti.bfh.ch/_static/datasheets/mbedApplicationShield/SKRH-1370966.pdf); the HRO K1-5202UA truth table (2-5 push; 1-5, 4-5, 3-5, 6-5 for the four diagonals); the XKB TM-4175 circuit (A-1, Center-2, C-3, B-4, Common-5, D-6).
* **Layout gotcha:** on all of them, directions a/b/c/d point to the **corners** of the square body (45° to the body edges), as drawn by the arrows in the Alps "direction of operation" figure. To have the directions point along the board axes, rotate the footprint 45°, or map the directions in firmware and orient the keycap accordingly.
* Wiring: Common goes to GND. The five direction/center pins go to five MCU GPIOs with internal pull-ups and EXTI wake. The contacts are normally open, so there is **zero static current**.

**Recommendation.** Use **SKRHABE010** (C139794). It is genuine Alps, has the deepest LCSC stock (6.8k), a full English datasheet, 200k cycles, and guide bosses that locate it mechanically for a hand-held device. Its 2 NPTH holes are free at JLCPCB.
* If you want no holes: **SKRHAAE010** (C202423, only 521 in stock), or SKRHACE010 for 1M cycles.
* Budget pin- and footprint-compatible second source: **HRO K1-5202UA-01** (C136710, about 45% of the price). Its footprint is the same 8.5 x 9.9 mm pad pattern, but life and rating are not stated.

**KiCad stock libraries.** There is **no SKRH or other 5-way navigation footprint** in [kicad-footprints `Button_Switch_SMD.pretty`](https://gitlab.com/kicad/libraries/kicad-footprints/-/tree/master/Button_Switch_SMD.pretty). All 171 entries were listed via the GitLab API on 2026-10-29: the only Alps entries are `SW_Push_SPST_NO_Alps_SKRK` and `SW_Push_1P1T-MP_NO_Horizontal_Alps_SKRTLAE010`. The [kicad-symbols `Switch.kicad_symdir`](https://gitlab.com/kicad/libraries/kicad-symbols/-/tree/master/Switch.kicad_symdir) also has no nav or joystick symbol. **Plan on a custom footprint.** Draw it from the Alps land drawing, or import the EasyEDA/LCSC model for C139794 (for example with `easyeda2kicad`). Alps also provides a STEP model: [product_3dcad_skrhabe010_en](https://tech.alpsalpine.com/cms.media/product_3dcad_skrhabe010_en_cddb240b17.zip), path taken from the Alps product page.

---

### 2. IR pairing parts

#### 2.1 Discrete side-looking emitters and detectors

| MPN | Mfr | Type, view | LCSC # | B/E | qty1 / qty100 | Stock | Package | λ | Half-angle | IF max (cont / pulse) | Key optical spec | Datasheet |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **VSMB2948SL** | Vishay | IRED, **side-looking** | [C6604342](https://www.lcsc.com/product-detail/C6604342.html) | E | $0.450 / $0.424 | **176** | 2.3 x 2.55 x 2.3 mm "MiniLED" side view | 940 nm | ±25° | 100 mA / IFSM 500 mA (100 µs) | Ie 20 mW/sr typ (10 min) at 100 mA; tr/tf 15 ns | [PDF](https://wmsc.lcsc.com/wmsc/upload/file/pdf/v2/lcsc/2312131723_Vishay-Intertech-VSMB2948SL_C6604342.pdf) |
| VSMB2943SLX01 | Vishay | IRED, side-looking | [C511094](https://www.lcsc.com/product-detail/C511094.html) | E | $1.002 / $0.626 | **0** | same 2.3 x 2.55 x 2.3 | 940 nm | ±25° | 100 mA / IFM 200 mA (50%, 100 µs) / IFSM 1 A | Ie 20 mW/sr at 100 mA | [PDF](https://www.lcsc.com/datasheet/lcsc_datasheet_2004181706_Vishay-Intertech-VSMB2943SLX01_C511094.pdf) |
| VSMY2943SLX01 | Vishay | IRED, side-looking | [C6925912](https://www.lcsc.com/product-detail/C6925912.html) | E | $1.019 / $0.968 | 10 | same | 940 nm | ±28° (per LCSC listing) | 100 mA | 27 mW/sr at 100 mA (LCSC listing) | [PDF](https://wmsc.lcsc.com/wmsc/upload/file/pdf/v2/lcsc/2312131740_Vishay-Intertech-VSMY2943SLX01_C6925912.pdf) |
| **VEMT2523SLX01** | Vishay | NPN phototransistor, **side-looking** | [C511451](https://www.lcsc.com/product-detail/C511451.html) | E | $0.410 / $0.326 | **1,874** | 2.3 x 2.55 x 2.3 side view | peak 850 nm (470-1090 nm) | ±35° | IC 50 mA, VCEO 20 V | Ica 2.7 mA typ (1.3 min) at Ee = 1 mW/cm², 950 nm; CCEO 25 pF; **tr/tf ≈ 20-40 µs at IC 0.25-0.5 mA, RL = 100 Ω** (Fig. 4) | [PDF](https://wmsc.lcsc.com/wmsc/upload/file/pdf/v2/lcsc/2004181602_Vishay-Intertech-VEMT2523SLX01_C511451.pdf) |
| **IR12-21C/TR8** | Everlight | IRED, **right-angle lens (side-looking)** | [C53672](https://www.lcsc.com/product-detail/C53672.html) | E | $0.086 / $0.067 | 1,269 (the "(PF)" variant [C2992770](https://www.lcsc.com/product-detail/C2992770.html) has 4,144 at $0.046; equivalence is UNVERIFIED) | 3.0 x 2.0 (incl. lens) x 1.0 mm high | 940 nm | 2θ½ = 150° | 65 mA cont (no pulse rating given) | Ie 0.8 mW/sr typ (0.5 min) at 20 mA; VF 1.2 V typ, 1.5 V max | [Everlight PDF](https://en.everlight.com/wp-content/plugins/ItemRelationship/product_files/pdf/IR12-21C-TR8.pdf) |
| **PT12-21C/TR8** | Everlight | NPN phototransistor, same 12-21 package as IR12-21C | [C53674](https://www.lcsc.com/product-detail/C53674.html) | E | $0.070 / $0.055 | **46,327** | 3.0 x 2.0 x 1.0 mm (identical outline) | peak 890 nm (400-1100 nm) | not given | IC 50 mA, VCEO 30 V | Ic(on) 1.14 mA typ (0.30 min) at Ee = 1 mW/cm², VCE 5 V; rise/fall not given | [Everlight PDF Rev 5](https://www.everlight.com.cn/wp-content/plugins/ItemRelationship/product_files/pdf/DPT-0000509_PT12-21C-TR8_Datasheet_Rev5.pdf) |

Notes on these parts:
* **Everlight 12-21 viewing direction.** The IR12-21C datasheet explicitly says "Chip Infrared LED With **Right Angle Lens**". The PT12-21C/TR8 datasheet title says "spherical top view lens", but its package drawing has the same outline and land pattern as IR12-21C: 3.0 x 1.0 mm top outline, dome projecting beyond the 1.1 mm body to 2.0 mm, and the same 0.8/0.65/0.9/0.65/0.8 pads. LCSC's legacy URL for it is `...PT12-21C-TR8-Side-receive_C53674.html` ([search result](https://lcsc.com/product-detail/Infrared-Receivers_PT12-21C-TR8-Side-receive_C53674.html)). Side-looking is therefore highly likely, but this is **inferred from the drawing, not stated in its text**. DigiKey lists PT12-21C/TR8 as obsolete (search snippet), while LCSC still holds 46k.
* **Rejected candidates.**
  * Everlight IR91-21C / PT91-21C (C106897 / C3210912) are **top-view** 1.9 mm round gull-wing parts: "spherical top view lens" in the [IR91 datasheet](https://en.everlight.com/wp-content/plugins/ItemRelationship/product_files/pdf/IR91-21C-TR7.pdf) and the [PT91 datasheet](https://en.everlight.com/wp-content/plugins/ItemRelationship/product_files/pdf/PT91-21C-TR7.pdf).
  * VSMB1940X01 (0805), TEMT7100X01 (0805, 9 in stock, $1.83), OSRAM SFH 4045N (C2900304) and the XINGLIGHT 0603/0805/1206 IR LEDs and phototransistors are all top-emitting chip parts.
  * No XINGLIGHT side-view IR part was found in the JLC library. XL-IR26-21C / XL-PT26-21C are clones of the Everlight 26-21 1206 **top-view** lens family.

#### 2.2 Integrated receivers and transceivers

| MPN | Mfr | What | LCSC # | B/E | qty1 / qty100 | Stock | Package / view | λ | Half-angle | Supply / idle / off current | Data rate | Datasheet |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **TFBS4711-TR1** | Vishay | IrDA SIR transceiver (IRED + PIN + IC) | [C102624](https://www.lcsc.com/product-detail/C102624.html) | E | $6.98 / $4.80 | **45** (TR3 [C3171514](https://www.lcsc.com/product-detail/C3171514.html): 47; TT1 [C506558](https://www.lcsc.com/product-detail/C506558.html): 88, top view) | 6 x 3 x 1.9 mm. TR1/TR3 are taped for **side-view** mounting, TT1 for top view | 880-900 nm | ±24° | 2.4-5.5 V; **idle 70 µA typ (40-100 at 25 °C)**; **SD 0.01 µA typ, 1 µA max**; IRED 300 mA typ with no resistor (200-430) | 9.6-115.2 kbit/s; link 0-0.7 m (Vishay "up to 1 m") | [PDF](https://www.lcsc.com/datasheet/lcsc_datasheet_1811151653_Vishay-Intertech-TFBS4711-TR1_C102624.pdf) |
| TFDU4101-TR3 | Vishay | IrDA SIR transceiver | [C222419](https://www.lcsc.com/product-detail/C222419.html) (TT3 [C52862](https://www.lcsc.com/product-detail/C52862.html): 18,057) | E | $4.59 / $3.17 | 2,308 | 9.7 x 4.7 x 4 mm "universal" (side or top) | IrDA | IrDA | 2.4-5.5 V; 75 µA typ (40-130); SD < 1 µA (< 10 nA at 25 °C) | 115.2 kbit/s, > 1 m | [PDF](https://www.lcsc.com/datasheet/lcsc_datasheet_1806131011_Vishay-Intertech-TFDU4101-TT3_C52862.pdf) |
| TFDU4301-TT1 | Vishay | IrDA SIR, low-power range | [C222418](https://www.lcsc.com/product-detail/C222418.html) | E | $3.08 / $3.00 | 221 | 8.5 x 2.5 x 3.1 mm universal; TT = top-view taping, TR (0 stock) = side | IrDA | IrDA | 2.4-5.5 V; 70 µA typ; SD < 1 µA | 115.2 kbit/s, about 70 cm | [PDF](https://www.lcsc.com/datasheet/lcsc_datasheet_2304140030_Vishay-Intertech-TFDU4301-TT1_C222418.pdf) |
| TSOP75238TR | Vishay | 38 kHz demodulating receiver (AGC2) | [C511498](https://www.lcsc.com/product-detail/C511498.html) | E | $2.13 / $1.35 | 978 | 6.8 x 3.0 x 3.2 mm; **TR = side-view taped**, TT = top | 940 nm band | ±50° | 2.5-5.5 V; **0.35 mA typ (0.27-0.45) at 3.3 V**; no shutdown pin | min burst 10 cycles, gap ≥ 10 cycles, ≤ 1800 short bursts/s, so **about 1-2 kbit/s** | [PDF](https://wmsc.lcsc.com/wmsc/upload/file/pdf/v2/lcsc/2304140030_Vishay-Intertech-TSOP75238TR_C511498.pdf) |
| IRM-H638T/TR2 | Everlight | 38 kHz demodulating receiver | [C91447](https://www.lcsc.com/product-detail/C91447.html) | E | $0.260 / $0.199 | 138,197 | 5 x 7 x 4 mm, **top view** (dome up, per drawing) | 940 nm band | ±45° | **2.7-5.5 V**; ≤ 1.2 mA at 3.0 V | burst ≥ 10 pulses, so about 1-2 kbit/s | [PDF (Espressif mirror)](https://dl.espressif.com/dl/schematics/EVERLIGHT(%E4%BA%BF%E5%85%89)_IRM-H638TTR2.PDF) |
| TSOP38238 | Vishay | 38 kHz receiver, THT | [C141632](https://www.lcsc.com/product-detail/C141632.html) | E | $0.698 / $0.506 | 78,534 | SIP-3 through-hole; not suitable for SMT-only | | ±45° | 0.35 mA | about 1-2 kbit/s | [PDF](https://www.lcsc.com/datasheet/lcsc_datasheet_1809191926_Vishay-Intertech-TSOP38238_C141632.pdf) |

KiCad stock footprints: [`OptoDevice.pretty`](https://gitlab.com/kicad/libraries/kicad-footprints/-/tree/master/OptoDevice.pretty) has `Everlight_IRM-H6xxT` but nothing for the TFBS/TFDU, VSMB/VEMT MiniLED, or Everlight 12-21 parts. `LED_SMD.pretty` has no side-view IR footprint either. Each needs a custom or EasyEDA-imported footprint.

#### 2.3 Link budget, drive current and load resistor

The calculations use inverse-square on-axis irradiance, E = Ie / d², with lens-to-lens distance d.

| Pair | IF | Ie | E at 5 cm | Ic at 5 cm (typ / min) | E at 2 cm | Ic at 2 cm (typ) |
|---|---|---|---|---|---|---|
| VSMB2948SL → VEMT2523SLX01 | 20 mA | about 4 mW/sr (20 mW/sr at 100 mA, scaled linearly; UNVERIFIED, check Fig. 5) | 0.16 mW/cm² | **0.43 mA / 0.21 mA** | 1.0 mW/cm² | 2.7 mA |
| IR12-21C → PT12-21C | 20 mA | 0.8 mW/sr typ (0.5 min) | 0.032 mW/cm² | **36 µA / 6 µA** | 0.2 mW/cm² | 0.23 mA |
| IR12-21C → PT12-21C | 50 mA | about 2 mW/sr (linear scaling, UNVERIFIED) | 0.08 mW/cm² | about 90 µA | 0.5 mW/cm² | about 0.57 mA |
| IR12-21C → VEMT2523 (mixed) | 20 mA | 0.8 mW/sr | 0.032 mW/cm² | 86 µA | 0.2 mW/cm² | 0.54 mA |

Real alignment losses (off-axis, a window or bezel) will cut these numbers. Treat them as an upper bound and leave 3-5x margin.

**Phototransistor speed is the binding limit, not sensitivity.**
* The VEMT2523 datasheet (Fig. 4) shows **tr/tf ≈ 20-40 µs even at RL = 100 Ω** at IC = 0.25-0.5 mA. A larger RL makes it slower: the Miller-multiplied C_CB scales with RL.
* A discrete phototransistor link therefore realistically supports **about 2.4-9.6 kbit/s NRZ UART**, not 115.2 kbit/s. Bench verification is required (UNVERIFIED beyond the datasheet curve).
* 115.2 kbit/s IrDA SIR uses 1.63 µs pulses (3/16 of a bit). Even STM32 IrDA low-power mode uses about 1.4-2.2 µs pulses. A phototransistor cannot resolve these.
* **With a discrete pair, do not use the USART in IrDA mode.** Use plain NRZ UART with TX inverted (USART TXINV), so the line idles with the LED **off**, and receive with a pull-up RL on the collector, which gives active-low inverted logic, or RXINV.
* The pairing payload is tens of bytes, so 9600 baud is under 0.1 s.

**Load resistor (collector to Vdd, emitter to GND, output to a USART RX or comparator input), for the Vishay pair:**
* Size it so that the **minimum** photocurrent at maximum distance pulls the node below VIL. Rule of thumb: RL ≥ 0.75·Vdd / Ic(min).
  * At 5 cm, Ic(min) ≈ 0.2 mA, so RL ≈ 2.2 V / 0.2 mA ≈ **10 kΩ**. Use 4.7 kΩ to favour speed if the range is mostly 2-3 cm.
  * At 2 cm the transistor saturates with 10 kΩ. That is fine logically but adds storage delay.
* Expected rate: about 4.8-9.6 kbit/s with 4.7-10 kΩ (UNVERIFIED, scale from Fig. 4).
* For the Everlight pair at 50 mA, 5 cm needs RL ≈ 22-47 kΩ. Expect about 1-2.4 kbit/s, or limit the range to 2-3 cm.
* An alternative that is faster and less sensitive to RL: RL around 1 kΩ into an analog comparator with a low threshold. Whether the STM32U073 has COMP is UNVERIFIED here; check RM0503 or the DS.

**LED drive from a CR2450:**
* Coin-cell pulse capability is low. Jauch rates a CR2450 at 4 mA continuous and **15 mA maximum pulse** ([Jauch CR2450](https://www.jauch.com/downloadfile/5ef1edcfd29fe456f9abc4030e6824b98/cr2450_jauch.pdf), via search snippet). A Renata-style sheet gives **50 mA pulse for 1 s** and 3 mA continuous ([CR2450CH datasheet](https://mouser.com/datasheet/2/490/CR2450CH_620mAh-3471493.pdf), via search snippet). Energizer's sheet tests only 9 mA pulses ([search snippet](https://dtsheet.com/doc/1429881/energizer-cr2450---energizer-technical-information)).
* **Recommendation: about 20 mA peak LED current**, switched by a small N-MOSFET or NPN low side. Driving straight from a GPIO is not recommended at 20 mA. Add a **47-100 µF bulk capacitor** (ceramic X5R or polymer) on the rail during pairing.
  * With NRZ and TX idle = LED off, the average current while sending is about 50% of peak, so about 10 mA for under 0.5 s. That is within all three coin-cell ratings above.
  * With the Vishay pair, 20 mA already gives about 2x margin at 5 cm.
* **Series resistor** (VF ≈ 1.2-1.35 V at 20 mA, Vbatt 2.6-3.0 V under load, about 0.1 V across the switch): R = (3.0 − 1.3 − 0.1) / 0.020 ≈ **82 Ω**, giving about 16 mA at 2.7 V. Use 68 Ω for about 20 mA at a sagging cell, or 33 Ω for about 45 mA (Everlight pair at 5 cm; check the cell budget).
* **IrDA modules.** The TFBS4711 IRED pulls 200-430 mA with no resistor. For a 2-5 cm link put a series resistor of **tens of Ω** in the IRED anode line (VCC2). Vishay explicitly recommends this for shorter ranges (TFBS4711 note 5). IrDA pulses are short (3/16 duty), so the average current is about 20% of peak while transmitting.
* **Supply floor.** TFBS4711 and TFDU4101 need ≥ 2.4 V. TSOP75238 needs ≥ 2.5 V and IRM-H638T needs ≥ 2.7 V, which a loaded CR2450 near end of life may sag below (discrete parts have no floor).

---

### 3. Comparison tables

#### 3.1 User input options

| Option | Example part (LCSC) | Unit price qty100 | Board area / height | Static current | Pros | Cons |
|---|---|---|---|---|---|---|
| **5-way nav switch** (chosen) | Alps SKRHABE010 (C139794), Extended | $0.50 | about 8.5 x 9.9 mm land, about 3.8-5 mm tall | 0 (NO contacts; 5 GPIOs with pull-ups and EXTI wake) | One control does up/down/left/right/select; 200k cycles; deep stock; second source (HRO K1-5202UA) on the same pattern | Extended-part fee; no KiCad stock footprint; diagonal direction axes; needs a keycap |
| 3 separate tact switches | XKB TS-1187A-B-A-B ([C318884](https://www.lcsc.com/product-detail/C318884.html)), **Basic**, 1.14M stock | 3 x $0.021 = $0.06 | 3 x 5.1 x 5.1 mm, 1.5 mm | 0 | Cheapest, Basic part (no fee), KiCad footprint exists (`SW_Push_1P1T_XKB_TS-1187A`), simple | Only up/down/OK, so menus need more presses; three holes or caps in the enclosure |
| Rotary encoder + push | Alps EC11E18244A5 ([C255515](https://www.lcsc.com/product-detail/C255515.html)), THT | $1.88 | 12 x 13 mm, more than 6.5 mm plus shaft | Can sit with contacts closed at a detent, so the pull-up current flows continuously (e.g. 3 V / 40 kΩ internal pull-up ≈ 75 µA per closed channel, UNVERIFIED for U073 pull-up value) | Fast scrolling | Through-hole and tall; quadrature decode and debounce; static drain risk on a coin cell; poor for a slim fob |

#### 3.2 IR pairing link options

| Option | Example parts | BOM cost qty100 | Current while pairing (RX idle / TX) | Off-state current | Data rate at 2-5 cm | Pros | Cons |
|---|---|---|---|---|---|---|---|
| **Discrete side-looking LED + phototransistor** (recommended) | Vishay VSMB2948SL + VEMT2523SLX01 (about $0.75 per device, both parts on each device); budget: Everlight IR12-21C + PT12-21C (about $0.12) | $0.12-$0.75 | RX: the photocurrent in RL only (dark ≈ 0, lit ≈ 0.2-2.7 mA); TX: about 20 mA peak, about 10 mA average NRZ | 0 with a load switch, or with RL switched by a GPIO | about 2.4-9.6 kbit/s NRZ (PT speed-limited) | Cheapest, true side-looking, no supply floor, ns-fast LED | Slow; must design RL and threshold; VSMB2948SL stock only 176 (VSMB2943SLX01 is out of stock); Everlight pair is weak at 5 cm |
| **IrDA SIR transceiver** | Vishay TFBS4711-TR1 (side) or TFDU4101 | $3.2-$4.8 | RX idle 70 µA typ; TX IRED needs a resistor (300 mA unlimited) | **0.01 µA in SD**, so it can stay on the rail and be shut down via the SD pin | 9.6-115.2 kbit/s; works directly with STM32 USART IrDA mode | Robust, fast, ambient-light-immune, one part, shutdown pin | Costly; TFBS4711 stock very low (45 + 47 + 88); 2.4 V floor; TFDU4101 is large (9.7 x 4.7 x 4) |
| **38 kHz demodulating receiver + IR LED** | Vishay TSOP75238TR (side) + VSMB2948SL; or Everlight IRM-H638T (top view) | $1.35-$1.8 (TSOP) / $0.20 + LED (IRM) | RX 0.35 mA (TSOP) to ≤ 1.2 mA (IRM); TX LED 38 kHz at 50% duty | No SD pin, so it needs a load switch | **about 1-2 kbit/s** (burst ≥ 10 cycles, gap ≥ 10 cycles, ≤ 1800 bursts/s) | Very sensitive, immune to ambient light, digital output | Slowest; needs a 38 kHz carrier from a timer; 2.5-2.7 V floor; IRM-H638T is top view only; large package |

**Bottom line for IRL2FA:**
* **Nav switch:** Alps SKRHABE010 (C139794), with HRO K1-5202UA-01 (C136710) as a pin- and footprint-compatible second source. The TE 2434804-1 is functionally a 5-way switch but is out of stock at LCSC/JLC.
* **IR:** a discrete Vishay side-looking pair, VSMB2948SL emitter plus VEMT2523SLX01 detector, matched 2.3 x 2.55 x 2.3 MiniLED packages. Drive it at about 20 mA through 68-82 Ω and a low-side FET, with RL about 4.7-10 kΩ. Run plain inverted-TX NRZ UART at 4800-9600 baud, and power-gate the whole thing.
* **If the emitter stock (176) is a concern:** use the Everlight IR12-21C emitter (1.3k-4.1k in stock) with the VEMT2523 detector, which gives about 86 µA at 5 cm, 20 mA.
* **If a 115 kbit/s or IrDA-standard link matters more than about $4 of BOM:** use TFBS4711-TR1. Its 10 nA shutdown removes the need for a separate load switch.
# IRL2FA: generic/passive parts (JLCPCB/LCSC) and KiCad 9 library coverage

Researched 2026-10-29.

**Sources.** Stock, Basic/Extended status and prices come from the JLCPCB parts API (`jlcpcb.com/api/overseas-pcb-order/v1/shoppingCart/smtGood/selectSmtComponentList`), queried live. Some parts were cross-checked against the LCSC product-detail page JSON (`https://www.lcsc.com/product-detail/<C#>.html`). Electrical specs come from the PDFs that LCSC links, downloaded to (local download folder, not committed).

**Prices.** All prices are USD per piece.
- "JLC q1" and "JLC q100" are JLCPCB assembly prices. For most parts both quantities fall in the same 1–999 tier, so the two numbers are equal.
- LCSC sells passives with minimum order quantities. For example, C1525 has a 100-piece minimum at $0.0032.
- "Preferred" marks a JLC "Preferred Extended" part. These carry no extended-part loading fee.

**Stock.** All stock figures are JLCPCB stock today.

**Datasheet links.** Datasheet URLs have the form `https://www.lcsc.com/datasheet/lcsc_datasheet_..._<C#>.pdf`, as returned by the JLC API. That link opens an HTML viewer. LCSC's direct PDF is at `https://datasheet.lcsc.com/datasheet/pdf/<hash>.pdf?productCode=<C#>`.

**Codes from the brief.** All six "commonly known" codes were checked and are correct and Basic: C1525, C52923, C15525, C25744, C20917 and C15127. One correction: C15525 is a **10 µF 0402** (6.3 V), not a 0603. The 10 µF 0603 Basic part is C19702.

---

## Part A: parts

### A1. MLCCs

| Role | LCSC # | MPN (mfr) | Spec | Type | Stock | JLC q1 / q100 | Datasheet |
|---|---|---|---|---|---|---|---|
| 100 nF 0402 decoupling | **C1525** | CL05B104KO5NNNC (Samsung) | 16 V X7R ±10 % | **Basic** | 32.0 M | 0.0031 / 0.0031 (LCSC: MOQ 100 @ 0.0032) | https://www.lcsc.com/datasheet/lcsc_datasheet_2304140030_Samsung-Electro-Mechanics-CL05B104KO5NNNC_C1525.pdf |
| 1 µF 0402 | **C52923** | CL05A105KA5NQNC (Samsung) | **25 V** X5R ±10 % | **Basic** | 10.5 M | 0.0064 / 0.0064 | https://www.lcsc.com/datasheet/lcsc_datasheet_2304140030_Samsung-Electro-Mechanics-CL05A105KA5NQNC_C52923.pdf |
| 4.7 µF 0402 | **C23733** | CL05A475MP5NRNC (Samsung) | 10 V X5R ±20 % | **Basic** | 2.42 M | 0.0151 / 0.0151 | https://www.lcsc.com/datasheet/lcsc_datasheet_2304140030_Samsung-Electro-Mechanics-CL05A475MP5NRNC_C23733.pdf |
| 4.7 µF 0603 | **C19666** | CL10A475KO8NNNC (Samsung) | 16 V X5R ±10 % | **Basic** | 3.00 M | 0.0169 / 0.0128 | https://www.lcsc.com/datasheet/lcsc_datasheet_2304140030_Samsung-Electro-Mechanics-CL10A475KO8NNNC_C19666.pdf |
| 10 µF 0402 | **C15525** | CL05A106MQ5NUNC (Samsung) | 6.3 V X5R ±20 % | **Basic** | 10.2 M | 0.0258 / 0.0258 | https://www.lcsc.com/datasheet/lcsc_datasheet_2208231630_Samsung-Electro-Mechanics-CL05A106MQ5NUNC_C15525.pdf |
| 10 µF 0603 | **C19702** | CL10A106KP8NNNC (Samsung) | 10 V X5R ±10 % | **Basic** | 12.1 M | 0.0212 / 0.0212 | https://www.lcsc.com/datasheet/lcsc_datasheet_2304140030_Samsung-Electro-Mechanics-CL10A106KP8NNNC_C19702.pdf |
| 1 µF ≥25 V 0603 (e-paper booster) | **C15849** | CL10A105KB8NNNC (Samsung) | **50 V** X5R ±10 % | **Basic** | 12.7 M | 0.014 / 0.014 | https://www.lcsc.com/datasheet/lcsc_datasheet_2304140030_Samsung-Electro-Mechanics-CL10A105KB8NNNC_C15849.pdf |
| (alt.) 1 µF 25 V 0402 | C52923 | as above | 25 V | Basic | | | The 0402 at 25 V loses a lot of capacitance under DC bias at the ±15–20 V booster rails, so the 0603 50 V part is the better choice. Neither part's bias curve has been checked (UNVERIFIED). |
| 10 nF 0402 | **C15195** | CL05B103KB5NNNC (Samsung) | 50 V X7R ±10 % | **Basic** | 6.25 M | 0.0039 / 0.0039 | https://www.lcsc.com/datasheet/lcsc_datasheet_2304140030_Samsung-Electro-Mechanics-CL05B103KB5NNNC_C15195.pdf |
| 4.7 µF 25 V 0805 | **C1779** | CL21A475KAQNNNE (Samsung) | 25 V X5R ±10 % | **Basic** | 1.88 M | 0.0343 / 0.0343 | https://www.lcsc.com/datasheet/lcsc_datasheet_2304140030_Samsung-Electro-Mechanics-CL21A475KAQNNNE_C1779.pdf |
| 4.7 µF 25 V 0603 | C69335 | CL10A475KA8NQNC (Samsung) | 25 V X5R ±10 % | Extended | 920 k | 0.0428 / 0.0312 | https://www.lcsc.com/datasheet/lcsc_datasheet_1810261513_Samsung-Electro-Mechanics-CL10A475KA8NQNC_C69335.pdf |

### A2. Bulk capacitors for coin-cell pulse buffering

| LCSC # | MPN | Spec | Type | Stock | JLC q1 / q100 | Leakage (datasheet max) |
|---|---|---|---|---|---|---|
| **C59461** | CL10A226MQ8NRNC | 22 µF 6.3 V X5R **0603** | **Basic** | 3.09 M | 0.0313 / 0.0313 | IR ≥ 100 MΩ·µF, i.e. ≥ 4.5 MΩ, i.e. **≤ 0.66 µA @ 3 V** |
| C86295 | CL10A226MP8NUNE | 22 µF **10 V** X5R 0603 | Extended | 1.25 M | 0.0328 / 0.0248 | same rule |
| C29277 | CL21A226MPQNNNE | 22 µF 10 V X5R 0805 | Extended | 527 k | 0.0443 / 0.0362 | same rule |
| **C45783** | CL21A226MAQNNNE | 22 µF **25 V** X5R 0805 | **Basic** | 3.82 M | 0.1918 / 0.1918 | IR ≥ 10 GΩ or 500 MΩ·µF, i.e. ≥ 22.7 MΩ, i.e. ≤ 0.13 µA @ 3 V (25 V rating, so the 500 MΩ·µF rule applies) |
| **C16780** | CL21A476MQYNNNE | 47 µF 6.3 V X5R 0805 | **Basic** | 1.05 M (LCSC 306 k) | 0.1019 / 0.0798 | 100 MΩ·µF, i.e. ≥ 2.1 MΩ, i.e. **≤ 1.4 µA @ 3 V** |
| **C15008** | CL31A107MQHNNNE | 100 µF 6.3 V X5R 1206 | **Basic** | 1.80 M | 0.1091 / 0.086 | 100 MΩ·µF, i.e. ≥ 1 MΩ, i.e. ≤ 3 µA @ 3 V |
| C116740 | T520B107M006ATE040 (KEMET polymer tantalum) | 100 µF 6.3 V, B case | Extended | 5 078 | 0.5585 / 0.3554 | **DCL 63 µA max @ 25 °C** (0.1 CV, T520 table row "T520B107M006A(1)E0xx … 63") |
| **C16133** | TAJB107K006RNJ (Kyocera AVX MnO2 tantalum) | 100 µF 6.3 V, B case | **Basic** | 746 k | 0.2762 / 0.19 | **DCL 6.3 µA max @ 25 °C** (0.01 CV), 63 µA at 85 °C (10 × IL, category table) |
| EDLC, e.g. Seiko CPH3225A (11 mF) | not stocked at JLC/LCSC | | | | | ESR is about 160 Ω and maximum discharge is 10 µA (Seiko spec, via search snippet). It cannot buffer mA pulses. No leakage figure found (UNVERIFIED). |

**Where the leakage figures come from**

- **Samsung MLCC IR.** From the Samsung CL-series reliability spec (in the C16780 PDF): "Insulation Resistance 10,000 MΩ min. or 500 MΩ·µF min. (or *100 MΩ·µF) product whichever is smaller… (Rated voltage ≤16 V: 10,000 MΩ min. or 100 MΩ·µF…)". Source: https://datasheet.lcsc.com/datasheet/pdf/02336ea48ea44ca18c72517dd3cb7b47.pdf?productCode=C16780.
  - These are guaranteed minimums, measured after 60–120 s at rated voltage.
  - Typical leakage of X5R MLCCs at 3 V is in the nA range. This is general knowledge; no datasheet number was found for it (UNVERIFIED).
- **KEMET T520.** "DC Leakage µA at 25 °C, Maximum/5 Minutes" = 63 for every T520B107M006 variant. Source: https://datasheet.lcsc.com/datasheet/pdf/7f51ccb8cd6347b8932a49fbb272f93b.pdf?productCode=C116740.
  - Panasonic POSCAP (TPE/TPB) is also specified at 0.1 CV. This is from memory and was not re-fetched (UNVERIFIED).
- **AVX TAJ.** The TAJB107*006 row gives DCL 6.3 µA. "DCL is measured at rated voltage after 5 minutes"; the 85 °C limit is 10 × IL. Source: https://datasheets.kyocera-avx.com/TAJ.pdf.
  - Leakage at 3 V on a 6.3 V part is typically far below the rated-voltage limit (UNVERIFIED).

**Recommendation:** use MLCCs.

- **Parts:**
  - **2 × C16780 (47 µF 0805, Basic)**, or
  - **4 × C45783 / C29277 (22 µF 0805)**.
- **Usable capacitance:** after DC-bias derating at 3 V, roughly 35–50 µF effective. This is a typical X5R derating estimate; the Samsung bias curve was not checked (UNVERIFIED). If more is needed, add C15008 (100 µF 1206, Basic).
- **Leakage:**
  - **Worst-case spec:** sub-µA to about 1.4 µA per part.
  - **Typical:** nA.
  - **Tantalum polymer:** up to 63 µA, about 10–100 times the whole sleep budget. Avoid.
  - **MnO2 TAJ:** 6.3 µA max, which is also too high for a worst-case design.
- **Rating:** a higher voltage rating (10 V or 25 V) gives better DC-bias retention and a tighter IR spec, at a higher price.

### A3. 0402 resistors

| Value | LCSC # | MPN (UNI-ROYAL) | Tol / TCR | Type | Stock | JLC q1 / q100 |
|---|---|---|---|---|---|---|
| 10 k | **C25744** | 0402WGF1002TCE | 1 % | **Basic** | 17.2 M | 0.0024 / 0.0024 |
| 100 k | **C25741** | 0402WGF1003TCE | 1 % | **Basic** | 8.56 M | 0.0012 / 0.0012 |
| 1 M | **C26083** | 0402WGF1004TCE | 1 % | **Basic** | 3.01 M | 0.0015 / 0.0015 |
| 100 Ω (IR LED) | **C25076** | 0402WGF1000TCE | 1 % | **Basic** | 6.18 M | 0.0012 / 0.0012 |
| 47 Ω (IR LED) | C25118 | 0402WGF470JTCE | 1 % | Extended **(Preferred)** | 1.78 M | 0.0012 / 0.0012 |
| 68 Ω | C25131 | 0402WGF680JTCE | 1 % | Extended | 188 k | 0.0011 / 0.0011 |
| 2.2 Ω (booster sense) | C25101 | 0402WGF220KTCE | 1 %, ±200 ppm | Extended | 3.64 M | 0.0015 / 0.0015 |
| 3 Ω (booster sense) | C423196 | 0402WGF300KTCE | 1 %, ±400 ppm | Extended | 24 k | 0.0018 / 0.0018 |
| 0.47 Ω (booster sense) | C423162 | 0402WGF470LTCE | 1 %, ±800 ppm | Extended | 27.5 k | 0.0047 / 0.0047 |

Datasheets:
- C25744: https://www.lcsc.com/datasheet/lcsc_datasheet_2411221126_UNI-ROYAL-Uniroyal-Elec-0402WGF1002TCE_C25744.pdf
- C25741: https://www.lcsc.com/datasheet/lcsc_datasheet_2206010100_UNI-ROYAL-Uniroyal-Elec-0402WGF1003TCE_C25741.pdf
- C26083: https://www.lcsc.com/datasheet/lcsc_datasheet_2206010030_UNI-ROYAL-Uniroyal-Elec-0402WGF1004TCE_C26083.pdf
- C25076: https://www.lcsc.com/datasheet/lcsc_datasheet_2205311900_UNI-ROYAL-Uniroyal-Elec-0402WGF1000TCE_C25076.pdf
- For the others, use `https://www.lcsc.com/product-detail/<C#>.html`.

UNI-ROYAL codes are easy to misread. In this series, "220K" means 2.2 Ω, "470K" means 4.7 Ω and "470L" means 0.47 Ω. Low-ohm 0402 parts are Extended only; no Basic part below 10 Ω was found in these searches.

### A4. 10 µH inductor for the e-paper booster

| LCSC # | MPN | Size | Rating (JLC fields) | Type | Stock | JLC q1 / q100 | Datasheet |
|---|---|---|---|---|---|---|---|
| **C37428** | Sunlord **SWPA252012S100MT** | 2520 (1008), shielded | 10 µH, DCR 690 mΩ, 620 mA / 880 mA (Irms/Isat order per Sunlord datasheet, UNVERIFIED) | Extended | 50.2 k | 0.0456 / 0.0456 | https://www.lcsc.com/datasheet/lcsc_datasheet_2310251550_Sunlord-SWPA252012S100MT_C37428.pdf |
| C89132 | Sunlord WPN201610H100MT | 2016 | 10 µH, 826 mΩ, 650 / 800 mA | Extended | 17.0 k | 0.0418 / 0.0339 | https://wmsc.lcsc.com/wmsc/upload/file/pdf/v2/lcsc/2310241732_Sunlord-WPN201610H100MT_C89132.pdf |
| C307852 | TDK VLS201610HBX-100M-1 | 2016 | 10 µH, 640 mΩ, 610 mA | Extended | 6.2 k | 0.0662 / 0.0529 | (JLC listing) |
| C408339 | Sunlord MWSA0402S-100MT | 4.4 × 4.2 mm | 10 µH, 282 mΩ, 1.6 / 2 A | Extended | 33.4 k | 0.1356 / 0.1126 | (JLC listing) |
| (ref) Sumida CDRH2D18/HPNP-100NC | 3 × 3 mm | | | not found at JLC/LCSC. The listed CDRH2D18 parts are 2.2/3.3/4.7 µH only. A Central Semi CTCDRH2D18F-100N (C17262080) clone shows 0 stock. | | | |

There is **no Basic 10 µH power inductor**. The Basic 10 µH parts, C1035 (0603, 3 mA) and C1046 (0805, 15 mA), are multilayer signal inductors and are unusable here. **Recommendation:** C37428 SWPA252012S100MT. KiCad has a matching footprint, `Inductor_SMD:L_Sunlord_SWPA252012S`.

### A5. Schottky diodes for the booster

| LCSC # | MPN (mfr) | Pkg | VR | IR max (datasheet) | Type | Stock | JLC q1 / q100 |
|---|---|---|---|---|---|---|---|
| **C8598** | B5819W SL (JSCJ / Changjing) | SOD-123 | 40 V | **1 mA @ 40 V** | **Basic** (confirmed) | 574 k | 0.0263 / 0.0263 |
| C82046 | MBR0530T1G (onsemi) | SOD-123 | 30 V | 130 µA @ 30 V, **20 µA @ 15 V** (25 °C) | Extended | 246 k | 0.0629 / 0.0498 |
| C77336 | MBR0530 (JSCJ) | SOD-123 | 30 V | 80 µA @ 30 V (sic, as printed in datasheet) | Extended | 52 k | 0.039 / 0.039 |
| C8590 | BAT54 (JSCJ) | SOT-23 | 30 V | **2 µA @ 25 V** | Extended | 6.5 k | 0.0266 / 0.0266 |
| **C19726** | BAT54SLT1G (onsemi, series pair) | SOT-23 | 30 V | 0.5 typ / **2.0 µA max @ 25 V** | Extended | 320 k | 0.0238 / 0.0132 |
| C8592 | BAT54S KL4 (JSCJ) | SOT-23 | 30 V | 2 µA @ 25 V | Extended | 260 k | 0.024 / 0.024 |

Datasheets:
- C8598: https://datasheet.lcsc.com/datasheet/pdf/4ac2c059be7c462694ab0715ce987a85.pdf?productCode=C8598
- C82046: https://datasheet.lcsc.com/datasheet/pdf/16accd9fe4069ab28e2c37a02635df6a.pdf?productCode=C82046
- C19726: https://datasheet.lcsc.com/datasheet/pdf/46cb550ea5604e38b1c44b1ec0935405.pdf?productCode=C19726

Notes:
- **Booster current.** The booster peak current (about 100–300 mA) is within the BAT54 ratings: 200 mA IF and 600 mA IFSM.
- **Avoid B5819W.** It is a 1 A part with up to mA-level leakage, and Schottky reverse leakage roughly doubles every 10 °C.
- **Recommended parts.** Use **BAT54S (C19726)** in place of the two series diodes on the VGL side, plus a single BAT54 for the VGH side. If the reference-design current is a concern, use MBR0530T1G instead.
- **Static leakage path.** When the booster is idle, VBAT → inductor → diode forms a DC path into the PREVGH capacitor. This happens because the inductor connects to VCI. Leakage through that path, and through the panel driver, therefore matters. Power-gating the booster inductor feed, or the whole panel VCI, with a P-FET load switch removes the issue.

### A6. N-MOSFET for the booster GDR

| LCSC # | MPN | Pkg | VGS(th) min/typ/max | RDS(on) @2.5 V max | Ciss | IGSS | IDSS @25 °C | Type | Stock | JLC q1 / q100 |
|---|---|---|---|---|---|---|---|---|---|---|
| **C469327** | **Si1308EDL-T1-GE3** (Vishay) | SOT-323 (SC-70) | 0.6 / – / 1.5 V | 185 mΩ | **105 pF** | ±20 µA max @ ±12 V; 1 µA @ 4.5 V (ESD-protected gate) | 1 µA @ 30 V | Extended | 34.7 k | 0.2699 / 0.2139 |
| **C20917** | AO3400A (AOS) | SOT-23 | 0.65 / 1.05 / 1.45 V | 48 mΩ | **630 pF**, Qg 6 nC @ 4.5 V | 100 nA @ ±12 V | 1 µA @ 30 V (5 µA @ 55 °C) | **Basic** | 1.15 M | 0.089 / 0.0718 |
| C8545 | 2N7002 (JSCJ) | SOT-23 | VGS(th) up to 2.5 V | – (5 Ω @ 10 V) | 25 pF | | | **Basic** | 3.09 M | 0.0161 / 0.0161 |

Datasheets:
- Si1308: https://datasheet.lcsc.com/datasheet/pdf/a865757ba512c18824d75705c123f47e.pdf?productCode=C469327
- AO3400A: https://datasheet.lcsc.com/datasheet/pdf/130876b936da464599428c58fd8de8f6.pdf?productCode=C20917

Notes:
- **Si1308EDL** is the part used in the Good Display/PDi reference circuits. Its gate is light, at Ciss 105 pF, which suits the weak GDR output of the SSD1680-class controller.
- **AO3400A** has six times the Ciss. Driven at a few hundred kHz from GDR it switches more slowly, which costs efficiency. It probably still works, but this has not been tested (UNVERIFIED).
- **2N7002** has a maximum VGS(th) of 2.5 V, which is marginal when GDR swings only about VCI (2.5–3 V). Do not use it.

### A7. P-MOSFETs for reverse-polarity protection and load switches

| LCSC # | MPN | VGS(th) | RDS(on) @ −2.5 V max | IGSS max | IDSS max @25 °C | Ciss | Type | Stock | JLC q1 / q100 |
|---|---|---|---|---|---|---|---|---|---|
| **C15127** | **AO3401A** (AOS) | −0.5 / −0.9 / −1.3 V | 85 mΩ | ±100 nA @ ±12 V | −1 µA @ −30 V | 645 pF | **Basic** | 949 k | 0.096 / 0.0783 |
| **C10487** | Si2301CDS-T1-GE3 (Vishay) | −0.4 / – / −1.0 V | 142 mΩ | ±100 nA @ ±8 V | −1 µA @ −20 V (−10 µA @ 55 °C) | 405 pF | **Basic** | 115 k | 0.0901 / 0.0729 |
| C150470 | DMG2305UX-7 (Diodes) | −0.5 / – / −0.9 V | 100 mΩ (200 mΩ @ −1.8 V) | ±100 nA @ ±8 V | −1 µA @ −20 V | 808 pF | Extended | 7.1 k | 0.5585 / 0.3718 |
| C110499 | DMP2035U-7 (Diodes) | −0.4 / −0.7 / −1.0 V | 45 mΩ | **±10 µA** @ ±8 V | −1 µA @ −20 V | 1610 pF | Extended | 272 k | 0.1337 / 0.1076 |

Datasheets:
- AO3401A: https://datasheet.lcsc.com/datasheet/pdf/fee353dd1e9e0bc90b295f14f381aa4c.pdf?productCode=C15127
- Si2301CDS: https://datasheet.lcsc.com/datasheet/pdf/f80dd06337c57ccfc2bd2185d147b710.pdf?productCode=C10487
- DMG2305UX: https://datasheet.lcsc.com/datasheet/pdf/47e4eca1ed8982a4ce6cf0c3e9eb9c9f.pdf?productCode=C150470
- DMP2035U: https://datasheet.lcsc.com/datasheet/pdf/b8be3723f2a938ff1161496d64a5a119.pdf?productCode=C110499

**Recommendation: AO3401A (Basic)** for both the reverse-polarity FET and the load switches.

- **Gate leakage drains the battery all the time.** In the reverse-polarity FET the gate is tied to GND with VGS ≈ −3 V permanently, so IGSS is a continuous battery drain.
  - AO3401A, Si2301 and DMG2305UX all specify ≤100 nA at full VGS rating, and leak far less at 3 V.
  - DMP2035U specifies 10 µA max. It has the best RDS(on) but should be avoided for this reason.
- **Pricing:** DMG2305UX-7 (genuine Diodes) is expensive at JLC and adds nothing over AO3401A here.
- **Clones:** there are many unbranded "DMG2305UX" / "DMP2035U" clones (TECH PUBLIC, HXY, ElecSuper…). Their specs differ, so do not substitute them blindly.

### A8. FPC connector (24-pin, 0.5 mm pitch)

| LCSC # | MPN | Contact / lock | Type | JLC stock | LCSC stock | Price q1 / q100 |
|---|---|---|---|---|---|---|
| C122434 | **Molex 503480-2400** | dual-contact (top+bottom), back-flip | Extended | **0** | **0** | LCSC 1.2566 / 0.7956 |
| **C2856805** | XUNPU FPC-05F-24PH20 | bottom contact, flip (hinged lid), H 2.0 mm | Extended | 99.0 k | 99.0 k | 0.1104 / 0.0882 |
| C262669 | JUSHUO AFC01-S24FCA-00 | bottom contact, hinged lid, H 2.0 mm | Extended | 39.0 k | 39.0 k | 0.1209 / 0.0981 |
| C262567 | JUSHUO AFC07-S24FCA-00 | bottom contact, LCSC says "slide lock" | Extended | 47.5 k | 47.5 k | 0.1312 / 0.1073 |
| C5373439 | SHOU HAN 0.5-24P XJH2.0 043 | bottom contact, flip-top | Extended | 12.0 k | – | 0.0844 / 0.0665 |

Datasheet links:
- Molex: https://datasheet.lcsc.com/datasheet/pdf/1eecc71786af35e5eb0b5deed492beaf.pdf?productCode=C122434
- XUNPU: https://datasheet.lcsc.com/datasheet/pdf/0ee18373cdadd5e6c8c1fa51e58ba102.pdf?productCode=C2856805
- AFC01: https://datasheet.lcsc.com/datasheet/pdf/97e3ba016a78286ffa6cfbc591e2beb6.pdf?productCode=C262669

**The Molex 503480-2400 is out of stock at both JLC and LCSC today.** JLC would need it consigned or sourced through global sourcing.

**Recommendation:** XUNPU FPC-05F-24PH20 (C2856805) or JUSHUO AFC01-S24FCA-00 (C262669). Both are bottom-contact flip-lock, the type used on most Good Display adapter boards.
- Check contact side against the panel's FPC: contacts must face the PCB.
- Land patterns differ between vendors. The Molex footprint is not interchangeable with these (UNVERIFIED in detail).

### A9. Coin-cell holder

- **Renata NL5077-LF:**
  - **Not on LCSC or JLC.** The JLC keyword search returned no hits.
  - **What it is:** a horizontal **through-hole** CR2450N/CR2477N holder, PA66 body, about 30 × 29.25 × 8.4 mm.
  - **Where to buy:**
    - Farnell 1216361: https://ie.farnell.com/renata/nl5077-lf/holder-battery-horizontal-for/dp/1216361
    - Future Electronics, about $2.09 for one: https://www.futureelectronics.com/p/electromechanical--battery-holders-accessories/nl5077-lf-renata-batteries-8142267
    - Octopart: https://octopart.com/part/renata/NL5077-LF
  - Mouser stock was not checked (UNVERIFIED).
- **Renata parts that JLC does stock:**

| JLC # | MPN | Mount | Type | JLC stock | JLC q1 |
|---|---|---|---|---|---|
| C5443913 | SMTU2450N-LF | SMD | Extended | 284 | 0.5994 |
| C6060329 | HU2450N-LF | THT | Extended | 1 012 | 0.6617 |

These two did not resolve on lcsc.com product-detail pages ("not found" in page JSON), so they may be JLC global-sourcing items. LCSC availability is UNVERIFIED.

### A10. RV-3028-C7

| LCSC # | MPN | Type | JLC stock | LCSC stock | Price q1 / q100 |
|---|---|---|---|---|---|
| **C3019759** | RV-3028-C7-32.768kHz-1ppm-TA-QC | Extended | **0** | **0** | 2.4936 / 1.6256 |
| C3304278 | RV-3028-C7 …-TA-QA | Extended | 0 | 0 | 1.9342 / 1.8339 |

The listed LCSC package is "SMD3215-8P" / DFN-8 1.5 × 3.2. Datasheet: https://www.microcrystal.com/fileadmin/Media/Products/RTC/Datasheet/RV-3028-C7.pdf.

**Both RV-3028-C7 codes are out of stock today.** Plan on consigning parts or using JLC global sourcing.

---

## Part B: KiCad 9 stock libraries

The repos have **no `9.0` branch**. KiCad 9 releases are tags, so I checked the latest one, **`9.0.9.1`**. The footprint directories were listed through the GitLab tree API, paginated. The symbol files were fetched raw and searched for symbol names.

| Item | Found in KiCad 9.0.9.1? | Exact name(s) |
|---|---|---|
| STM32U073 symbol | **Yes** | `MCU_ST_STM32U0:STM32U073KCUx`, along with K8Ux, KBUx and the whole U031/U073/U083 family. The default Footprint is `Package_DFN_QFN:QFN-32-1EP_5x5mm_P0.5mm_EP3.45x3.45mm` and the filter is `QFN*1EP*5x5mm*P0.5mm*`. Datasheet: https://www.st.com/resource/en/datasheet/stm32u073kc.pdf |
| QFN-32 5×5 | **Yes** | `Package_DFN_QFN:QFN-32-1EP_5x5mm_P0.5mm_EP3.45x3.45mm` (+ `_ThermalVias`). There is also `Package_DFN_QFN:UFQFPN-32-1EP_5x5mm_P0.5mm_EP3.5x3.5mm` (+ `_ThermalVias`), plus EP 3.1/3.3/3.6/3.65/3.7 variants. Check the EP size against ST's UFQFPN32 drawing (D2/E2). ST usually specifies 3.5 mm, which would make the UFQFPN-32 EP3.5 footprint a better match than the symbol's default (UNVERIFIED). |
| RV-3028-C7 symbol | **Yes** | `Timer_RTC:RV-3028-C7`. Default Footprint `Package_SON:MicroCrystal_C7_SON-8_1.5x3.2mm_P0.9mm`, filter `MicroCrystal*C7*1.5x3.2mm*` |
| RV-3028-C7 footprint | **Yes** | `Package_SON:MicroCrystal_C7_SON-8_1.5x3.2mm_P0.9mm` |
| Tag-Connect TC2030 footprints | **Yes** | `Connector:Tag-Connect_TC2030-IDC-NL_2x03_P1.27mm_Vertical` (no legs) and `Connector:Tag-Connect_TC2030-IDC-FP_2x03_P1.27mm_Vertical` (legged) |
| Tag-Connect TC2030 symbols | **Yes** | `Connector:Conn_ARM_SWD_TagConnect_TC2030` (default FP: the -FP footprint) and `Connector:Conn_ARM_SWD_TagConnect_TC2030-NL` (extends it, default FP: the -NL footprint) |
| Molex 503480-2400 FPC | **No** | Not present in 9.0.9.1 or `master`. The nearest 24P 0.5 mm parts are listed below this table. |
| CR2450 / Renata holder | **Partial** | No Renata 2450 and no NL5077 footprint, in 9.0.9.1 or master. The only Renata footprint is `Battery:BatteryHolder_Renata_SMTU2032-LF_1x2032`. The CR2450 holders that are present are listed below this table. |
| 5-way navigation switch | **No** | `Button_Switch_SMD` (170 footprints) has no nav, joystick or multi-direction switch: no Alps SKRH/SKQU, C&K, or anything similar. The same is true on master. `Switch.kicad_sym` has no 5-way nav symbol either. The nearest are `SW_Push*` and `SW_Rotary_1x5_MP`. A custom symbol and footprint are needed. |
| 10 µH inductor | **Yes** | `Inductor_SMD:L_Sunlord_SWPA252012S` (matches C37428), `L_Sunlord_SWPA252010S`, `L_1008_2520Metric`, `L_0806…` variants. CDRH2D18: no Sumida CDRH2D18 footprint found. |
| SOT-23 / SOT-323 / SOD-123 / SOD-323 | **Yes** | `Package_TO_SOT_SMD:SOT-23`, `SOT-23-3`, `SOT-323_SC-70`; `Diode_SMD:D_SOD-123`, `D_SOD-323`, `D_SOD-523` |
| 0402 / 0603 / 0805 / 1206 passives | **Yes** | `Capacitor_SMD:C_0402_1005Metric` … `C_1206_3216Metric`; `Resistor_SMD:R_0402_1005Metric` (standard) |

Nearest stock footprints for the 24P 0.5 mm FPC:
- `Connector_FFC-FPC:Molex_502244-2430_1x24-1MP_P0.5mm_Horizontal`
- `Connector_FFC-FPC:Hirose_FH12-24S-0.5SH_1x24-1MP_P0.50mm_Horizontal`
- `Connector_FFC-FPC:Jushuo_AFC07-S24FCA-00_1x24-1MP_P0.50_Horizontal`. This matches C262567 in stock.
- `Connector_FFC-FPC:TE_2-1734839-4_1x24-1MP_P0.5mm_Horizontal`
- `Amphenol_F32Q/F32R-1A7x1-11024_1x24-1MP_P0.5mm_Horizontal`

CR2450 holder footprints in `Battery`:
- `Battery:BatteryHolder_Keystone_3008_1x2450`
- `Battery:BatteryHolder_Keystone_3009_1x2450`
- `Battery:BatteryHolder_ComfortableElectronic_CH273-2450_1x2450`
- `Battery:Battery_Panasonic_CR2450-VAN_Vertical_CircularHoles`

**Project custom library.** `hardware/fp-lib-table` and `hardware/sym-lib-table` point to `${KIPRJMOD}/footprints/irl2fa.pretty` and `${KIPRJMOD}/symbols/irl2fa.kicad_sym`. **Neither path exists in the repo today.** `hardware/` contains only `fp-lib-table`, `irl2fa.kicad_pro`, `LICENSE` and `sym-lib-table`. So there are no custom footprints yet. These would need to be created:
- the Molex 503480-2400 footprint, or one for an in-stock alternative
- the Renata NL5077-LF footprint
- the 5-way nav switch symbol and footprint

Raw listings are in (local download folder, not committed):
- footprint directory listings: `fp_*.txt` (9.0.9.1) and `master_*.txt`
- symbol files: `sym_*.kicad_sym`
