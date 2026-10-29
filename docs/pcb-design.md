# PCB design

<p align="center">
  <img src="images/pcb/pcb-front.png" alt="PCB front" width="420" />
  <img src="images/pcb/pcb-back.png" alt="PCB back" width="420" />
</p>

| | |
|-|-|
| Size | 80 × 34 mm, corner radius 3 mm, 1.0 mm FR-4 |
| Layers | 2, with a ground pour on both |
| Rules | 0.15 mm signal / 0.4 mm power tracks, 0.15 mm clearance, 0.6/0.3 mm vias. Within JLCPCB's standard 2-layer process (board minimum set to their 0.1 mm capability) |
| Assembly | both sides: the joystick on the front, everything else on the back |
| Files | `hardware/irl2fa.kicad_sch`, `hardware/irl2fa.kicad_pcb` (KiCad 9) |

## Layout

**Front (screen side).**

- The 59.2 × 29.2 mm e-paper glass sits on the left, stuck to the board with 0.2 mm adhesive. Front copper under it is ground pour only.
- The Alps joystick is on the right, rotated 45° because its contacts sit at the body's corners.
- `hardware/pins.yaml` maps the four contacts to up, down, left and right.

**Back, left to right.**

- **The display's flex cable** comes through a 1.4 × 14 mm slot just left of the glass. It plugs into the Hirose FH34 connector (J1), which opens towards the slot. The panel's pin 1 is at the top, so after the 180° fold it is still at the top, and `gen_pcb.py` checks that the connector's pin 1 is too.
- **The e-paper booster** sits in the top-left corner: L1, Q3, D1-D3, C10, C11, R6, R7. The panel's decoupling capacitors (C12-C19) are in a column beside J1. The column has a gap level with J1's signal pins, so they can escape towards the MCU.
- **The CR2450 holder** is in the middle. Its courtyard follows the real outline (terminal tabs, body and cell circle), so the corners the round cell leaves free can hold parts.
- **On the right:**
  - the MCU (U1) with its decoupling
  - the RTC (U2)
  - the reverse-battery FET (Q1) and bulk capacitors (C1, C2)
  - the Tag-Connect TC2030 programming pads (J3)
  - the test points TP1 (VBAT) and TP2 (GND), for measuring sleep current with a meter in series with the cell
- **IR emitter and detector.** Both are side-looking and sit on the back at the **top edge**: the emitter (D4) at x = 58 mm and the detector (Q5) at x = 22 mm. Two devices held top edge to top edge, one turned 180°, line up the emitter of one with the detector of the other. The lens tips are flush with the board edge.

**Mounting.** Four M2 plated holes, tied to ground, at the corner radius centres.

## How the board was made, and how to change it

The first version of the schematic and PCB was generated from
`hardware/tools/design.py` by `hardware/tools/build_pcb.sh`:

1. `gen_schematic.py` writes the schematic. Every pin gets a wire stub and a net label, and symbols come from KiCad's stock libraries plus `hardware/lib/irl2fa.kicad_sym`.
2. `gen_pcb.py` places the footprints from the netlist, draws the outline and edge keepouts, exports Specctra DSN, and imports the routes.
3. [Freerouting](https://github.com/freerouting/freerouting) autoroutes the board in Docker, with extra passes that start from the previous routes until everything is connected.
4. `gen_pcb.py` adds the ground pours on both layers and fills them.
5. ERC and DRC with schematic parity must come back clean.

**From now on, the KiCad files are the source.** Open them in KiCad 9 and edit them like any project. Hand-tidy the autorouted tracks if you like: CI checks the result, not how it was made. `build_pcb.sh` overwrites both files, so only re-run it if you want to start again from `design.py`.

Footprints, symbols and 3D models for the parts KiCad doesn't ship were drawn from the manufacturers' datasheets by `hardware/tools/gen_footprints.py` and `gen_models.py`. See [hardware/lib/README.md](../hardware/lib/README.md) for sources and dimensions.

## Checks

Every push runs the following. Details are in [hardware-testing.md](hardware-testing.md).

- ERC
- DRC with schematic parity
- netlist against `pins.yaml`
- SPICE benches
- the power budget
- a check that the fab outputs are current
- Gerber checks
- the case fit test

## Things to verify on the first boards

These are modelled or taken from datasheets but not yet measured:

1. **E-paper image orientation.** The firmware assumes the panel's RAM origin from Good Display's drawing. If the picture is upside down, build with `-DEPD_ROTATE_180=ON`.
2. **IR range and speed.** The plan is 4800 baud over 2-5 cm, set by the phototransistor's 20-40 µs edges.
3. **Sleep current.** The budget is about 1.2 µA typical; measure at TP1/TP2.
4. **Joystick direction mapping** after the 45° rotation. If it's wrong, fix it in `pins.yaml` and regenerate `pins.h`.
5. **Fit of the FPC fold.** The 14.3 mm tail reaches about x = 13.5 mm on the back; J1 sits at x = 13 so the end seats with ~0.8 mm to spare. The datasheet tolerance is ±0.3 mm.
