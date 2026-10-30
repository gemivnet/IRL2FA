# IRL2FA KiCad library

Project-local KiCad 9 library: `irl2fa.pretty/` (footprints), `irl2fa.3dshapes/`
(STEP models for them), `irl2fa.kicad_sym` (symbols) and `kicad.3dshapes/` (stock
KiCad STEP models copied so renders work without a full KiCad install).

Regenerate the footprints and models with:

    python3 hardware/tools/gen_footprints.py      # -> irl2fa.pretty/
    python3 hardware/tools/gen_models.py          # -> irl2fa.3dshapes/ (needs CadQuery)

All footprints are drawn from the manufacturer's land-pattern drawing. No
third-party CAD library file is copied.

`hardware/tests/footprints.yaml` records the datasheet land pattern, pin 1,
holes and body of every footprint on the board, stock KiCad ones included,
with the source page and tolerances. `hardware/tests/test_footprints.py`
checks the footprints embedded in the board and their library copies
against it, and the symbol pins against the pads. A footprint on the board
with no entry fails the test, so a new part needs an entry. Models use
`${KIPRJMOD}/lib/irl2fa.3dshapes/<footprint>.step` with zero offset and rotation.

## Footprints and models

| Footprint | Source | Dimensions used |
|---|---|---|
| `SW_Nav_Alps_SKRHABE010` | [Alps SKRH catalog, Drawing No.2](https://tech.alpsalpine.com/cms.media/product_catalog_mu_02_skrh_en_34aeb8c2e3.pdf) | Pads 1.35x1.0 at x ±3.575 (8.5/5.8 spans), y 0/±1.425 (3.85/1.85 spans). Pads 1=A 2=Center 3=C (left, top to bottom), 4=B 5=Common 6=D (right). MP 2.0x1.8 at y ±4.05 (9.9/6.3 spans). NPTH 0.80 at (0,-1.9) and 1.10 at (0,+1.9); the drawing gives 0.75 and 1.05, +0.1/-0. Model: body 7.35x7.5x1.8 with its corners cut to 8.6 across the operating diagonal ("c"), a cone to 2.14, the ø3 shoulder to ~3.12, and the keytop stem, a **1.95 mm square with R0.5 corners at 45° to the body** (so axis-aligned on this board), to 5.0; guide bosses ~1.4 long (scaled). Only the top 1.7 mm of the stem may go into a cap. Courtyard: body + signal pads + frame tabs as a cross-shaped outline (not their bounding box), so the 45°-rotated part can sit 1.2 mm from the glass. |
| `Hirose_FH34SRJ-24S-0.5SH_1x24-1MP_P0.50mm_Horizontal` | [Hirose FH34SRJ datasheet](https://www.hirose.com/en/product/series/FH34S) ([LCSC copy](https://datasheet.lcsc.com/datasheet/pdf/4fa13630ce538d3df6cea685e5f75b4e.pdf?productCode=C324726)) | Signal pads 0.3x0.8, pitch 0.5, B=11.5. Pad 1 at x=+5.75. MP pads 0.4x0.8 at x ±6.75 (E=13.1, F=13.9), y=+2.5. The 3.3 dimension runs from the outer edge of the signal pads to the outer edge of the MP pads. The FPC enters from +Y. Housing D=13.38, A=14.0, 3.8 deep, 1.0 high. |
| `BatteryHolder_MYOUNG_MY-2450-01_CR2450` | [MYOUNG drawing MY-CP-0106 rev A/0, "CR2450-W" (LCSC C964845)](https://datasheet.lcsc.com/datasheet/pdf/26b02de8ee838f915c56d5652dea3d54.pdf?productCode=C964845) | Clip of 0.25 mm Ni-plated C5191, **5.70 high**. Plate 25.50 wide, 23.20 deep: a 7.50 closed end with a stop flap (3.30 down), chamfered out to full width 6.9 below it, straight sides to 18.2, then two lobes and a finger notch. Two ø6.00 holes 11.00 apart, 10.0 from the closed end, carry the contact tongues. Pads 6.15x6.00, outer span 36.80 (x ±15.325), 9.20 from the closed end. Origin = cell centre, 12.50 from the closed end (cell edge against the 0.25 mm flap), so the pads sit 0.30 towards the closed end. Pad 1 = + (both legs). Pad 2 = − is our own ø12.0 bare copper pad under the cell (mask-defined, no paste), well inside the cell's negative cap. The courtyard is the union of plate, cell and pads. The cell slides in from the open end (+y). |
| `LED_Vishay_VSMB2948SL_SideView` | [Vishay VSMB2948SL, doc 83498](https://www.vishay.com/docs/83498/vsmb2948sl.pdf), drawing 6.544-5410.02-4 | IPC-7351 pad proposal: 0.9x1.2 pads, outer span 4.2, so x ±1.65. Pad 1 = K (+x), pad 2 = A (−x). Body 2.2 wide, 2.3 high, 1.6 deep (y −0.42..+1.18). Lens ø1.8 tips at y −1.37 (2.55 overall). Optical axis −Y, 1.15 above the PCB. |
| `PhotoTransistor_Vishay_VEMT2523SLX01_SideView` | [Vishay VEMT2523SLX01, doc 84167](https://www.vishay.com/docs/84167/vemt2523slx01.pdf), drawing 6.544-5410.01-4 | Same package and pads as the VSMB2948SL. Pad 1 = C (+x), pad 2 = E (−x). |
| `EPD_GoodDisplay_GDEY0213B74_Outline` | [GDEY0213B74 datasheet rev 1.0, §4](https://v4.cecdn.yun300.cn/100001_1909185148/GDEY0213B74.pdf); `hardware/board.yaml` | Glass 59.2x29.2x1.0, with the origin at its centre. Active area 48.55x23.7046, starting 7.95 from the FPC-side edge and centred in Y. FPC tail 12.5 wide, 14.3 long from the glass edge, its top edge 8.15 below the top glass edge. Edge.Cuts slot 1.4x14.0 with rounded ends, centred at (−32.3, 0). The flex is **14.30 wide for its first ~3.9 mm** (from 6.77 below the top glass edge), narrowing to 12.5 by ~4.7 mm: dimensioned on the GDEM0213B74WT drawing (same flex), scaled on the GDEY drawing. Contacts on the viewing face; 0.12 thick, 0.30 over the 6.0 stiffener. Model: 0.2 adhesive, the flex leaves the glass at the TFT face (~0.9 up), turns down through the slot, takes up its ~2.5 mm of surplus length in a loop under the board and runs flat into J1 (1.5 mm insertion). See `docs/mechanical-review.md`. |
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
