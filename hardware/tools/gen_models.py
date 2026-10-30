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
    """Alps SKRH catalog, Drawing No.2 (and the SKRHABE010 product page drawing):
    body 7.35 x 7.5 x 1.8 with corners cut so that it is 8.6 across the operating
    diagonal ("c"); a cone on the body to 2.14 (base ~3.9, scaled); the dia-3
    shoulder to ~3.12 (scaled; "3" is dimensioned); and the keytop stem, a 1.95 mm
    square with R0.5 corners whose flats sit at 45 deg to the body, to 5.0. Guide
    bosses dia 0.65 / 0.95 at y -1.9 / +1.9, ~1.4 long below the body (scaled)."""
    bx, by = 7.35 / 2, 7.5 / 2
    cut = bx + by - 8.6 / math.sqrt(2)                     # corner cut that gives c = 8.6
    frame = box(-bx, bx, -by, by, 0, 1.8).edges("|Z").chamfer(cut)
    cone = cq.Solid.makeCone(3.9 / 2, 3.45 / 2, 2.14 - 1.8, pnt=cq.Vector(0, 0, 1.8))
    shoulder = cyl_z(0, 0, 1.5, 2.14, 3.12)
    s, r = 1.95, 0.5
    stem = (cq.Workplane("XY").rect(s, s).extrude(5.0 - 3.12).edges("|Z").fillet(r)
            .rotate((0, 0, 0), (0, 0, 1), 45).translate((0, 0, 3.12)))
    parts = [(frame, "metal"), (cq.Workplane("XY").add(cone), "dark"), (shoulder, "black"), (stem, "black")]
    py = (3.85 + 1.85) / 4
    for x in (-1, 1):
        for y in (-py, 0, py):
            parts.append((box(x * bx, x * 4.1, y - 0.3, y + 0.3, 0, 0.15), "metal"))
    for sg in (-1, 1):
        parts.append((box(-0.8, 0.8, sg * by, sg * 4.4, 0, 0.15), "metal"))
    parts.append((cyl_z(0, -1.9, 0.65 / 2, -1.4, 0), "black"))
    parts.append((cyl_z(0, 1.9, 0.95 / 2, -1.4, 0), "black"))
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
# GDEY0213B74 drawing (spec rev 1.0 sec. 4) and its sibling GDEM0213B74WT, which
# dimensions the same flex: 14.30 long from the glass edge, 12.5 wide with its edge
# 8.15 below the top glass edge, but 14.30 wide (from 6.77) for its first ~3.9 mm,
# narrowing to 12.5 by ~4.7 mm (scaled from the drawing). 0.12 thick, 0.30 over the
# 6.0 mm stiffener at the end. Contacts on the viewing (front) face.
FPC = dict(L=14.3, w=12.5, y0=8.15, flare_w=14.30, flare_y0=6.77, flare_end=3.9, taper_end=4.7,
           t=0.12, t_stiff=0.30, stiff=6.0)
TFT_T = 0.6          # TFT glass under the FPC bond (the 1.0 panel minus front plane laminate; estimate)
J1_ENTRY_X, J1_ENTRY_Z, J1_DEPTH = 9.45, 0.5, 1.5   # board x of J1's FPC opening, its height, insertion


