#!/usr/bin/env python3
# SPDX-License-Identifier: CERN-OHL-S-2.0
"""Generate simple, dimensionally accurate STEP models for the IRL2FA custom footprints.

Writes hardware/lib/irl2fa.3dshapes/<footprint name>.step. Needs CadQuery
(pip install cadquery). Geometry is written in FOOTPRINT coordinates
(x right, y DOWN, z up from the PCB surface) and converted to KiCad's model
frame (y up) by the helpers below, so every model lines up with its footprint
with offset 0 / rotation 0. Dimensions: see hardware/lib/README.md.

Usage:  python3 hardware/tools/gen_models.py
"""
from __future__ import annotations

import math
import os

import cadquery as cq
import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "lib", "irl2fa.3dshapes"))
BOARD = yaml.safe_load(open(os.path.join(HERE, "..", "board.yaml")))

C = {
    "black": cq.Color(0.08, 0.08, 0.08), "dark": cq.Color(0.2, 0.2, 0.22), "metal": cq.Color(0.75, 0.75, 0.78),
    "gold": cq.Color(0.85, 0.68, 0.25), "white": cq.Color(0.92, 0.92, 0.9), "beige": cq.Color(0.85, 0.8, 0.68),
    "glass": cq.Color(0.88, 0.9, 0.9), "epd": cq.Color(0.78, 0.78, 0.76), "fpc": cq.Color(0.8, 0.5, 0.1),
    "lens_ir": cq.Color(0.35, 0.4, 0.6), "cell": cq.Color(0.82, 0.82, 0.85),
}


# --- helpers in footprint coordinates --------------------------------------
def box(x0, x1, y0, y1, z0, z1):
    x0, x1 = sorted((x0, x1)); y0, y1 = sorted((y0, y1)); z0, z1 = sorted((z0, z1))
    return cq.Workplane("XY").box(x1 - x0, y1 - y0, z1 - z0, centered=False).translate((x0, -y1, z0))


def cyl_z(cx, cy, r, z0, z1):
    return cq.Workplane("XY").circle(r).extrude(z1 - z0).translate((cx, -cy, z0))


def ring_y(cx, cz, r_in, r_out, y_center, half_w):
    """Ring around an axis parallel to Y (footprint), extruded +-half_w about y_center."""
    o = cq.Workplane("XZ").center(cx, cz).circle(r_out).extrude(half_w, both=True)
    i = cq.Workplane("XZ").center(cx, cz).circle(r_in).extrude(half_w, both=True)
    return o.cut(i).translate((0, -y_center, 0))


def save(name, parts):
    os.makedirs(OUT, exist_ok=True)
    assy = cq.Assembly(name=name)
    for i, (shape, color) in enumerate(parts):
        assy.add(shape, name=f"{name}_{i}", color=C[color])
    path = os.path.join(OUT, name + ".step")
    if hasattr(assy, "export"):
        assy.export(path)
    else:  # older CadQuery
        assy.save(path)
    print("wrote", os.path.relpath(path))


# --- a. Alps SKRHABE010 -------------------------------------------------------
def skrh():
    bx, by = 7.35 / 2, 7.5 / 2
    frame = box(-bx, bx, -by, by, 0, 1.8).edges("|Z").chamfer(0.6)
    collar = cyl_z(0, 0, 2.6, 1.8, 2.14)
    shoulder = cyl_z(0, 0, 2.0, 2.14, 3.0)
    stem = box(-1.5, 1.5, -1.5, 1.5, 3.0, 5.0).edges("|Z").fillet(0.5)   # dia 3 stem (square-ish)
    parts = [(frame, "metal"), (collar, "dark"), (shoulder, "black"), (stem, "black")]
    py = (3.85 + 1.85) / 4
    for x in (-1, 1):
        for y in (-py, 0, py):
            parts.append((box(x * bx, x * 4.1, y - 0.3, y + 0.3, 0, 0.15), "metal"))
    for s in (-1, 1):
        parts.append((box(-0.8, 0.8, s * by, s * 4.4, 0, 0.15), "metal"))
    parts.append((cyl_z(0, -1.9, 0.65 / 2, -0.5, 0), "black"))
    parts.append((cyl_z(0, 1.9, 0.95 / 2, -0.5, 0), "black"))
    save("SW_Nav_Alps_SKRHABE010", parts)


