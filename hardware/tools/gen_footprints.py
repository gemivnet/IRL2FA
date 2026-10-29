#!/usr/bin/env python3
# SPDX-License-Identifier: CERN-OHL-S-2.0
"""Generate the IRL2FA custom KiCad 9 footprints into hardware/lib/irl2fa.pretty/.

Every footprint here is drawn from the manufacturer's datasheet land-pattern
drawing (sources and dimensions are listed in hardware/lib/README.md and in each
footprint's description). Nothing is copied from third-party CAD libraries.

Conventions (KiCad): units mm, x right, y DOWN, footprint seen from the top
(component side). 3D models are referenced through ${KIPRJMOD} so the project
renders without a global library install; gen_models.py writes them.

Usage:  python3 hardware/tools/gen_footprints.py
"""
from __future__ import annotations

import math
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "lib", "irl2fa.pretty"))
MODEL_DIR = "${KIPRJMOD}/lib/irl2fa.3dshapes"

W_SILK, W_FAB, W_CRT = 0.12, 0.10, 0.05
CRT_CLEAR = 0.25

_NTFS_BAD = re.compile(r'[<>:"/\\|?*]')


def f(v: float) -> str:
    """Format a coordinate: round to 1 um, drop trailing zeros."""
    s = f"{round(v, 4):.4f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def crt(v: float) -> float:
    """Round a courtyard coordinate outward-ish to the 0.01 mm grid."""
    return round(v * 100) / 100


