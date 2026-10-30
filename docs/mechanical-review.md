# Mechanical review against the manufacturers' drawings

Review date 2026-10-30. Board as of commit `66b5be7` (side-looking IR parts turned
to face out through the top edge). Scope: does the first board fit and assemble?
Every number we had drawn ourselves (`board.yaml`, `gen_footprints.py`,
`gen_models.py`, the case) was checked against the manufacturer's drawing rather
than against our own models. Our models had been drawn from the same assumptions
as the board, so a fit test against them could not have caught these.

**Result.** The board cannot be assembled as it stands: the display's flex is
14.3 mm wide where it passes through a 14.0 mm slot (MECH-01, board change). The
joystick cap was sized for a stem that does not exist and would not have held
(MECH-02), and the case's glass rib pressed on the flex (MECH-03). Both are fixed
in the case on this branch. Everything else fits. The checks that found these are
now automated in `case/tests/test_mechanical.py`.

Coordinates: board x right, y down from the top-left corner seen from the front
(as `board.yaml`); Z = 0 on the PCB front, the PCB back at Z = -0.8.

## Sources

| Ref | Document | Pages used |
|---|---|---|
| GD | Good Display GDEY0213B74 specification rev 1.0 (2021-03-17), from good-display.com product 391 ("GDEY0213B74 Specification") | p.7 mechanical drawing; p.35 inspection item 12 (silicone glue on the FPC) |
| GDWT | Good Display GDEM0213B74WT specification: same glass, active area and flex (FPC-A002), fully dimensioned front, side and back views | p.6 |
| ALPS | Alps Alpine SKRH series catalog (update 2510), [PDF](https://tech.alpsalpine.com/cms.media/product_catalog_mu_02_skrh_en_34aeb8c2e3.pdf), and the SKRHABE010 product-page drawing | p.1 (travel, forces, note 2), p.2 Drawing No.2 |
| HRS | Hirose FH34SRJ catalog (2018.11), [LCSC copy](https://datasheet.lcsc.com/datasheet/pdf/4fa13630ce538d3df6cea685e5f75b4e.pdf?productCode=C324726) | p.1-2 (top and bottom contact), p.3 connector, p.4 PCB pattern and recommended FPC, p.8-10 operation |
| MY | MYOUNG drawing MY-CP-0106 rev A/0, MY-2450-01 "CR2450-W", [LCSC C964845](https://datasheet.lcsc.com/datasheet/pdf/26b02de8ee838f915c56d5652dea3d54.pdf?productCode=C964845) | 1 page |
| VSH | Vishay VSMB2948SL doc 83498 rev 1.1 (drawing 6.544-5410.02-4, also VEMD2x23SLX01); VEMT2523SLX01 doc 84167 | p.5 |
| MUR | Murata CR2450R data sheet (Dec 2019) | p.1 |

Dimensions marked "scaled" are not dimensioned on the drawing. They were measured
off the drawing at 900 dpi against its own dimensioned features (better than
±0.1 mm on these drawings).

## Findings by severity

| ID | Severity | Where | Status |
|---|---|---|---|
| MECH-01 | **Critical** | board: FPC slot shorter than the flex flare that passes through it | **fixed** in [`2b8301f`](https://github.com/gemivnet/IRL2FA/commit/2b8301f) |
| MECH-02 | **High** | case: joystick cap socket for the wrong stem (size and angle) | fixed in the case |
| MECH-03 | Medium | case: glass-locating rib sat on the flex flare at its glued root | fixed in the case |
| MECH-04 | Medium | board: the coin cell may rest on mask-covered tracks instead of its negative pad | **fixed** in [`2b8301f`](https://github.com/gemivnet/IRL2FA/commit/2b8301f) |
| MECH-05 | Low | board: via in the coin cell's insertion path | **fixed** in [`2b8301f`](https://github.com/gemivnet/IRL2FA/commit/2b8301f) |
| MECH-06 | Medium | docs: assembly order glued the display before threading its flex | fixed (README, case-design) |
| MECH-07 | Low | models: joystick and display/flex 3D models did not match the drawings | fixed (`gen_models.py`) |
| MECH-08 | Low | assembly: glass placement tolerance against the 0.4 mm rib clearance | documented; check on the first build |
| MECH-09 | Low | `board.yaml` describes the joystick stem by its 3.0 mm shoulder | **fixed** in [`2b8301f`](https://github.com/gemivnet/IRL2FA/commit/2b8301f) |
| MECH-10 | Info | flex length: 2-3.5 mm spare, not "0.8 mm to spare" | docs fixed |
| MECH-11 | Info | J3 (Tag-Connect) only reachable with the board out of the case | documented |
| MECH-12 | Info | IR windows: beam above the axis is clipped; "thin" window needs an IR-clear filament | documented |
| MECH-13 | Info | the flex enters J1 contacts-up: J1 must stay a top-contact (or dual) part | documented |

### MECH-01 (Critical, board): the display flex is wider than its slot where it goes through

**Evidence.** GDWT p.6 front view dimensions the flex root: **14.30 wide, starting
6.77 below the top glass edge**, narrowing to the 12.50 tail (8.15 below the top
edge). The GDEY p.7 drawing shows the same outline (the 6.77 dimension is on it).
Scaled from GDEY p.7, the flare is 14.37 wide and runs **3.85 mm** from the glass
edge, tapering to 12.5 by 4.7 mm.

Our slot (`board.yaml` `fpc_slot`: y 8.5..22.5, 1.4 wide with R0.7 ends) is
2.0-3.4 mm from the glass edge (x 1.9..3.3, glass at x 5.3). The flex goes down
through it 2.7-4.0 mm along its length from the glass, inside the flare. On the
board the flare spans y 7.67..21.97: **0.83 mm of the flex's top edge is above
the slot's end**. With ±0.3 position and ±0.2 width tolerances (GD) it needs
y 7.27..22.37. The flex runs along the slot's right wall, where the round ends
shorten the usable length by another ~0.27 mm each. The flare is where the traces
fan out from the bonding pads, so it cannot be creased into the slot.

**Fix (board).** In `board.yaml`:

```yaml
fpc_slot:                    # covers the 14.3 mm flare (6.77 below the glass top) +-0.5 and 0.2 clear
  {x: 1.9, y: 6.3, w: 1.4, h: 17.0}
```

The straight part is then y 7.0..22.6. The slot stays 1.5 mm from H1's pad
(y 4.8) and 2.9 mm from H3's (y 26.2). Then regenerate the EPD footprint
(`gen_footprints.py` reads the slot from `board.yaml`) and run `gen_pcb.py --step
refresh`, `gen_models.py` (the flex model folds through the slot), `case/build.sh`
and `make fab`. Re-pour and re-route around the longer slot. Then remove the xfail
from `test_fpc_flare_fits_the_slot`. The case needs no change: its rib opening
already covers y 6.77..22.87.

**Status: fixed** in [`2b8301f`](https://github.com/gemivnet/IRL2FA/commit/2b8301f): the slot above, with the EPD footprint, the board (re-placed
and re-routed), the case dimensions and the fab outputs regenerated;
`test_fpc_flare_fits_the_slot` passes without its xfail.

### MECH-02 (High, case, fixed): the cap socket did not fit the joystick stem

**Evidence.** ALPS p.2 Drawing No.2, top view: the keytop stem is **□1.95 ±0.05
with R0.5 corners, drawn as a diamond**, so its flats are at 45 deg to the body.
The side view adds "keytop fitting area 1.7 max" from the stem top (5.0), and the
Ø3 is the shoulder below it (2.14..~3.1). The case had a **3.2 mm square socket
at 45 deg on the board**: the flats were turned 45 deg the wrong way, and the
crush ribs reached in only to 1.40 mm from the centre, against a stem whose
corners are at 1.17 mm. The cap would have dropped off. Its collar-clearance cone
also assumed collars of Ø5.2/Ø4.0, from a third-party model.

**Fix (done).** `case/irl2fa_case.scad`:
- new part-data parameters `stem_square = 1.95`, `stem_corner_r = 0.5`,
  `stem_angle = 0` (flats parallel to the board edges, because the body is at
  45 deg) and `stem_fit_max = 1.7`;
- the socket is the 1.95 R0.5 square + 0.2, with one 0.1 mm crush rib per face,
  from 3.30 to the stem top (1.70 engaged, the Alps maximum);
- below it, a flat-roofed Ø4.7 bore (a 4.7 mm bridge) clears the Ø3 shoulder
  (top ~3.12) and the Ø3.9 cone (to 2.14);
- `joystick_collars = [[3.9, 2.14], [3.0, 3.12]]`, from the drawing.

The cap still stands 3.15 mm proud. The 4-way tilt (0.25 mm at 4.3 mm height,
pivot 0.49 mm above the board, ALPS p.1-2) moves the cap ≤0.15 mm at the hole,
against a 0.4 mm gap. Retention is the press fit alone (see "Physical checks").

### MECH-03 (Medium, case, fixed): the glass rib sat on the flex

**Evidence.** The front shell's glass-locating rib (x 3.9..4.9, down to Z 0.7)
was open only over y 8.35..22.65: centred on the glass, and 12.5 + 1.8 wide.
The flex leaves the glass at the TFT face, ~0.8-0.9 mm up (GD p.7 side view;
GDWT p.6 "CONNECT SIDE"). Its flare spans y 7.67..21.97. The new interference
test, with the corrected flex model, found the rib inside the flare's top edge
(x 3.90..4.88, y 7.67..8.34, Z 0.80..0.92). That is 0.4-1.4 mm from the glass,
right on the silicone-glued root (GD p.35: glue up to 0.5 mm on the front of the
flex, 1.0 mm on the back). Screwing the case shut would have pressed on the bond.

**Fix (done).** The rib opening now follows the flare (`fpc_flare_y = 6.77`,
`fpc_flare_w = 14.30`, `fpc_tol = 0.5`) plus `part_clearance`: y 6.77..22.87.
`test_case.py` now models the flex as the flare, 0.95 mm high.

### MECH-04 (Medium, board): the coin cell's negative contact

**Evidence.** The cell's negative face rests on BT1 pad 2, a ø12 bare ENIG pad.
MUR p.1 gives the negative side as Ø21.0 (reference), so the pad sits well inside
it. But the B.Cu rule area under the cell allows tracks, and there are mask-covered
tracks under the negative cap: a track at y 20.51 (r 6.4 mm from the cell centre),
one at y 22.28 (r 6.3) and the pad's own GND tracks under mask from r 6 to 8.5.
Copper plus solder mask (~35 + 10-20 µm) stands **higher than the bare pad
(~35 µm + ENIG)**, so the rigid cell can sit on the mask ridges, with the pad
touching only through the clip force and the cap's flatness. Bare laminate with
mask (~15-25 µm) is lower than the pad, so the pad wins wherever there is no
copper under the mask.

**Fix (board, either or both).**
1. Change the B.Cu rule area under BT1 to `tracks not_allowed` inside r = 10.5 mm
   (the negative cap), except pad 2's own connection. Route the y 20.5 and
   y 22.3 tracks round it, and leave the pad's GND connection as a single short
   spoke.
2. Or raise the contact: add B.Paste to pad 2 (for example 7 dots of ø2 mm), so
   reflow leaves solder bumps ~0.05-0.1 mm proud of the mask.

The README (ENIG) and footprint description stay valid with either.

**Status: fixed** in [`2b8301f`](https://github.com/gemivnet/IRL2FA/commit/2b8301f) with option 1. A B.Cu rule area, a ring from the pad's edge
(r 6) to r 10.5, allows no tracks and no pour except in one 1.2 mm corridor straight
down (+y). The pad's ground connection runs through it as a single spoke (the GND track
and the pour, solid to the pad), and the solder mask is open over the corridor, so the
spoke is bare ENIG like the pad: nothing under the negative cap stands higher than the
contact. The ring stops at the pad's edge because a pad inside a keep-out made
Freerouting give up on the whole ground net. Option 2 (paste dots) was not needed.
`test_nothing_on_bcu_under_the_negative_cap_but_the_contact` checks tracks and pour.

### MECH-05 (Low, board): a via in the coin cell's insertion path

**Evidence.** A via on EPD_PWR_N at **(33.11, 30.06)** is in the strip the cell
slides over (x 26.25..50.75, from the cell to the bottom edge). It is tented by
the board default, but the cell's can (+, sharp crimped rim) scrapes across it on
every insertion. `pcb-design.md` says the strip holds no parts. The via rule area
covers only the cell's final position (y 3.55..28.45). No pads are in the strip.

**Fix (board).** Move the via out of x 25.9..51.1 below y 16, or extend the
B.Cu `vias not_allowed` rule area to the bottom edge. Then remove the xfail from
`test_cell_insertion_strip_has_no_vias`.

**Status: fixed** in [`2b8301f`](https://github.com/gemivnet/IRL2FA/commit/2b8301f): a rule area x 25.9..51.1 from the cell centre to the bottom
edge allows no vias on either layer, and the cell's own via keep-out grew to r 12.6;
the test passes without its xfail.

### MECH-06 (Medium, docs, fixed): assembly order

The README said "stick the display to the front of the board, then fold its flex
cable through the slot". The slot is 2 mm from the glass and the flex sticks
straight out sideways. With the glass already glued, the 6 mm stiffened tip can
only reach the slot by bending the flex back over the glass at its glued root.
The order is now: thread the flex through the slot from the front, lock it into
J1, then stick the display down (README "Build one", `case-design.md` assembly
step 2).

### MECH-07 (Low, models, fixed): two 3D models did not match their drawings

- **SKRHABE010** (`gen_models.py skrh()`): the stem was modelled as a 3.0 mm
  square aligned with the body, i.e. 45 deg out and 54 % too wide (MECH-02); the
  collars as Ø5.2/Ø4.0 (drawing: cone Ø~3.9 to 2.14, Ø3 shoulder to ~3.12); the
  body corners with a 0.6 mm cut (drawing: 8.6 across the operating diagonal,
  "c", i.e. a ~1.35 mm cut); the guide bosses 0.5 mm long (scaled: ~1.4 mm, so
  they stand ~0.6 mm proud of the 0.8 mm board's back; nothing is under them).
- **GDEY0213B74 flex** (`epd()`): 12.5 wide and 0.3 thick all along, lying on
  the PCB, wrapped tight round the slot, and running to x = 13.0 on the back.
  That runs through J1's body: J1's opening is at x 9.45 and the flex stops
  ~1.5 mm in (HRS p.4/p.8: the uncovered 1.5 ±0.3 mm ends at the housing edge).
  It now has the 14.30 flare and taper, 0.12 thickness, a 0.30 × 6.0 stiffener,
  and leaves the glass at the TFT face. The spare length is a loop under the
  board, and the flat stiffened end goes 1.5 mm into J1.

Every other model matched its drawing within tolerance (table below). The
regenerated models are in `hardware/lib/irl2fa.3dshapes`, `hardware/fab/irl2fa.step`
and the new `hardware/fab/irl2fa.glb`.

### MECH-08 (Low, assembly): glass placement against the rib

The rib clears the nominal glass by 0.4 mm and reaches 0.5 mm down its edge. The
glass is 59.2 / 29.2 ±0.2 (GD), so a glass that is hand-placed more than about
0.3 mm off lands under the rib's foot. The four screws then clamp the protective
sheet's edge. **Place the glass to the silkscreen corner marks (0.15 mm outside
the nominal glass) within ±0.2 mm, and test-fit the front shell before pressing
the adhesive down.** If first builds show this is hard, widen `part_clearance`
for the rib only, or print a placement jig.

### MECH-09 (Low, board.yaml): the stem data

`board.yaml` `joystick.stem_diameter: 3.0` is the shoulder, not the stem a cap
fits. Until it changes, the keytop data lives in the `.scad` (`stem_square`,
`stem_corner_r`, `stem_angle`, `stem_fit_max`, `joystick_collars`). **Status: fixed** in [`2b8301f`](https://github.com/gemivnet/IRL2FA/commit/2b8301f): `board.yaml` has `shoulder_diameter: 3.0` (was
`stem_diameter`), `stem_square`, `stem_corner_r` and `stem_fit_max`, and the `.scad`
reads them through `board_dims.scad`. Suggested
`board.yaml` additions, then `gen_dims.py` and the `.scad` reading them:

```yaml
joystick:
  stem_square: 1.95          # keytop stem across flats, R0.5, flats at 45 deg to the body (Alps Drawing No.2)
  stem_fit_max: 1.7          # "keytop fitting area 1.7 max"
```

### MECH-10 (Info, docs fixed): the flex has 2-3.5 mm to spare

Shortest path: off the glass at the TFT face (~0.86 mm), round the slot's right
wall with R ≥ 0.72 mm, under the board 0.5 mm off the back to J1's opening, and
1.5 ±0.3 mm in. That needs 10.8-12.1 mm; the flex is 14.3 ±0.3. The 2.2-3.5 mm
surplus makes a loose loop under the board, between the slot and J1. There is
room: the back cavity is 6.2 mm deep there, and nothing is placed at x < 9.1 in
y 7-23. The 6.0 ±0.5 mm stiffener lies flat into J1 at the longest. The first
bend starts beyond the 1.0 mm of silicone on the flex's back (GD p.35), 2.0 mm
from the glass to the slot. `pcb-design.md` "Things to verify" item 5 had "the
end seats with ~0.8 mm to spare", measured to J1's pad row instead of its
opening; it is corrected. Pin 1 lines up: the panel's pin 1 is 8.15 + 0.50 below
the top glass edge, at y = 9.55, and so is J1 pad 1; pad 24 is at y = 21.05.

### MECH-11 (Info, documented): reflashing means opening the case

J3 is on the back at (66.0, 26.2), inside the back shell and outside the battery
door (x 24.75..52.25). With the case shut, the Tag-Connect cannot reach it. With
the board out of the back shell, its courtyard (the plug's keep-out) is clear of
parts and of the H4 boss zone. Flash before closing the case. To reflash, take
out the four screws and lift the board out; the display stays on it. Now in
README "Build one" and `case-design.md`.

### MECH-12 (Info): IR windows

They line up in x within the test's 0.3 mm (emitter 56.5, detector 20.5). In z,
the lens axis is 1.15 mm below the PCB back (VSH p.5: lens Ø1.8 centred on the
2.3 mm face), at Z ≈ -1.95, inside the window's Z -3.4..-0.8. On axis the path
is open, and two devices butted top edge to top edge, one turned 180 deg, put
the lenses 3.9 mm apart on the same axis. The emitter's ±25 deg cone is clipped
above about +12 deg by the front shell's wall above the parting line. That
matters only for vertical misalignment. The default `ir_window = "open"` puts
nothing in the path at 940 nm. `"thin"` (0.6 mm skin) needs a filament that
passes 940 nm, and many black filaments are carbon-black and opaque to it. Test
with the devices themselves, not a phone camera: phone cameras often filter
940 nm.

### MECH-13 (Info): which face of the flex has the contacts

GDWT p.6 labels the side view "CONNECT SIDE" on the **front** (viewing) face and
shows the stiffener on the back view. GD p.7 draws the contacts solid in its
front view. After the 180 deg fold the contacts therefore face **away from the
PCB** on the back. The FH34SRJ is **top and bottom contact** (HRS p.1-2, "RJ"),
so it takes the flex either way up, and pin positions match whichever face is
up. The operation notes (HRS p.8, "top contact specification: insert with the
conductive surfaces facing up") describe exactly our case. A bottom-contact-only
substitute (such as the XUNPU FPC-05F-24PH20 or JUSHUO AFC01 listed in the parts
research) would not make contact. The FPC matches Hirose's recommended FPC
(HRS p.4): G = 12.5 ±0.05 (GD 12.50), 0.30 ±0.03 thick at the contacts
(GD 0.30 ±0.03), stiffener ≥ 2.5 mm (GD 6.0).

## Verified OK (per item)

| # | Item | Source | Ours | Drawing | Verdict |
|---|---|---|---|---|---|
| 1 | Glass outline | GD p.7 | 59.2 × 29.2 | 59.20 ±0.20 × 29.20 ±0.20 | OK |
| 1 | Glass thickness | GD p.7 | 1.0 (+0.2 adhesive) | 1.0 ±0.1 | OK: front skin 0.4 above, ≥0.25 at worst |
| 1 | Active area | GD p.7, GDWT p.6 | 48.55 × 23.70, 7.95 from the FPC edge | 48.55 × 23.7046; 2.70 from the far end, 2.75 / 2.745 sides | OK; window = AA + 0.3, centred by construction (test) |
| 1 | FPC exit side, position | GD p.7 | left short edge, 8.15 below the top | left, 8.15 ±0.3 below the top (front view) | OK |
| 1 | FPC tail width / length | GD p.7 | 12.5 / 14.3 | 12.50 / 14.30 ±0.30 from the glass edge | OK |
| 1 | FPC flare | GDWT p.6, GD p.7 | (not modelled) | 14.30 wide from 6.77, ~3.9 long | **MECH-01, MECH-03, MECH-07** |
| 1 | Stiffener | GD p.7 | (not modelled) | 6.00 ±0.5 long, 0.30 ±0.03 total | OK: flat into J1, Hirose 0.30 ±0.03 |
| 1 | Contacts face / connector type | GDWT p.6, HRS p.1-2 | FH34SRJ | front face; FH34SRJ top + bottom | OK (MECH-13) |
| 1 | Pin 1 to pin 1 | GD p.7, board | J1 pad 1 at y 9.55 | panel pin 1 at y 0.9 + 8.15 + 0.50 = 9.55 | OK (test) |
| 1 | Bend radius / glued root | GD p.35, IPC-2223 | 2.0 mm glass-to-slot | glue ≤1.0 on the back; R ≥ 6t = 0.72 | OK (test) |
| 1 | Reach into J1 | HRS p.4, p.8 | 14.3 | needs 10.8-12.1 | OK, 2.2-3.5 slack (MECH-10) |
| 1 | Slot width | GD, HRS | 1.4 | flex 0.12, stiffener 0.30 | OK (length: MECH-01) |
| 2 | Stem height | ALPS p.2 | 5.0 | 5 | OK |
| 2 | Stem section | ALPS p.2 | 3.0 square at 45 deg | □1.95 ±0.05 R0.5, flats at 45 deg to the body | **MECH-02 (fixed)** |
| 2 | Cap engagement | ALPS p.2 | 1.65 | ≤ 1.7 | OK, now 1.70 |
| 2 | Body height / span | ALPS p.2 | 1.8 / 7.5 square | h = 1.8; 7.35 × 7.5, 8.6 on the diagonal | OK; pocket clears it |
| 2 | Travel, force path | ALPS p.1-2 | cap only touches the stem | 0.25 mm at 4.3 mm, 1.23 N; 0.15 mm, 2.35 N centre | OK: ≤0.15 mm at the hole vs 0.4 gap; a press on the cap rim (r 3.5) tilts at ~1.3 N |
| 2 | Directions | ALPS p.2, `pins.yaml` | A left, B up, C down, D right | a/b/c/d at the body corners; footprint +45 deg (CCW) | OK (test); confirm on the first board |
| 3 | Clip height, plate, feet | MY | 5.70; 25.50 × 23.20; 35.80; pads 6.15 × 6.00, 36.80, 9.20 | same | OK |
| 3 | Clip vs door | MY | 0.5 mm | 5.70 ±0.20 | OK: ≥0.3 at the tallest (test) |
| 3 | + contact | MY | tongues on the + face | tongues in the ø6 holes 11.0 apart, 10.0 from the closed end, hanging 3.80 ±0.3 below the plate top | OK: they touch the + face ~6 mm from the centre; 3.1 mm spring travel for a 5.0 cell (vendor design; check the force) |
| 3 | − contact | MUR p.1 | ø12 pad at the cell centre | negative side Ø21.0 (ref.) | OK (MECH-04 for the height) |
| 3 | + legs vs cell | MY, MUR | pads' inner edge 12.25 from the centre | cell r 12.25 | OK: the can only meets its own + pads |
| 3 | Insertion path | board, case | strip x 26.25..50.75 to the bottom edge | — | OK: no pads or parts (test); one via (MECH-05); door opening 27.5 wide, wall section 29.9 |
| 4 | IR x | board, case | windows at 56.5 / 20.5 | — | OK (test) |
| 4 | IR z | VSH p.5 | axis 1.15 below the back, window Z -3.4..-0.8 | lens Ø1.8 on the 2.3 square face | OK (MECH-12) |
| 4 | IR package | VSH p.5 | 2.2 × 1.6 body, 2.55 deep, 2.3 high, leads 3.8, pads 0.9 × 1.2 @ 4.2 | same | OK |
| 5 | Back heights | datasheets | keep-out 5.8, cavity 6.2 | clip 5.9 max; everything else ≤ 2.5 (IR 2.3 +0.2, SOD-123 1.35, L1 1.5, 0805 ≤1.45, J1 1.0) | OK |
| 5 | Front heights | GD, ALPS | skin 1.6 above the PCB, 2.2 over the joystick | glass 1.3 max with adhesive; joystick 1.8 | OK |
| 5 | Screws | case | M2 × 6 from the front, 0.8 under the head | thread 4.4 mm in the boss, tip 1.8 above the floor | OK (scad asserts) |
| 5 | Bosses vs parts | board | ø4.6 + corner quadrant | — | OK: nothing within; glass 0.4 from the front bosses |
| 5 | Tag-Connect | board | J3 on the back | TC2030 keep-out (courtyard) | OK with the board out (MECH-11) |
| 5 | Lanyard tab | case | 4 mm thick, Ø4 eye, 2.2 mm ligament (1.6 at the chamfers) | — | OK: loaded in the layer plane, ~400 N in PETG |
| 6 | Interference, every part model vs every printed part | `irl2fa.glb`, STLs | — | — | OK after the fixes (only the cap's crush ribs on the stem, by design); found MECH-03 |
| 6 | Models vs drawings | all | — | — | OK after MECH-07 (table in `test_mechanical.py`) |

## Checks added

`case/tests/test_mechanical.py` (runs in `make case` and the case CI workflow, ~25 s):

- `DRAWINGS`: every dimension above with its source and page;
- model checks: the part models as placed on the board (`hardware/fab/irl2fa.glb`,
  now exported by `make fab`) against `DRAWINGS`: glass, flex flare at the slot,
  stiffener and tail in J1, joystick stem (size and that its flats are parallel
  to the board edges), body diagonal and height, shoulder, FH34 height, width and
  depth, clip height, plate and feet, IR height, depth, lead span, lens axis and
  lens tip on the top edge;
- interference: surface points of every part model against the four printed parts
  in their assembled positions (the stem inside the cap's socket excepted). It
  catches MECH-03 on the old front shell;
- flex: the slot covers the flare with tolerances (**xfail MECH-01**), the fold
  starts beyond the glued root with R ≥ 6t, pin 1 to pin 1, the flex reaches J1
  at its shortest and its stiffener lies flat at its longest;
- joystick: the cap socket's size, angle and engagement against the stem
  (measured on the printed STL, across the ribs), and the 45 deg turn against
  `pins.yaml`;
- coin cell: pad 2 at the cell centre inside the negative cap, the + legs outside
  the cell, no pads in the insertion strip, **no vias in it (xfail MECH-05)**, and
  the tallest clip against the door.

`case/tests/test_case.py` now uses the flare and the drawing's collars.
`.github/workflows/case.yml` also runs on changes to the board, `pins.yaml` and
the board mesh.

## Only a physical build can verify

1. **Flex through the (enlarged) slot.** The flare dimensions come from the WT
   sibling drawing plus scaling, so measure a panel: flare width and length,
   whether the flex lies flat over the slot, the loop under the board, and that
   J1 locks with the stiffener flat.
2. **TFT thickness under the flex bond** (we assumed ~0.6 mm, so the flex leaves
   the glass ~0.86 mm up). It moves the flex height under the front shell by a
   few tenths; the case leaves 0.7 mm above it.
3. **Cap retention** on the 1.95 mm stem with 0.1 mm crush ribs in PETG and in
   PLA. Pull-off should need clearly more than a fingernail; tune
   `stem_rib_interference` or glue it.
4. **Joystick directions** after the 45 deg turn (the mapping assumes "pushing
   towards corner a closes contact A", which the Alps drawing implies but does
   not state).
5. **Coin cell**: contact on the negative pad under the clip force (MECH-04), the
   clip tongues' force on a 4.6-5.0 mm cell, insertion and removal through the
   door with a fingernail in the clip's notch. Watch for a brief short: the cell's
   + rim slides across the bare pad while its − face is also on it.
6. **Glass placement** to the corner marks against the rib (MECH-08), and the
   front shell closing without pressing on the glass.
7. **IR**: link range with the open windows, and with `"thin"` in the filament
   actually used.
8. **Print tolerances**: the door detent, the lip/rebate and the screw bosses
   (PLA vs PETG), as `case-design.md` "Tolerance tuning" describes.