def fpc_path():
    """Centre line of the folded FPC in board (x, z) as segments (x0, z0, x1, z1, s0).
    Leaves the glass horizontally at the TFT front face, turns down through the
    slot (hugging its right wall), drops into a slack loop under the board, comes
    back up and runs flat (stiffener) into J1. The loop takes up the length the
    straight path does not need."""
    t = FPC["t"]
    pcb_t = BOARD["board"]["thickness"]
    gl, sl = BOARD["display"]["glass"], BOARD["fpc_slot"]
    ze = BOARD["display"]["adhesive"] + TFT_T + t / 2
    xv = sl["x"] + sl["w"] - t / 2 - 0.15
    R1, Rl, R2 = 0.8, 0.7, 0.5
    zj = -pcb_t - J1_ENTRY_Z
    tip = J1_ENTRY_X + J1_DEPTH
    x_up = xv + 2 * Rl
    fixed = (gl["x"] - (xv + R1)) + math.pi / 2 * R1 + math.pi * Rl + math.pi / 2 * R2 + (tip - (x_up + R2))
    # vertical down from ze - R1 to z_low, back up from z_low to zj - R2
    z_low = ((ze - R1) + (zj - R2) - (FPC["L"] - fixed)) / 2
    segs, s = [], 0.0

    def line(x0, z0, x1, z1):
        nonlocal s
        segs.append((x0, z0, x1, z1, s))
        s += math.hypot(x1 - x0, z1 - z0)

    def arc(cx, cz, r, a0, a1, n=8):
        for i in range(n):
            u0, u1 = a0 + (a1 - a0) * i / n, a0 + (a1 - a0) * (i + 1) / n
            line(cx + r * math.cos(u0), cz + r * math.sin(u0), cx + r * math.cos(u1), cz + r * math.sin(u1))

    line(gl["x"], ze, xv + R1, ze)
    arc(xv + R1, ze - R1, R1, math.pi / 2, math.pi)
    line(xv, ze - R1, xv, z_low)
    arc(xv + Rl, z_low, Rl, math.pi, 2 * math.pi, 12)
    line(x_up, z_low, x_up, zj - R2)
    arc(x_up + R2, zj - R2, R2, math.pi, math.pi / 2)
    line(x_up + R2, zj, tip, zj)
    return segs, s, z_low


def fpc_edges(s):
    """Top/bottom edge of the flex (mm below the top glass edge) at arc length s."""
    f = FPC
    y0n, y1n = f["y0"], f["y0"] + f["w"]
    y0f, y1f = f["flare_y0"], f["flare_y0"] + f["flare_w"]
    if s <= f["flare_end"]:
        return y0f, y1f
    if s >= f["taper_end"]:
        return y0n, y1n
    u = (s - f["flare_end"]) / (f["taper_end"] - f["flare_end"])
    return y0f + u * (y0n - y0f), y1f + u * (y1n - y1f)


def epd():
    gw, gh, gt, adh = 59.2, 29.2, 1.0, 0.2
    glass = box(-gw / 2, gw / 2, -gh / 2, gh / 2, adh, adh + gt)
    aa_x0, aa_y0 = -gw / 2 + 7.95, -(23.7046 / 2)
    active = box(aa_x0, aa_x0 + 48.55, aa_y0, -aa_y0, adh + gt, adh + gt + 0.01)
    gl = BOARD["display"]["glass"]
    ox = gl["x"] + gw / 2                       # board x of the footprint origin
    segs, total, z_low = fpc_path()
    fpc = None
    cuts = [FPC["flare_end"], FPC["taper_end"], FPC["L"] - FPC["stiff"]]
    pieces = []
    for x0, z0, x1, z1, s0 in segs:             # split straight runs at the width/thickness changes
        L = math.hypot(x1 - x0, z1 - z0)
        marks = [0.0] + [c - s0 for c in cuts if s0 < c < s0 + L] + [L]
        if L > 1.0 and s0 < FPC["taper_end"] and s0 + L > FPC["flare_end"]:
            marks += [FPC["flare_end"] - s0 + (FPC["taper_end"] - FPC["flare_end"]) * k / 4 for k in (1, 2, 3)]
        marks = sorted(m for m in set(marks) if 0 <= m <= L)
        for a, b in zip(marks, marks[1:]):
            pieces.append((x0 + (x1 - x0) * a / L, z0 + (z1 - z0) * a / L,
                           x0 + (x1 - x0) * b / L, z0 + (z1 - z0) * b / L, s0 + a, s0 + b))
    for x0, z0, x1, z1, sa, sb in pieces:
        L = math.hypot(x1 - x0, z1 - z0)
        if L < 1e-6:
            continue
        sm = (sa + sb) / 2
        ya, yb = fpc_edges(sm)
        th = FPC["t_stiff"] if sm > FPC["L"] - FPC["stiff"] else FPC["t"]
        ang = math.atan2(z1 - z0, x1 - x0)
        seg = (cq.Workplane("XY").box(L + 0.02, yb - ya, th, centered=(False, False, True))
               .translate((-0.01, 0, 0))
               .rotate((0, 0, 0), (0, 1, 0), -math.degrees(ang))
               .translate((x0 - ox, -(yb - gh / 2), z0)))
        fpc = seg if fpc is None else fpc.union(seg)
    print(f"  EPD FPC: {total:.2f} mm modelled, slack loop down to z = {z_low - 0.7:.2f}, "
          f"tip at board x = {J1_ENTRY_X + J1_DEPTH:.2f}")
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