class Footprint:
    def __init__(self, name: str, descr: str, tags: str, attr: str, datasheet: str):
        if _NTFS_BAD.search(name) or name.endswith((".", " ")):
            raise ValueError(f"footprint name not NTFS-safe: {name}")
        self.name, self.descr, self.tags, self.attr, self.datasheet = name, descr, tags, attr, datasheet
        self.items: list[str] = []
        self.ref_at = (0.0, 0.0)
        self.val_at = (0.0, 0.0)
        self.fab_ref_at = (0.0, 0.0)
        self.model = True

    # graphics -------------------------------------------------------------
    def line(self, layer, x1, y1, x2, y2, w=None):
        w = w if w is not None else _w(layer)
        self.items.append(
            f'\t(fp_line\n\t\t(start {f(x1)} {f(y1)})\n\t\t(end {f(x2)} {f(y2)})\n'
            f'\t\t(stroke\n\t\t\t(width {f(w)})\n\t\t\t(type solid)\n\t\t)\n\t\t(layer "{layer}")\n\t)')

    def poly_line(self, layer, pts, closed=False, w=None):
        pts = list(pts) + ([pts[0]] if closed else [])
        for a, b in zip(pts, pts[1:]):
            self.line(layer, a[0], a[1], b[0], b[1], w)

    def rect(self, layer, x1, y1, x2, y2, w=None):
        w = w if w is not None else _w(layer)
        self.items.append(
            f'\t(fp_rect\n\t\t(start {f(x1)} {f(y1)})\n\t\t(end {f(x2)} {f(y2)})\n'
            f'\t\t(stroke\n\t\t\t(width {f(w)})\n\t\t\t(type solid)\n\t\t)\n\t\t(fill no)\n\t\t(layer "{layer}")\n\t)')

    def circle(self, layer, cx, cy, r, w=None, fill=False):
        w = w if w is not None else _w(layer)
        self.items.append(
            f'\t(fp_circle\n\t\t(center {f(cx)} {f(cy)})\n\t\t(end {f(cx + r)} {f(cy)})\n'
            f'\t\t(stroke\n\t\t\t(width {f(w)})\n\t\t\t(type solid)\n\t\t)\n\t\t(fill {"yes" if fill else "no"})\n\t\t(layer "{layer}")\n\t)')

    def arc(self, layer, cx, cy, r, a0, a1, w=None):
        """Arc around (cx, cy) from angle a0 to a1 (degrees, KiCad screen coords)."""
        w = w if w is not None else _w(layer)
        am = (a0 + a1) / 2
        p = lambda a: (cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a)))
        (sx, sy), (mx, my), (ex, ey) = p(a0), p(am), p(a1)
        self.items.append(
            f'\t(fp_arc\n\t\t(start {f(sx)} {f(sy)})\n\t\t(mid {f(mx)} {f(my)})\n\t\t(end {f(ex)} {f(ey)})\n'
            f'\t\t(stroke\n\t\t\t(width {f(w)})\n\t\t\t(type solid)\n\t\t)\n\t\t(layer "{layer}")\n\t)')

    def poly(self, layer, pts, w=None, fill=False):
        w = w if w is not None else _w(layer)
        xy = " ".join(f"(xy {f(x)} {f(y)})" for x, y in pts)
        self.items.append(
            f'\t(fp_poly\n\t\t(pts {xy})\n\t\t(stroke\n\t\t\t(width {f(w)})\n\t\t\t(type solid)\n\t\t)\n'
            f'\t\t(fill {"yes" if fill else "no"})\n\t\t(layer "{layer}")\n\t)')

    def text(self, layer, s, x, y, size=1.0, thick=None):
        thick = thick if thick is not None else round(size * 0.15, 3)
        self.items.append(
            f'\t(fp_text user "{s}"\n\t\t(at {f(x)} {f(y)} 0)\n\t\t(layer "{layer}")\n'
            f'\t\t(effects\n\t\t\t(font\n\t\t\t\t(size {f(size)} {f(size)})\n\t\t\t\t(thickness {f(thick)})\n\t\t\t)\n\t\t)\n\t)')

    def arrow(self, layer, x1, y1, x2, y2, head=0.5, w=None):
        """Line from (x1,y1) to (x2,y2) with an arrow head at (x2,y2)."""
        self.line(layer, x1, y1, x2, y2, w)
        a = math.atan2(y2 - y1, x2 - x1)
        for da in (math.radians(150), math.radians(-150)):
            self.line(layer, x2, y2, x2 + head * math.cos(a + da), y2 + head * math.sin(a + da), w)

    # pads -------------------------------------------------------------------
    def smd(self, num, x, y, w, h, shape="roundrect", rratio=0.25):
        extra = f"\n\t\t(roundrect_rratio {rratio})" if shape == "roundrect" else ""
        self.items.append(
            f'\t(pad "{num}" smd {shape}\n\t\t(at {f(x)} {f(y)})\n\t\t(size {f(w)} {f(h)})\n'
            f'\t\t(layers "F.Cu" "F.Paste" "F.Mask"){extra}\n\t)')

    def npth(self, x, y, d):
        self.items.append(
            f'\t(pad "" np_thru_hole circle\n\t\t(at {f(x)} {f(y)})\n\t\t(size {f(d)} {f(d)})\n'
            f'\t\t(drill {f(d)})\n\t\t(layers "*.Cu" "*.Mask")\n\t)')

    def courtyard_rect(self, x1, y1, x2, y2):
        self.rect("F.CrtYd", crt(x1), crt(y1), crt(x2), crt(y2))

    # output -----------------------------------------------------------------
    def render(self) -> str:
        def prop(key, val, at, layer, hide):
            hid = "\n\t\t(hide yes)" if hide else ""
            return (f'\t(property "{key}" "{val}"\n\t\t(at {f(at[0])} {f(at[1])} 0)\n\t\t(layer "{layer}"){hid}\n'
                    f'\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 1 1)\n\t\t\t\t(thickness 0.15)\n\t\t\t)\n\t\t)\n\t)')

        out = [f'(footprint "{self.name}"',
               "\t(version 20241229)",
               '\t(generator "irl2fa_gen_footprints")',
               '\t(generator_version "1.0")',
               '\t(layer "F.Cu")',
               f'\t(descr "{self.descr}")',
               f'\t(tags "{self.tags}")',
               prop("Reference", "REF**", self.ref_at, "F.SilkS", False),
               prop("Value", self.name, self.val_at, "F.Fab", False),
               prop("Datasheet", self.datasheet, (0, 0), "F.Fab", True),
               prop("Description", self.descr, (0, 0), "F.Fab", True),
               f"\t(attr {self.attr})"]
        out += self.items
        out.append(
            f'\t(fp_text user "${{REFERENCE}}"\n\t\t(at {f(self.fab_ref_at[0])} {f(self.fab_ref_at[1])} 0)\n'
            f'\t\t(layer "F.Fab")\n\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 0.5 0.5)\n\t\t\t\t(thickness 0.08)\n\t\t\t)\n\t\t)\n\t)')
        out.append("\t(embedded_fonts no)")
        if self.model:
            out.append(f'\t(model "{MODEL_DIR}/{self.name}.step"\n\t\t(offset\n\t\t\t(xyz 0 0 0)\n\t\t)\n'
                       f'\t\t(scale\n\t\t\t(xyz 1 1 1)\n\t\t)\n\t\t(rotate\n\t\t\t(xyz 0 0 0)\n\t\t)\n\t)')
        out.append(")")
        return "\n".join(out) + "\n"

    def write(self):
        os.makedirs(OUT, exist_ok=True)
        path = os.path.join(OUT, self.name + ".kicad_mod")
        with open(path, "w", newline="\n") as fh:
            fh.write(self.render())
        print("wrote", os.path.relpath(path))


