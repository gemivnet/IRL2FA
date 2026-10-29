# IRL2FA KiCad library

Project-local KiCad 9 library: `irl2fa.pretty/` (footprints), `irl2fa.3dshapes/`
(STEP models for them), `irl2fa.kicad_sym` (symbols) and `kicad.3dshapes/` (stock
KiCad STEP models copied so renders work without a full KiCad install).

Regenerate the footprints and models with:

    python3 hardware/tools/gen_footprints.py      # -> irl2fa.pretty/
    python3 hardware/tools/gen_models.py          # -> irl2fa.3dshapes/ (needs CadQuery)

All footprints are drawn from the manufacturer's land-pattern drawing. No
third-party CAD library file is copied. Models use
`${KIPRJMOD}/lib/irl2fa.3dshapes/<footprint>.step` with zero offset and rotation.

## Footprints and models

| Footprint | Source | Dimensions used |
|---|---|---|
| `SW_Nav_Alps_SKRHABE010` | [Alps SKRH catalog, Drawing No.2](https://tech.alpsalpine.com/cms.media/product_catalog_mu_02_skrh_en_34aeb8c2e3.pdf) | Pads 1.35x1.0 at x ±3.575 (8.5/5.8 spans), y 0/±1.425 (3.85/1.85 spans). Pads 1=A 2=Center 3=C (left, top to bottom), 4=B 5=Common 6=D (right). MP 2.0x1.8 at y ±4.05 (9.9/6.3 spans). NPTH 0.80 at (0,-1.9) and 1.10 at (0,+1.9); the drawing gives 0.75 and 1.05, +0.1/-0. Body 7.35x7.5x1.8, collar to 2.14, shoulder to 3.0, stem ø3 to 5.0. |
| `Hirose_FH34SRJ-24S-0.5SH_1x24-1MP_P0.50mm_Horizontal` | [Hirose FH34SRJ datasheet](https://www.hirose.com/en/product/series/FH34S) ([LCSC copy](https://datasheet.lcsc.com/datasheet/pdf/4fa13630ce538d3df6cea685e5f75b4e.pdf?productCode=C324726)) | Signal pads 0.3x0.8, pitch 0.5, B=11.5. Pad 1 at x=+5.75. MP pads 0.4x0.8 at x ±6.75 (E=13.1, F=13.9), y=+2.5. The 3.3 dimension runs from the outer edge of the signal pads to the outer edge of the MP pads. The FPC enters from +Y. Housing D=13.38, A=14.0, 3.8 deep, 1.0 high. |
| `BatteryHolder_MYOUNG_BS-24-B4AA016_CR2450` | [MYOUNG drawing MY-CP-0138 (LCSC C964809)](https://www.lcsc.com/datasheet/C964809.pdf) | Pads 3.625x4.5 at x ±16.6875 (37.00/29.75 spans). Pad 1 = + (right), pad 2 = − (left). Body 27.2x16.0, 36.0 long including the terminals, **7.50 high**. Cell ø24.5 centred on the origin. |
| `LED_Vishay_VSMB2948SL_SideView` | [Vishay VSMB2948SL, doc 83498](https://www.vishay.com/docs/83498/vsmb2948sl.pdf), drawing 6.544-5410.02-4 | IPC-7351 pad proposal: 0.9x1.2 pads, outer span 4.2, so x ±1.65. Pad 1 = K (+x), pad 2 = A (−x). Body 2.2 wide, 2.3 high, 1.6 deep (y −0.42..+1.18). Lens ø1.8 tips at y −1.37 (2.55 overall). Optical axis −Y, 1.15 above the PCB. |
| `PhotoTransistor_Vishay_VEMT2523SLX01_SideView` | [Vishay VEMT2523SLX01, doc 84167](https://www.vishay.com/docs/84167/vemt2523slx01.pdf), drawing 6.544-5410.01-4 | Same package and pads as the VSMB2948SL. Pad 1 = C (+x), pad 2 = E (−x). |
| `EPD_GoodDisplay_GDEY0213B74_Outline` | [GDEY0213B74 datasheet rev 1.0, §4](https://v4.cecdn.yun300.cn/100001_1909185148/GDEY0213B74.pdf); `hardware/board.yaml` | Glass 59.2x29.2x1.0, with the origin at its centre. Active area 48.55x23.7046, starting 7.95 from the FPC-side edge and centred in Y. FPC 12.5 wide, 14.3 long, 0.3 thick, its top edge 8.15 below the top glass edge. Edge.Cuts slot 1.4x14.0 with rounded ends, centred at (−32.3, 0). Model: 0.2 adhesive, FPC folded 180° (R0.5) through the slot with 9.9 mm on the back. |
| `MicroCrystal_C7_SON-8_1.5x3.2mm_P0.9mm` (model only) | [Micro Crystal RV-3032-C7 datasheet](https://www.microcrystal.com/fileadmin/Media/Products/RTC/Datasheet/RV-3032-C7.pdf) | 3.2x1.5x0.8 box for the stock `Package_SON` footprint, which has no model in kicad-packages3D 9.0.9.1. |

## Symbols (`irl2fa.kicad_sym`)

| Symbol | Pins | Default footprint |
|---|---|---|
| `RV-3032-C7` | 1 VBACKUP, 2 SDA, 3 ~INT (open collector), 4 EVI, 5 VSS, 6 VDD, 7 CLKOUT, 8 SCL (from the Micro Crystal datasheet). The graphics are KiCad's `Timer_RTC:RV-3028-C7`, renumbered. | `Package_SON:MicroCrystal_C7_SON-8_1.5x3.2mm_P0.9mm` |
| `Nav_Switch_5way` | 1 A, 2 Center, 3 C, 4 B, 5 Common, 6 D, MP (passive, may be left unconnected) | `irl2fa:SW_Nav_Alps_SKRHABE010` |

For the FPC connector, use the stock `Connector_Generic_MountingPin:Conn_01x24_MountingPin`
(pins 1-24, plus `MP`).

## Stock KiCad models (`kicad.3dshapes/`)

These are copied unchanged from [kicad-packages3D](https://gitlab.com/kicad/libraries/kicad-packages3D)
tag `9.0.9.1`, keeping KiCad's file names:
QFN-32-1EP_5x5mm_P0.5mm_EP3.45x3.45mm, SOT-23, SOT-323_SC-70, D_SOD-123, D_SOD-323F,
R_0402_1005Metric, R_0603_1608Metric, C_0402_1005Metric, C_0603_1608Metric,
C_0805_2012Metric, L_APV_ANR3015 (for `Inductor_SMD:L_APV_ANR3015`, the 3x3 mm 47 µH
ANR3015T470M). Tag-Connect TC2030 has no model. The stock footprints point at
`${KICAD9_3DMODEL_DIR}/<Lib>.3dshapes/<name>.step`. To use these copies, set
each footprint's model path to `${KIPRJMOD}/lib/kicad.3dshapes/<name>.step`.

## Licence

- Generated footprints, models and the `Nav_Switch_5way` symbol: **CERN-OHL-S-2.0**,
  like the rest of `hardware/`.
- `kicad.3dshapes/*.step`, and the `RV-3032-C7` symbol (derived from KiCad's
  `RV-3028-C7`): **CC-BY-SA-4.0 with the KiCad libraries exception**
  (https://www.kicad.org/libraries/license/).