# --- b. Hirose FH34SRJ-24S ---------------------------------------------------------
def fh34():
    front, rear = 3.05, -0.75
    D, A = 13.38, 14.0
    housing = box(-D / 2, D / 2, 0.15, front, 0, 0.9)
    housing = housing.cut(box(-6.1, 6.1, front - 1.8, front + 0.1, 0.35, 0.65))   # FPC entry slot
    actuator = box(-A / 2, A / 2, rear, 0.9, 0.55, 1.0)
    parts = [(housing, "beige"), (actuator, "dark")]
    for i in range(24):
        x = 5.75 - 0.5 * i
        parts.append((box(x - 0.1, x + 0.1, -0.35, 0.6, 0, 0.12), "gold"))
    for s in (-1, 1):
        parts.append((box(s * 6.55, s * 6.95, 2.1, 2.9, 0, 0.6), "metal"))
    save("Hirose_FH34SRJ-24S-0.5SH_1x24-1MP_P0.50mm_Horizontal", parts)


# --- c. MYOUNG MY-2450-01 CR2450 clip ------------------------------------------
def my2450():
    """Drawing MY-CP-0106: 0.25 mm plate 5.70 above the PCB, legs to 6.15 x 6.0 feet,
    stop flap on the closed end, two contact tongues in the 6 mm holes. The cell
    lies on the PCB's negative pad. Outline shared with the footprint."""
    from gen_footprints import MY2450, my2450_geometry
    m, g = MY2450, my2450_geometry()
    t, h = 0.25, m["height"]
    hw = m["plate_w"] / 2
    outline = [(x, -y) for x, y in g["outline"]]
    plate = cq.Workplane("XY").polyline(outline).close().extrude(t).translate((0, 0, h - t))
    hole_y = g["y_top"] + 10.0                       # 6 mm holes, 11.0 apart
    for sx in (-1, 1):
        plate = plate.cut(cyl_z(sx * 5.5, hole_y, 3.0, h - t - 0.1, h + 0.1))
    parts = [(plate, "metal")]
    for sx in (-1, 1):
        # tongue: pressed down from the hole edge onto the cell's + face
        parts.append((box(sx * 5.5 - 1.2, sx * 5.5 + 1.2, hole_y - 2.8, hole_y + 1.0, 5.0, h - t), "metal"))
        py0, py1 = g["pad_y"] - m["pad_h"] / 2, g["pad_y"] + m["pad_h"] / 2
        parts.append((box(sx * (hw - t), sx * hw, py0, py1, 0, h), "metal"))            # leg
        parts.append((box(sx * hw, sx * 35.8 / 2, py0, py1, 0, t), "metal"))   # foot, 35.80 over the feet
    parts.append((box(-m["top_w"] / 2, m["top_w"] / 2, g["y_top"], g["y_top"] + t, h - 3.3, h), "metal"))  # stop
    parts.append((cyl_z(0, 0, m["cell_r"], 0.035, 5.035), "cell"))   # CR2450 cell, 5.0 thick
    save("BatteryHolder_MYOUNG_MY-2450-01_CR2450", parts)


# --- d. Vishay MiniLED side view -----------------------------------------------
def sidelooker(name, body_color, lens_color):
    front, back, tip, h, axis_z, r = -0.42, 1.18, -1.37, 2.3, 1.15, 0.9
    body = box(-1.1, 1.1, front, back, 0.05, h)
    sph = cq.Workplane("XY").sphere(r).translate((0, -(tip + r), axis_z))
    lens = sph.intersect(box(-r, r, tip - 0.1, tip + r, axis_z - r, axis_z + r))
    lens = lens.union(cq.Workplane("XZ").center(0, axis_z).circle(r).extrude(-(front - (tip + r)))
                      .translate((0, -front, 0)))
    parts = [(body, body_color), (lens, lens_color)]
    for s in (-1, 1):
        parts.append((box(s * 1.1, s * 1.9, -0.4, 0.4, 0, 0.15), "metal"))
        parts.append((box(s * 1.1, s * 1.25, -0.4, 0.4, 0, 1.1), "metal"))
    save(name, parts)