def _w(layer):
    return {"F.SilkS": W_SILK, "F.Fab": W_FAB, "F.CrtYd": W_CRT, "Edge.Cuts": 0.05}.get(layer, W_FAB)


# ---------------------------------------------------------------------------
# a. Alps SKRHABE010 5-way navigation switch
# ---------------------------------------------------------------------------
def skrh():
    fp = Footprint(
        "SW_Nav_Alps_SKRHABE010",
        "Alps Alpine SKRHABE010 4-direction + center-push switch, SMD, guide bosses, 7.35x7.5x1.8 mm body, "
        "stem top 5.0 mm (LCSC C139794). Land pattern from Alps SKRH catalog Drawing No.2 (viewed from mounting face). "
        "Pads: 1=A 2=Center 3=C 4=B 5=Common 6=D, MP=frame tabs (no net). Directions a/b/c/d point at the body CORNERS "
        "(a=top-left, b=top-right, c=bottom-left, d=bottom-right); rotate 45 deg on the board for axis-aligned directions. "
        "NPTH 0.8 (0.75+0.1/-0) at y=-1.9 and 1.1 (1.05+0.1/-0) at y=+1.9.",
        "Alps SKRH navigation joystick 5-way tactile switch",
        "smd",
        "https://tech.alpsalpine.com/cms.media/product_catalog_mu_02_skrh_en_34aeb8c2e3.pdf")
    # signal pads 1.35 x 1.0: x outer 8.5, inner 5.8; y outer 3.85, inner 1.85, middle pad 1.0
    px, pw, ph, py = (8.5 + 5.8) / 4, (8.5 - 5.8) / 2, 1.0, (3.85 + 1.85) / 4
    for num, x, y in ((1, -px, -py), (2, -px, 0), (3, -px, py), (4, px, -py), (5, px, 0), (6, px, py)):
        fp.smd(num, x, y, pw, ph)
    # frame tabs 2.0 x 1.8: y outer 9.9, inner 6.3
    my, mh = (9.9 + 6.3) / 4, (9.9 - 6.3) / 2
    fp.smd("MP", 0, -my, 2.0, mh, "rect")
    fp.smd("MP", 0, my, 2.0, mh, "rect")
    # guide-boss holes, 3.8 apart, small one at the top
    fp.npth(0, -1.9, 0.80)
    fp.npth(0, 1.9, 1.10)

    bx, by = 7.35 / 2, 7.5 / 2
    c = 0.6  # corner cut (Fab only, cosmetic)
    fp.poly_line("F.Fab", [(-bx + c, -by), (bx - c, -by), (bx, -by + c), (bx, by - c), (bx - c, by),
                           (-bx + c, by), (-bx, by - c), (-bx, -by + c)], closed=True)
    fp.circle("F.Fab", 0, 0, 1.5)        # stem ø3
    fp.circle("F.Fab", 0, 0, 2.6)        # collar
    # operating directions (to the corners) and their pin letters
    for lab, sx, sy in (("A", -1, -1), ("B", 1, -1), ("C", -1, 1), ("D", 1, 1)):
        fp.arrow("F.Fab", sx * 1.9, sy * 1.9, sx * 3.0, sy * 3.0, head=0.4)
        fp.text("F.Fab", lab, sx * 2.25, sy * 3.05, 0.6)
    # pin-1 mark on Fab
    fp.line("F.Fab", -bx + 0.3, -py, -bx + 0.9, -py)

    # silkscreen: body outline, kept 0.2 mm clear of copper
    sx, sy = bx + 0.11, by + 0.11
    clear_y = 3.85 / 2 + 0.2          # below/above the signal pads
    clear_x = 1.0 + 0.2               # beside the frame tabs
    for s in (-1, 1):
        fp.line("F.SilkS", -sx, s * clear_y, -sx, s * sy)
        fp.line("F.SilkS", sx, s * clear_y, sx, s * sy)
        fp.line("F.SilkS", -sx, s * sy, -clear_x, s * sy)
        fp.line("F.SilkS", clear_x, s * sy, sx, s * sy)
    # pin-1 marker: short bar left of pad 1
    fp.line("F.SilkS", -px - pw / 2 - 0.2, -py - 0.5, -px - pw / 2 - 0.2, -py + 0.5)

    xc = max(bx, px + pw / 2) + CRT_CLEAR
    yc = max(by, my + mh / 2) + CRT_CLEAR
    fp.courtyard_rect(-xc, -yc, xc, yc)
    fp.ref_at, fp.val_at = (0, -yc - 0.8), (0, yc + 0.8)
    fp.write()


