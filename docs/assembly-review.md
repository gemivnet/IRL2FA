# Assembly review: JLCPCB fabrication and PCBA

Review of what JLCPCB will build from `hardware/fab/`, done on 2026-10-30 against
commit 66b5be7. The question it answers is the one KiCad's checks cannot: will the
boards come back right, given that JLCPCB places parts from **its own** footprints
and reads our placement file by **its own** conventions?

It found that, as committed, the placement file would have put **9 of the 14
oriented parts on the board wrong**: the coin-cell clip reversed, all three booster
diodes reversed, both IR parts facing into the board with their pins swapped, the
reverse-battery FET Q1 turned 180°, the RTC turned 90° off its pads, and the FPC
connector 1.25 mm off its pads. Every KiCad check passed. The placement file is now
generated with the corrections (`hardware/tools/fab.py`, table `JLC`), and
`hardware/tests/test_assembly.py` places JLCPCB's own footprints with it and checks
that every pad lands on ours. Against the old file that test fails on BT1, D1, D2,
D3, D4, Q1, Q5, J1 and U2.

Contents: [findings](#findings) · [per-part placement](#per-part-placement) ·
[fab capabilities](#fabrication-capabilities) · [two-sided assembly and panel](#two-sided-assembly-and-the-panel) ·
[ordering](#ordering-at-jlcpcb) · [preview checklist](#what-the-placement-preview-must-show) ·
[order remark](#order-remark) · [not verified](#what-could-not-be-verified) · [sources](#sources)

## Findings

Ranked by severity. "Fixed" means fixed on this branch; "layout" means a change to
`irl2fa.kicad_pcb` that the board owner applies (this review does not edit the board).

| ID | Severity | Finding | Status |
|-|-|-|-|
| A-01 | Critical | Back-side CPL angles were KiCad's, not JLCPCB's | Fixed in `fab.py` |
| A-02 | High | J1 (FPC connector) would be placed 1.25 mm off its pads | Fixed in `fab.py` |
| A-03 | High | JLCPCB's SOT-23, SC-70 and RV-3032 footprints are drawn turned against KiCad's | Fixed in `fab.py` |
| A-04 | High | The board is too small for two-sided (Standard) PCBA without a panel; the README did not say | Fixed in README |
| A-08 | High | Both copper pours have no net: floating copper, not the ground pour the docs describe | Layout |
| A-05 | Medium | D4 (VSMB2948SL) and L1 (ANR3015) have no JLCPCB library footprint | Mitigated: preview check and remark |
| A-06 | Medium | RV-3032-C7 shows −144 available at JLCPCB; STM32U083 has 49 | Documented; pre-order |
| A-09 | Medium | IR lenses are flush with the top edge, where the panel's breakaway tabs go | Remark; optional own panel |
| A-12 | Medium | The `Power` net class matches no net: VBAT, VEPD and EPD_SW are routed at 0.15 and 0.112 mm | Layout (for the electrical review) |
| A-07 | Low | Silkscreen text below JLCPCB's 1.0 mm / 0.15 mm minimum | Layout |
| A-10 | Low | Full paste on the clip's two 6.15 × 6 mm legs | Optional footprint change |
| A-11 | Low | Our clip legs sit 0.3 mm from where JLCPCB's footprint has them | No action |
| A-13 | Info | No reserved spot for the JLCPCB order number | README says "Remove" |
| A-14 | Info | 11 Extended parts, about $3 each in feeder fees | Options listed |
| A-15 | Info | SW1 is rated for 2 reflows; its guide bosses may touch the second-side stencil | Remark |
| A-16 | Info | Via copper to track is 0.152 mm; JLCPCB lists 0.2 mm "via to track" | No action (hole to track is 0.30 mm) |

### A-01 Critical: back-side angles were KiCad's, not JLCPCB's (fixed)

**Evidence.** KiCad flips a back-side footprint top to bottom and keeps its angle
`a`. Looking at the back of the board, which is how a placement machine sees the
second side, that part appears unmirrored at `180 - a`. JLCPCB reads back-side
rotations that way. The three tools people use to order from KiCad all apply this rule:

- kicad-jlcpcb-tools, `fabrication.py`: `rotation = (180 - rotation) % 360` for any part not on the top layer ("bottom angles need to be mirrored on Y-axis").
- JLCKicadTools, `cpl_fix_rotations.py`: `rotation = (-rotation + 180) % 360`. It also notes that JLCPCB changed the rule in 2020 and twice in 2022.
- KiBot's `rot_footprint` filter with `mirror_bottom`: "180 - component rot + angle ... This is used by JLCPCB".

`kicad-cli pcb export pos` writes `a` unchanged, and `fab.py` copied it. That is
right only at 90° and 270°. Every other back part came out turned 180°, which is
harmless for resistors and capacitors and fatal for the rest: BT1, D1-D3, D4 and Q5.
Combined with A-03, Q1 and U2 were wrong too, while Q2-Q4 were right only because
two 180° errors cancelled.

**Fix.** `fab.py` writes `(180 - a + c) % 360` on the back and `(a + c) % 360` on the
front, where `c` is the per-part footprint correction from A-03. The positions keep
X unmirrored, as JLCPCB expects, and use the drill/aux origin (bottom-left, Y up),
the same frame as the Gerbers (`test_cpl_frame_matches_the_gerbers`).

**Residual risk.** The rule is JLCPCB's current convention as documented by third
parties; JLCPCB publishes no specification. That is why the order steps make
**Confirm Parts Placement: Yes** mandatory, with the checklist
[below](#what-the-placement-preview-must-show).

### A-02 High: J1 would be 1.25 mm off its pads (fixed)

JLCPCB centres its footprint's origin on the CPL point. The EasyEDA FH34SRJ-24S
footprint has its origin at the centre of the pad field, midway between the signal
row and the fitting nails. Our footprint's anchor is on the signal row. Matching all
26 pads by number gives an exact fit (0.000 mm residual) only with JLCPCB's origin at
(0, +1.25) mm in our footprint frame. With the old CPL, the 0.5 mm-pitch connector
would have been placed 1.25 mm across its own depth, leaving the contacts off the pads.
`fab.JLC["C324726"]` carries the offset, and the CPL now gives J1 at X = 11.25 mm
instead of 12.5. All the other parts' origins coincide with ours to within 0.07 mm,
which is pad-length difference rather than offset.

### A-03 High: SOT-23, SC-70 and RV-3032 footprints are turned (fixed)

The pad centres of every EasyEDA footprint were matched to our library footprint,
pad by pad, over 0/90/180/270°:

- **SOT-23 (AO3401A C15127, AO3400A C20917) and SC-70 (Si1308EDL C469327): 180°.** EasyEDA puts pins 1 and 2 on the right and pin 3 on the left; KiCad puts pins 1 and 2 on the left. This is the well-known SOT-23 entry in kicad-jlcpcb-tools' corrections.
- **RV-3032-C7 (C5127802): 270°.** EasyEDA draws the 3.2 × 1.5 mm package landscape with pin 1 bottom-left; KiCad's `MicroCrystal_C7_SON-8` is portrait with pin 1 top-left. The pin-number fit is 0.008 mm at 270°.
- **Everything else: 0°.** That covers passives, SOD-123 (pad 1 = cathode on both), UFQFPN-32 (pin 1 and its dot top-left on both), SKRHABE010 (pads 1-3 on the left, the small boss at the top), VEMT2523SLX01 (pad 1 = C on the right, lens at the top), FH34SRJ (pin 1 at +x) and the MY-2450-01 clip (chamfered closed end at the top).

### A-04 High: two-sided assembly needs a panel (fixed in README)

JLCPCB's PCBA capabilities page gives:

- Economic PCBA: one side only, boards 10 × 10 to 470 × 500 mm, 0.8-1.6 mm thick.
- Standard PCBA: one or both sides, boards **and panels 70 × 70 mm minimum**, edge rails and fiducials necessary.

Our 77 × 31 mm board has the joystick on the front and everything else on the back,
so it needs Standard, and it is too small. Edge rails alone (77 × 41 mm) are still too
small. A **1 × 2 panel with 5 mm rails on the long sides** comes to about 77 × 74 mm.
The README now asks for "Panel by JLCPCB", 1 column × 2 rows, with edge rails and stamp
holes (mouse bites). V-cut is out: the corners are rounded and the IR parts are flush
with the top edge. JLCPCB adds the tooling holes and fiducials on the rails.

A cheaper alternative, not chosen here: order **Economic, bottom side only**, mark SW1
"do not place", and hand-solder the joystick (8 pads round a 7.5 mm body, with guide
bosses that self-align it). That removes the second setup, the second stencil, the
panel and the 70 mm limit. It would need SW1 excluded from the BOM and CPL for that
order.

### A-08 High: the copper pours are on no net (layout)

Both board-level fills (F.Cu, `irl2fa.kicad_pcb` line ~15864, and B.Cu) have
`(net 0) (net_name "")`. The net is called `/GND` everywhere else, so the pours were
probably made with `GND` and lost the net when the net names gained the `/` prefix.
The result is floating copper, not the ground pour `pcb-design.md` describes. Thermal
reliefs, return paths and shielding are all absent, and DRC does not flag it. GND is
still fully connected by its 157 tracks, so the board works, but every pour island is
an unconnected antenna next to the booster's switch node.

**Fix.** Set both zones' net to `/GND`, refill, re-run DRC, and then remove the xfail
on `test_copper_pours_are_ground`. Afterwards, check that BT1 pad 2, the 12 mm contact,
connects solid rather than by four spokes. It is a contact, not a joint, so solid is
better.

### A-12 Medium: the Power net class is never applied (layout, for the electrical review)

`irl2fa.kicad_pro` assigns the `Power` class (0.3 mm) by the patterns `GND`, `VBAT`,
`VBAT_RAW`, `VEPD` and `EPD_SW`. The nets are named `/GND`, `/VBAT` and so on, so
nothing matches. Every track is `Default`: VBAT, VEPD and EPD_SW are 0.15 mm, with
segments at 0.112 mm. `pcb-design.md` says "0.4 mm power tracks". This is within
JLCPCB's 0.10 mm capability, so fabrication is unaffected; whether the booster's
switch-node and VEPD current wants wider copper is for the electrical review.

**Fix.** Use `/VBAT` (or `*VBAT`) style patterns and re-route the power nets.

### A-05 Medium: D4 and L1 have no JLCPCB footprint

`easyeda.com/api/products/C6604342/components` and `.../C7427088/...` answer 404:
JLCPCB has no library footprint for the VSMB2948SL or the ANR3015. Its engineers
draw one when they prepare the order, so the rotation they apply cannot be checked
in advance.

- **D4.** The CPL assumes they draw it like the VEMT2523SLX01 (C511451), the same Vishay side-view package: pad 1 (cathode) on the right, lens at the top. `jlc_footprints.yaml` checks D4 against that assumption.
- **L1.** It is unpolarised and square, so only a 90° error would matter.

**Mitigation.** The placement preview checklist and the order remark both say where
D4's lens and cathode must be.

### A-06 Medium: stock for the first run

Read from JLCPCB's parts API on 2026-10-30. The [BOM](bill-of-materials.md) now shows
JLCPCB stock and Basic/Extended, not only LCSC's:

- **RV-3032-C7 (C5127802).** Stock 400, but "available" −144: more is reserved than held. An assembly order may not be accepted. Buy 10+ into your JLCPCB parts inventory first, or use global sourcing. `test_jlc_has_stock_for_the_first_run[U2]` is xfail (non-strict) with this ID.
- **STM32U083KCU6 (C22459164).** 49 at JLCPCB (42 at LCSC). That is enough for 10 boards, but one other order away from not being.
- **VSMB2948SL (C6604342).** 176. Fine for 10.
- **0402 10 kΩ (C25744).** 0 at LCSC, which the old BOM showed, but 16.8 M at JLCPCB. Not a problem.

### A-09 Medium: breakaway tabs and the flush IR lenses

D4 and Q5 sit with their lens tips on the top edge (CPL Y = 31 mm). A panel joins
boards along their long edges. A mouse-bite tab in front of a lens would leave a
0.3-0.5 mm stub in the optical path, and snapping it puts stress on joints 1.4 mm
from the break.

The order remark asks for no tabs within 5 mm of either part. That is X 15.5-25.5 mm
and 51.5-61.5 mm on the top edge. JLCPCB does not promise to honour remarks about its
own panels. To control it fully, supply the panel yourself, for example with KiKit:
rails on the long sides, tabs at x ≈ 8, 38 and 69 mm only, and three fiducials and
tooling holes per rail. This is a layout task.

### A-07 Low: silkscreen below JLCPCB minimums (layout)

JLCPCB's minimum is 1.0 mm text height and 0.15 mm line width. On the board:

- 36 references are 0.8 mm / 0.15 mm.
- 4 are 0.65 mm / 0.12 mm.
- 2 are 0.7 mm / 0.105 mm.

There are also 0.12 mm footprint outlines (KiCad's stock 0.12 mm silk). JLCPCB prints
them, but small text may come out blurred. Polarity marks are lines, and they are
present for D1-D3, U1, U2, Q1-Q4, J1, D4 and Q5. `test_silkscreen_meets_jlc_minimums`
is a strict xfail. Fix it by raising the board's text to 1.0 mm / 0.15 mm, or by hiding
the references that do not fit (the iBOM still identifies them).

### A-10 Low: full paste on the clip legs

The legs of BT1 (pad 1, 6.15 × 6.0 mm, ×2) get 100 % paste: 37 mm² each under a
0.25 mm phosphor-bronze foot. That is a lot of solder for a foot that should sit flat.
A 70-80 % aperture would do, for example a paste margin of −0.35 mm on those pads in
`BatteryHolder_MYOUNG_MY-2450-01_CR2450`. It is optional, and JLCPCB's stencil
engineers often reduce large apertures anyway.

### A-11 Low: clip leg position

Our footprint, from the MYOUNG drawing, has the legs 0.3 mm towards the closed end.
EasyEDA's has them level with the centre pad. Both are well inside the 6 × 6 mm pads,
so no action is needed; the mechanical review owns which is right.

### A-13 Info: order number

The board has no `JLCJLCJLCJLC` placeholder, so the README says **Remove order
number: Yes**. If that costs extra on your order, "No" is harmless: the number
would be printed somewhere JLCPCB chooses.

### A-14 Info: Extended parts

11 unique Extended parts: BT1, D1-D3, D4, J1, L1, Q3, Q5, R7, SW1, U1 and U2. Each
costs about $3 per order in feeder fees. Basic alternatives exist for two of them,
but each changes the electrical design, so they are listed for the electrical review
rather than applied:

- MBR0530 → B5819W SL (C8598, SOD-123, Basic). It leaks more: 1 mA at 40 V maximum.
- 2.2 Ω 0402 R7: no Basic 2.2 Ω 0402 was found.

### A-15 Info: SW1 and two-sided reflow

Alps rates the SKRHABE010 for 2 reflows, so top-first is still within its rating. Its
guide bosses go into 0.8 mm NPTHs. If they protrude below the 0.8 mm board, they lift
the back-side stencil when the front is assembled first. The order remark asks for
the back side first, so SW1 sees one reflow.

Back side first means BT1 (about 1.3 g on 74 mm² of joints) goes through the second
reflow upside down. That is under the usual 30 g/in² (0.047 g/mm²) limit; it is at
about 0.018 g/mm².

### A-16 Info: via to track

The closest via copper to another net's track is 0.152 mm, at 12 places (DRC uses
0.15 mm). JLCPCB's capabilities table lists "via to track 0.20 mm". The via's drilled
hole is 0.302 mm from the track, which is above that figure under any reading, and the
0.10 mm copper spacing is within their 2-layer capability. No action is needed. If
JLCPCB's DFM flags it, it will say so before production.

## Per-part placement

"Corr." is the angle that turns JLCPCB's footprint onto ours (top view,
counter-clockwise). "Old" is the committed CPL at 66b5be7; "New" is what `fab.py`
writes now; "verdict" is what the old file would have built. Back-side angles are as
JLCPCB reads them: seen from the back.

| Refs | LCSC, MPN | JLCPCB footprint (EasyEDA) | Package match | Side, KiCad angle | Corr. | Old | **New** | Verdict on old CPL | Pin 1 / body evidence | JLC type, stock |
|-|-|-|-|-|-|-|-|-|-|-|
| BT1 | C964845 MY-2450-01 | BAT-SMD_MY-2450-01 | ✓ CR2450 SMT clip | B, 180 | 0 | 180 | **0** | ✗ clip turned: open end at the IR edge, the cell cannot go in from the bottom | legs (pad 1, +) at ±15.3 mm, pad 2 (−) at origin; chamfered closed end at +y in both | Ext, 5366 |
| D1, D2, D3 | C77336 MBR0530 | SOD-123_L2.8-W1.8-LS3.7-RD | ✓ SOD-123 | B, 0 | 0 | 0 | **180** | ✗ all three reversed | pad 1 = cathode (symbol pin C) at −x with the bar, as KiCad D_SOD-123 | Ext, 50085 |
| D4 | C6604342 VSMB2948SL | none (404) | ✓ datasheet (test_footprints) | B, 180 | 0 (assumed) | 180 | **0** | ✗ lens into the board, K/A swapped (if drawn as Q5) | ours: pad 1 = K at +x, lens −y (y down); see A-05 | Ext, 176 |
| J1 | C324726 FH34SRJ-24S-0.5SH(50) | FFC-SMD_24P-P0.50_FH34SRJ-24S-0.5SH | ✓ 24 pos 0.5 mm, top + bottom contact, (50) = reel | B, 90 | 0, origin (0, 1.25) | 90 at X 12.5 | **90 at X 11.25** | ✗ 1.25 mm off the pads | all 26 pads match by number, 0.000 mm | Ext, 10185 |
| L1 | C7427088 ANR3015T470M | none (404) | ✓ 3 × 3 mm | B, 0 | 0 (assumed) | 0 | 180 | – unpolarised | – | Ext, 13776 |
| Q1 | C15127 AO3401A | SOT-23_L2.9-W1.3-P1.90-LS2.4-BR | ✓ SOT-23 | B, 90 | 180 | 90 | **270** | ✗ turned 180°: G/S on D's pad | EasyEDA pins 1, 2 at +x, 3 at −x; KiCad the reverse | Basic, 1.0 M |
| Q2 | C15127 AO3401A | (as Q1) | ✓ | B, 0 | 180 | 0 | 0 | ✓ (two errors cancelled) | as Q1 | Basic |
| Q3 | C469327 Si1308EDL-T1-GE3 | SOT-323_L2.0-W1.3-P1.30-LS2.1-BR | ✓ SC-70-3 = SOT-323 | B, 0 | 180 | 0 | 0 | ✓ (cancelled) | pins 1, 2 at +x | Ext, 43574 |
| Q4 | C20917 AO3400A | SOT-23-3_L2.9-W1.3-P1.90-LS2.4-BR | ✓ SOT-23 | B, 0 | 180 | 0 | 0 | ✓ (cancelled) | pins 1, 2 at +x | Basic, 1.06 M |
| Q5 | C511451 VEMT2523SLX01 | SENSOR-SMD_VEMT2523SLX01 | ✓ side-view 2.3 × 2.55 | B, 180 | 0 | 180 | **0** | ✗ lens into the board, C/E swapped | pad 1 = C at +x, lens arc at +y in both | Ext, 1874 |
| SW1 | C139794 SKRHABE010 | SW-SMD_SKRHABE010 | ✓ 7.5 × 7.5, 6 + 2 pads, 2 bosses | **F**, 45 | 0 | 45 | 45 | ✓ | pads 1-3 at −x (1 top), 4-6 at +x; small boss top in both | Ext, 6785 |
| U1 | C22459164 STM32U083KCU6 | UFQFPN-32_L5.0-W5.0-P0.50-TL-EP3.5 | ✓ 5 × 5, EP 3.5 | B, 90 | 0 | 90 | 90 | ✓ | pin 1 top of the left column, dot beside it in both; 33 pads fit to 0.053 mm | Ext, **49** |
| U2 | C5127802 RV-3032-C7 | OSC-SMD_8P-L3.2-W1.5-P0.90-BL | ✓ C7 3.2 × 1.5, 8 pads 0.9 mm | B, 0 | 270 | 0 | **90** | ✗ turned 90°: pins off the pads | EasyEDA landscape, pin 1 bottom-left; KiCad portrait, pin 1 top-left | Ext, 400 (**−144 available**) |
| C1, C2 | C16780 CL21A476MQYNNNE | C0805 | ✓ | B, 90 | 0 | 90 | 90 | – unpolarised | – | Basic |
| C3, C4, C6-C8 | C1525 / C52923 / C15195 | C0402 | ✓ | B, 0 | 0 | 0 | 180 | – | – | Basic |
| C9 | C1525 | C0402 | ✓ | B, 90 | 0 | 90 | 90 | – | – | Basic |
| C5 | C19666 | C0603 | ✓ | B, 90 | 0 | 90 | 90 | – | – | Basic |
| C10, C11 | C1779 | C0805 | ✓ | B, 0 | 0 | 0 | 180 | – | – | Basic |
| C12-C19 | C15849 | C0603 | ✓ | B, 0 | 0 | 0 | 180 | – | – | Basic |
| R1, R6, R8, R9 | C25744 / C26083 / C25076 / C25741 | R0402 | ✓ | B, 90 | 0 | 90 | 90 | – | – | Basic |
| R2-R5, R7, R10 | C25744 / C25900 / C25741 / C25101 | R0402 | ✓ | B, 0 | 0 | 0 | 180 | – | – | Basic (R7 Ext) |

Every MPN in `irl2fa-bom.csv` matches the LCSC part's model, ignoring case and
punctuation (`test_bom_mpn_matches_lcsc_part`). The one difference in form is
"RV-3032-C7 32.768kHz 2.5ppm-TA-QC" against "RV-3032-C7-32.768kHz-2.5PPM-TA-QC".
Package match is the LCSC package field and the EasyEDA land pattern against our
footprint; our footprints themselves are checked against the manufacturers' drawings
by `test_footprints.py`.

## Fabrication capabilities

2 layers, 0.8 mm FR-4, ENIG, 1 oz, green. "Ours" is measured on the board (pcbnew)
or in the committed Gerbers (gerbonara), not taken from the DRC rule settings.
JLCPCB values are from jlcpcb.com/capabilities/pcb-capabilities (2026-10-30).

| Item | Ours | JLCPCB | OK |
|-|-|-|-|
| Min track | 0.112 mm (signals 0.15) | 0.10 mm | ✓ |
| Min copper spacing | 0.152 mm (track-track, via-track) | 0.10 mm | ✓ |
| SMD pad to pad, different nets | 0.20 mm (FPC), 0.25 mm (QFN) | 0.15 mm | ✓ |
| Via | 0.3 mm drill / 0.6 mm pad (ring 0.15 mm; pad − hole 0.3 mm) | hole ≥ 0.15 (0.2 preferred); pad ≥ hole + 0.1 (0.15 preferred) | ✓ |
| Via hole to other copper | 0.302 mm | via to track 0.20 mm | ✓ |
| PTH | 2.2 mm M2, 4.4 mm pad (ring 1.1 mm) | ring ≥ 0.18 mm (0.25 recommended) | ✓ |
| NPTH | 0.8, 0.991 (×3), 1.1 mm | ≥ 0.50 mm | ✓ |
| Hole to hole | ≥ 0.25 mm (DRC) | via 0.20, pad 0.45 mm | ✓ |
| Copper to board edge | ≥ 0.30 mm (DRC); mounting-hole pads 0.40 mm from the corner arcs | ≥ 0.20 mm routed | ✓ |
| Hole to board edge | M2 holes 1.5 mm from the corner arcs (they sit at the radius centres) | – | ✓ |
| FPC slot | 1.4 × 14.0 mm routed in Edge.Cuts with full-round ends; 0.3 mm copper keepout | non-plated slot ≥ 1.0 mm wide; copper ≥ 0.2 mm from a routed edge | ✓ |
| Solder-mask dam, fine pitch | U1 QFN 0.18 mm (corner pair), J1 FPC 0.20 mm, U2 0.25 mm; mask expansion 0 | 0.10 mm (green), 0.13 mm (black/white) | ✓ any colour |
| Vias | tented (no mask openings at vias in either mask Gerber) | order option "Tented" | ✓ |
| QFN exposed-pad paste | 4 windows 1.41 × 1.41 mm on 3.5 × 3.5 mm: about 63 % | 50-80 % is usual | ✓ |
| 12 mm cell-contact pad | mask open, **no paste** (bare ENIG contact, as intended) | – | ✓ (remark tells JLCPCB it is deliberate) |
| Silkscreen | text 0.65-0.8 mm high, strokes 0.105-0.15 mm | ≥ 1.0 mm high, ≥ 0.15 mm | ✗ A-07 |
| Thermal reliefs | none: the pours have no net (A-08); zone setting 0.25 mm gap, 0.3 mm spokes | – | after A-08 |
| Board size | 77 × 31 mm | fab ≥ 3 × 3 mm; Standard PCBA ≥ 70 × 70 mm | panel (A-04) |
| Thickness | 0.8 mm (±0.1) | 0.8 mm standard option; Economic PCBA 0.8-1.6, Standard any | ✓ |

## Two-sided assembly and the panel

- **Front:** SW1 only.
- **Back:** everything else, including the tallest part: BT1, 5.7 mm.
- There are no through-hole parts. The only holes are the M2 mounting holes, SW1's two guide-boss NPTHs and the Tag-Connect NPTHs.
- Double-sided assembly is Standard PCBA only. That needs edge rails, fiducials and tooling holes (all added by JLCPCB on a "Panel by JLCPCB" order) and a panel of at least 70 × 70 mm (A-04).
- The board has no fiducials of its own; the rail fiducials serve. If you supply your own panel, put three fiducials on each rail, on both sides.
- Bottom side first is preferred (A-15).

## Ordering at JLCPCB

The README's [Build one](../README.md#build-one) step 1 is the short version. Each
option, and why:

1. **Upload `hardware/fab/irl2fa-gerbers.zip`.** Size, layers and outline are detected from it. Nothing else should be needed from the Gerber side.
2. **Delivery format: Panel by JLCPCB, 1 column × 2 rows, edge rails on the long sides, stamp holes not V-cut** (A-04, A-09). Check whether the quantity box then counts panels or boards: 10 boards is 5 panels.
3. **PCB thickness 0.8 mm.** The case and the FPC fold are designed for it.
4. **Surface finish ENIG.** BT1 pad 2 is the cell's negative contact: bare gold, no solder. HASL would leave a domed, oxidising contact.
5. **Via covering: Tented.** It matches the Gerbers, and a bare via under the sliding cell could short to its can.
6. **Outer copper 1 oz; colour any.** The narrowest mask dam is 0.18 mm, fine even for black or white.
7. **Remove order number: Yes** (A-13).
8. **PCB Assembly: on.** PCBA type **Standard**, assembly side **Both sides**, tooling holes **added by JLCPCB**, **Confirm Parts Placement: Yes**.
9. **BOM: `irl2fa-bom-jlcpcb.csv`; CPL: `irl2fa-cpl-jlcpcb.csv`.** On the parts page, check U1 and U2 are in stock (A-06), and confirm the 11 Extended parts.
10. **Placement preview.** Go through the table below before approving.
11. **Remark.** Paste the text in [Order remark](#order-remark).

## What the placement preview must show

JLCPCB shows the back side as seen from the back, so left and right are swapped
compared with the front-view coordinates. The board's **top edge** (IR edge, CPL
Y = 31 mm) is at the top in both views.

| Part | Must show |
|-|-|
| D4 (IR LED, back, near the top edge right of centre seen from the front) | lens/dome pointing at the top edge, overhanging nothing; cathode on the pad towards the board centre (front-view X ≈ 54.85 mm) |
| Q5 (phototransistor, back, top edge left of centre seen from the front) | lens pointing at the top edge; collector on the pad away from the board centre (front-view X ≈ 18.85 mm) |
| BT1 (coin-cell clip) | chamfered, closed end towards the top edge; open end towards the bottom edge |
| D1, D2, D3 (SOD-123) | cathode band on the end with the silkscreen bar |
| Q1, Q2, Q4 (SOT-23), Q3 (SC-70) | the body's single lead (pin 3, drain) on the single pad. Q1 is the one the old file had wrong |
| U1 (QFN-32) | pin-1 dot at the silkscreen pin-1 mark (triangle) |
| U2 (RV-3032, 3.2 × 1.5 mm) | long axis matching the 2 × 4 pads; pin-1 corner at the silkscreen mark |
| J1 (FPC connector) | body centred on the 24 pads and the two nails, not on the pad row; actuator towards the FPC slot |
| SW1 (front) | turned 45°, guide bosses in the two holes |

For SOT-23s, the simplest check is that the part body's single lead sits on the
single pad. That is true of every SOT-23 placement that is right, and of none that is
wrong.

## Order remark

Paste into the order's remark box:

> Double-sided board. Bottom-side rotations in the CPL already follow your convention (viewed from the bottom). Please check in particular: D4 (IR LED) and Q5 (phototransistor) are side-looking; their lenses must point at the board edge at CPL Y = 31 mm. D4 cathode is the pad at X = 54.85 mm. BT1 coin-cell clip: open end towards the Y = 0 edge. BT1 pad 2 (12 mm round pad under the cell) has no solder paste on purpose: it is a bare ENIG battery contact. Panel: please put no breakaway tabs on the Y = 31 edge within 5 mm of D4 (X 51.5-61.5 mm) or Q5 (X 15.5-25.5 mm). If possible, assemble the bottom side first and the top side (SW1 only) second.

## What could not be verified

- **JLCPCB's back-side rotation convention.** No first-party specification exists. Three independent tools agree on it, and JLCPCB has changed it before. Hence Confirm Parts Placement.
- **JLCPCB's footprints for D4 and L1.** They do not exist yet (A-05).
- **The library EasyEDA serves now against the library JLCPCB's engineers use.** The fetched footprints are JLCPCB's public library; they could be revised.
- **SW1's guide-boss length below the mounting face.** It is not in Alps' web data, only in the drawing image (A-15).
- **Exact JLCPCB form labels and prices on the day you order** (panel quantity counting, remove-mark fee, Standard PCBA setup fees). The form changes; this review gives the choices, not the prices.
- **Whether JLCPCB honours the tab-placement remark** for its own panel (A-09).

## Sources

- JLCPCB PCB capabilities: <https://jlcpcb.com/capabilities/pcb-capabilities>
- JLCPCB PCBA capabilities: <https://jlcpcb.com/capabilities/pcb-assembly-capabilities>
- EasyEDA/LCSC component API (JLCEDA/EasyEDA official library, <https://easyeda.com>, <https://lceda.cn>): `https://easyeda.com/api/products/<LCSC>/components?version=6.4.19.5`
- JLCPCB parts library API: `https://jlcpcb.com/api/overseas-pcb-order/v1/shoppingCart/smtGood/selectSmtComponentList`
- LCSC product API: `https://wmsc.lcsc.com/ftps/wm/product/detail?productCode=<LCSC>`
- kicad-jlcpcb-tools `fabrication.py`: <https://github.com/Bouni/kicad-jlcpcb-tools>
- JLCKicadTools `cpl_fix_rotations.py`: <https://github.com/matthewlai/JLCKicadTools>
- KiBot position notes and `rot_footprint` filter: <https://kibot.readthedocs.io/en/latest/notes_position.html>, <https://kibot.readthedocs.io/en/latest/configuration/filters/rot_footprint.html>
- Alps SKRHABE010 (reflow 2 times): <https://tech.alpsalpine.com/e/products/detail/SKRHABE010/>