# --- e. Good Display GDEY0213B74 + folded FPC ------------------------------------
def epd():
    gw, gh, gt, adh = 59.2, 29.2, 1.0, 0.2
    glass = box(-gw / 2, gw / 2, -gh / 2, gh / 2, adh, adh + gt)
    aa_x0, aa_y0 = -gw / 2 + 7.95, -(23.7046 / 2)
    active = box(aa_x0, aa_x0 + 48.55, aa_y0, -aa_y0, adh + gt, adh + gt + 0.01)
    # FPC: 12.5 wide, 0.3 thick, 14.3 long from the glass edge, centred 8.15+6.25 from the top glass edge
    w, t, L = 12.5, 0.3, 14.3
    yc = -gh / 2 + 8.15 + w / 2
    pcb_t = BOARD["board"]["thickness"]
    sl, gl = BOARD["fpc_slot"], BOARD["display"]["glass"]
    xv = sl["x"] + sl["w"] / 2 - (gl["x"] + gw / 2) - 0.15   # vertical run centre, inside the slot
    R = 0.5                          # bend radius at the FPC centre line
    zt = adh + t / 2                 # top run centre line (on the PCB front, under the glass level)
    zb = -pcb_t - t / 2              # bottom run centre line (against the PCB back)
    x_b = xv + R                     # bend centres x
    top = box(x_b, -gw / 2, yc - w / 2, yc + w / 2, zt - t / 2, zt + t / 2)
    vert = box(xv - t / 2, xv + t / 2, yc - w / 2, yc + w / 2, zb + R, zt - R)
    q1 = ring_y(x_b, zt - R, R - t / 2, R + t / 2, yc, w / 2).intersect(
        box(xv - 1, x_b, yc - w, yc + w, zt - R, zt + 1))
    q2 = ring_y(x_b, zb + R, R - t / 2, R + t / 2, yc, w / 2).intersect(
        box(xv - 1, x_b, yc - w, yc + w, zb - 1, zb + R))
    used = (-gw / 2 - x_b) + 2 * (math.pi / 2 * R) + ((zt - R) - (zb + R))
    back_len = L - used
    bottom = box(x_b, x_b + back_len, yc - w / 2, yc + w / 2, zb - t / 2, zb + t / 2)
    fpc = top.union(q1).union(vert).union(q2).union(bottom)
    print(f"  EPD FPC: {back_len:.2f} mm run on the back after the bend, ends at x={x_b + back_len:.2f} "
          f"(footprint coords) / board x={x_b + back_len + gl["x"] + gw / 2:.2f}")
    save("EPD_GoodDisplay_GDEY0213B74_Outline", [(glass, "glass"), (active, "epd"), (fpc, "fpc")])


# --- Micro Crystal C7 (RV-3032-C7): KiCad ships the footprint but no model -----
def c7():
    body = box(-0.75, 0.75, -1.6, 1.6, 0, 0.55)
    lid = box(-0.65, 0.65, -1.5, 1.5, 0.55, 0.8)
    mark = cyl_z(-0.4, -1.2, 0.12, 0.8, 0.81)
    save("MicroCrystal_C7_SON-8_1.5x3.2mm_P0.9mm", [(body, "white"), (lid, "metal"), (mark, "black")])


def main():
    skrh()
    fh34()
    my2450()
    sidelooker("LED_Vishay_VSMB2948SL_SideView", "white", "lens_ir")
    sidelooker("PhotoTransistor_Vishay_VEMT2523SLX01_SideView", "white", "black")
    epd()
    c7()


if __name__ == "__main__":
    main()