# ---------------------------------------------------------------------------
# b. Hirose FH34SRJ-24S-0.5SH(50) FPC connector
# ---------------------------------------------------------------------------
def fh34():
    n, pitch, B = 24, 0.5, 11.5
    fp = Footprint(
        "Hirose_FH34SRJ-24S-0.5SH_1x24-1MP_P0.50mm_Horizontal",
        "Hirose FH34SRJ-24S-0.5SH(50), 24 pos, 0.5 mm pitch, 1.0 mm high, top+bottom contact, back-flip actuator, "
        "0.3 mm FPC (LCSC C324726). Recommended PCB mounting pattern from the Hirose FH34SRJ datasheet: signal pads "
        "0.3x0.8, B=11.5, fitting nails 0.4x0.8 with E=13.1 / F=13.9, 3.3 from signal-pad outer edge to nail outer edge. "
        "Pad 1 at +X (right, as in the Hirose top view). The FPC inserts from +Y (the side away from the signal pads); "
        "the signal pads are under the actuator side. Body outline on Fab is approximate (A=14.0, D=13.38, depth 3.8).",
        "Hirose FH34 FH34SRJ FPC FFC 0.5mm 24P horizontal",
        "smd",
        "https://www.hirose.com/en/product/series/FH34S")
    for i in range(n):
        fp.smd(i + 1, B / 2 - i * pitch, 0, 0.3, 0.8)
    E, F = 13.1, 13.9
    nx, nw = (E + F) / 4, (F - E) / 2
    ny = -0.4 + 3.3 - 0.4          # nail centre: pad outer edge (-0.4) + 3.3 - half nail
    fp.smd("MP", -nx, ny, nw, 0.8, "rect")
    fp.smd("MP", nx, ny, nw, 0.8, "rect")

    # Fab body (approximate): housing D=13.38 wide, overall 3.8 deep incl. tails/actuator,
    # FPC entry face 0.15 beyond the nail outer edge.
    front = ny + 0.4 + 0.15
    rear = front - 3.8
    D, A = 13.38, 14.0
    fp.poly_line("F.Fab", [(-D / 2, rear), (D / 2, rear), (D / 2, front), (-D / 2, front)], closed=True)
    # pin 1 marker on Fab
    fp.poly("F.Fab", [(B / 2, 0.6), (B / 2 - 0.3, 1.1), (B / 2 + 0.3, 1.1)], fill=True)
    # FPC insertion arrow (Fab): from outside +Y toward the entry face
    fp.arrow("F.Fab", 0, front + 2.2, 0, front + 0.2, head=0.6)
    fp.text("F.Fab", "FPC IN", 2.2, front + 1.3, 0.7)

    # silkscreen
    s = 0.2
    fp.line("F.SilkS", -nx + nw / 2 + s, front + 0.1, nx - nw / 2 - s, front + 0.1)
    for sgn in (-1, 1):
        fp.line("F.SilkS", sgn * (A / 2 + 0.1), rear, sgn * (A / 2 + 0.1), ny - 0.4 - s)
        fp.line("F.SilkS", sgn * (A / 2 + 0.1), rear, sgn * (B / 2 + 0.15 + s), rear)
    # pin 1 marker: dot-like bar outside the pad row at pad 1 end
    fp.line("F.SilkS", B / 2, -0.4 - 0.35, B / 2, -0.4 - 0.15 - 0.5)

    x_c = A / 2 + CRT_CLEAR
    fp.courtyard_rect(-x_c, min(rear, -0.4) - CRT_CLEAR, x_c, front + CRT_CLEAR)
    fp.ref_at, fp.val_at, fp.fab_ref_at = (0, rear - 1.0), (0, front + 3.4), (0, front - 1.8)
    fp.write()


# ---------------------------------------------------------------------------
# c. MYOUNG BS-24-B4AA016 CR2450 holder
# ---------------------------------------------------------------------------
def bs24():
    fp = Footprint(
        "BatteryHolder_MYOUNG_BS-24-B4AA016_CR2450",
        "MYOUNG BS-24-B4AA016 (BS-2450-2 Au) SMT CR2450 holder, open frame, gold contacts (LCSC C964809), "
        "drawing MY-CP-0138 rev A/0. Pads 3.62x4.50, outer span 37.00, inner span 29.75. Pad 1 = + (right), "
        "pad 2 = - (left); both contacts are end terminals, the holder has no PCB-side contact pad "
        "(the - spring presses the cell's bottom face from the holder floor). Body 27.2x16.0, 36.0 over the "
        "terminals, height 7.50 above the PCB. The 24.5 mm cell overhangs the body by 4.25 mm on each long side.",
        "CR2450 coin cell battery holder SMD MYOUNG BS-24",
        "smd",
        "https://www.lcsc.com/datasheet/C964809.pdf")
    pw, ph = (37.0 - 29.75) / 2, 4.5
    px = (37.0 + 29.75) / 4
    fp.smd("1", px, 0, pw, ph, "rect")
    fp.smd("2", -px, 0, pw, ph, "rect")

    bx, by = 27.2 / 2, 16.0 / 2
    fp.rect("F.Fab", -bx, -by, bx, by)
    fp.circle("F.Fab", 0, 0, 24.5 / 2)
    fp.rect("F.Fab", -18.0, -3.5, -bx, 3.5)      # terminal tabs (7.0 wide)
    fp.rect("F.Fab", bx, -3.5, 18.0, 3.5)
    fp.text("F.Fab", "+", 11.0, 0, 2.0)
    fp.text("F.Fab", "-", -11.0, 0, 2.0)

    # silk: body outline + the cell overhang arcs
    sx, sy = bx + 0.11, by + 0.11
    fp.rect("F.SilkS", -sx, -sy, sx, sy)
    r = 24.5 / 2 + 0.11
    a = math.degrees(math.asin(sy / r))
    fp.arc("F.SilkS", 0, 0, r, -180 + a, -a)     # top overhang
    fp.arc("F.SilkS", 0, 0, r, a, 180 - a)       # bottom overhang
    fp.text("F.SilkS", "+", px, -ph / 2 - 1.2, 1.5)
    fp.text("F.SilkS", "-", -px, -ph / 2 - 1.2, 1.5)

    xc = px + pw / 2 + CRT_CLEAR
    yc = 24.5 / 2 + CRT_CLEAR
    fp.courtyard_rect(-xc, -yc, xc, yc)
    fp.ref_at, fp.val_at = (0, -yc - 0.9), (0, yc + 0.9)
    fp.write()


# ---------------------------------------------------------------------------
# d. Vishay side-looking MiniLED parts (VSMB2948SL emitter, VEMT2523SLX01 detector)
# ---------------------------------------------------------------------------
# Package geometry (Vishay drawings 6.544-5410.02-4 / 6.544-5410.01-4), origin at the
# midpoint between the two pads, optical axis along -Y:
MINI = dict(
    pad_x=(4.2 - 0.9) / 2, pad_w=0.9, pad_h=1.2,     # IPC-7351 pad proposal
    back=1.18, front=-0.42, lens_tip=-1.37,          # body 1.6 deep + lens, 2.55 total
    body_w=2.2, lead_w=3.8, height=2.3, axis_z=1.15, lens_d=1.8)


def sidelooker(name, part, kind, p1, p2, ds, lcsc):
    m = MINI
    fp = Footprint(
        name,
        f"Vishay {part} side-looking {kind}, 2.3x2.55x2.3 mm MiniLED side-view package ({lcsc}). "
        f"Pads 0.9x1.2, 4.2 mm outer span (Vishay IPC-7351 pad proposal). Pad 1={p1}, pad 2={p2}. "
        f"Optical axis is parallel to the PCB along -Y, i.e. the TOP edge of the footprint faces the board edge: "
        f"lens tip at y=-1.37, lens base / body front face at y=-0.42, body back at y=+1.18, "
        f"optical axis 1.15 mm above the PCB. Place the origin 1.37 mm inside the board edge for a flush lens.",
        f"Vishay {part} side looking side view MiniLED infrared",
        "smd", ds)
    fp.smd("1", m["pad_x"], 0, m["pad_w"], m["pad_h"])
    fp.smd("2", -m["pad_x"], 0, m["pad_w"], m["pad_h"])

    bw = m["body_w"] / 2
    fp.rect("F.Fab", -bw, m["front"], bw, m["back"])
    # lens (half disc seen from above) as an arc + chord
    r = m["lens_d"] / 2
    lens_len = m["front"] - m["lens_tip"]
    fp.line("F.Fab", -r, m["front"], -r, m["front"] - (lens_len - r))
    fp.line("F.Fab", r, m["front"], r, m["front"] - (lens_len - r))
    fp.arc("F.Fab", 0, m["front"] - (lens_len - r), r, 180, 360)
    # leads
    for s in (-1, 1):
        fp.rect("F.Fab", s * bw, -0.4, s * m["lead_w"] / 2, 0.4)
    # optical direction arrow (out of the lens, off the board edge)
    label = "IR OUT" if kind.endswith("emitter") else "IR IN"
    if label == "IR OUT":
        fp.arrow("F.Fab", 0, 0.6, 0, m["lens_tip"] - 1.2, head=0.4)
    else:
        fp.arrow("F.Fab", 0, m["lens_tip"] - 1.2, 0, 0.6, head=0.4)
    fp.text("F.Fab", label, 1.6, m["lens_tip"] - 0.7, 0.5)
    # pin-1 marker on Fab
    fp.text("F.Fab", p1, m["pad_x"], 0.95, 0.5)

    # silk: only the back edge (the lens side is the board edge) + pin-1 bar
    pad_y = m["pad_h"] / 2
    fp.line("F.SilkS", -bw, m["back"] + 0.11, bw, m["back"] + 0.11)
    fp.line("F.SilkS", m["pad_x"] + m["pad_w"] / 2 + 0.25, -pad_y, m["pad_x"] + m["pad_w"] / 2 + 0.25, pad_y)

    xc = m["pad_x"] + m["pad_w"] / 2 + CRT_CLEAR
    fp.courtyard_rect(-xc, m["lens_tip"] - CRT_CLEAR, xc, max(m["back"], pad_y) + CRT_CLEAR)
    fp.ref_at, fp.val_at, fp.fab_ref_at = (0, 2.3), (0, 3.4), (0, 0.4)
    fp.write()


# ---------------------------------------------------------------------------
# e. Good Display GDEY0213B74 e-paper outline + FPC fold slot
# ---------------------------------------------------------------------------
def epd():
    # From the GD mechanical drawing (GDEY0213B74 datasheet rev 1.0, sec. 4):
    gw, gh = 59.2, 29.2
    aa_w, aa_h = 48.55, 23.7046
    aa_x0 = -gw / 2 + 7.95                       # 59.2 - 48.55 - (0.45+1.45+0.40+0.40)
    aa_y0 = -gh / 2 + (gh - aa_h) / 2            # 0.5+1.45+0.40+0.40 = 2.75 bottom; ~2.745 top
    fpc_y0 = -gh / 2 + 8.15                      # FPC 12.5 wide, 8.15 from the top glass edge
    fpc_w = 12.5
    # Slot from hardware/board.yaml, relative to the glass (glass x 6.0, y 2.4; slot x 2.6 w 1.4, y 10 h 14)
    gx, gy = 6.0 + gw / 2, 2.4 + gh / 2           # glass centre in board coords
    sx0, sw, sy0, sh = 2.6 - gx, 1.4, 10.0 - gy, 14.0
    scx, r = sx0 + sw / 2, sw / 2

    fp = Footprint(
        "EPD_GoodDisplay_GDEY0213B74_Outline",
        "Good Display GDEY0213B74 2.13in e-paper (SSD1680), mechanical outline only, front side. Origin = glass centre. "
        "Glass 59.2x29.2x1.0, active area 48.55x23.70 starting 7.95 mm from the FPC-side glass edge (centred in Y). "
        "FPC 12.5 wide, 14.3 long, 0.3 thick, leaves the left short edge 8.15 mm from the top glass edge. "
        "Edge.Cuts: 1.4x14.0 rounded FPC fold slot per hardware/board.yaml (slot centre x=-32.3 rel. glass centre). "
        "3D model: glass on 0.2 mm adhesive, FPC folded 180 deg through the slot to the back side.",
        "e-paper EPD Good Display GDEY0213B74 2.13 outline",
        "exclude_from_pos_files exclude_from_bom",
        "https://v4.cecdn.yun300.cn/100001_1909185148/GDEY0213B74.pdf")
    fp.rect("F.Fab", -gw / 2, -gh / 2, gw / 2, gh / 2)
    fp.rect("F.Fab", aa_x0, aa_y0, aa_x0 + aa_w, aa_y0 + aa_h)
    fp.text("F.Fab", "ACTIVE AREA 48.55 x 23.70", aa_x0 + aa_w / 2, aa_y0 + aa_h / 2 + 2.0, 1.0)
    # FPC tail (Fab): from the glass edge to the slot
    fp.rect("F.Fab", scx, fpc_y0, -gw / 2, fpc_y0 + fpc_w)
    fp.text("F.Fab", "FPC", (scx - gw / 2) / 2 + 0.3, fpc_y0 + fpc_w / 2 + 2.5, 0.5)
    # silkscreen corner marks just outside the glass
    L, o = 3.0, 0.15
    for cx, cy in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
        x, y = cx * (gw / 2 + o), cy * (gh / 2 + o)
        fp.line("F.SilkS", x, y, x - cx * L, y)
        fp.line("F.SilkS", x, y, x, y - cy * L)
    # FPC fold slot on Edge.Cuts (rounded ends)
    ya, yb = sy0 + r, sy0 + sh - r
    fp.line("Edge.Cuts", sx0, ya, sx0, yb)
    fp.line("Edge.Cuts", sx0 + sw, ya, sx0 + sw, yb)
    fp.arc("Edge.Cuts", scx, ya, r, 180, 360)
    fp.arc("Edge.Cuts", scx, yb, r, 0, 180)
    # courtyard: glass + slot region
    c = CRT_CLEAR
    xl = sx0 - c
    fp.poly("F.CrtYd", [(crt(-gw / 2 - c), crt(-gh / 2 - c)), (crt(gw / 2 + c), crt(-gh / 2 - c)),
                        (crt(gw / 2 + c), crt(gh / 2 + c)), (crt(-gw / 2 - c), crt(gh / 2 + c)),
                        (crt(-gw / 2 - c), crt(sy0 + sh + c)), (crt(xl), crt(sy0 + sh + c)),
                        (crt(xl), crt(sy0 - c)), (crt(-gw / 2 - c), crt(sy0 - c))])
    fp.ref_at, fp.val_at, fp.fab_ref_at = (0, -gh / 2 - 1.2), (0, aa_y0 + aa_h / 2), (0, -4)
    fp.write()


def main():
    skrh()
    fh34()
    bs24()
    sidelooker("LED_Vishay_VSMB2948SL_SideView", "VSMB2948SL", "940 nm IR emitter", "K", "A",
               "https://www.vishay.com/docs/83498/vsmb2948sl.pdf", "LCSC C6604342")
    sidelooker("PhotoTransistor_Vishay_VEMT2523SLX01_SideView", "VEMT2523SLX01", "NPN phototransistor detector",
               "C", "E", "https://www.vishay.com/docs/84167/vemt2523slx01.pdf", "LCSC C511451")
    epd()


if __name__ == "__main__":
    main()
