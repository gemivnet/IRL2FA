"""Assembly at JLCPCB: the placement file puts JLCPCB's own footprints where
ours are, the right way round, and the board meets JLCPCB's fab and PCBA rules.

JLCPCB does not read our footprints. It places each part from its own library
footprint (the EasyEDA one for that LCSC number), turned by the CPL rotation,
and for back-side parts it reads the rotation as seen from the back. Every
check in KiCad passes with a CPL that puts SOT-23s, diodes, the RTC, the coin
cell clip and the IR parts the wrong way round, so these tests simulate the
placement machine instead: they take JLCPCB's pads (jlc_footprints.yaml),
place them with irl2fa-cpl-jlcpcb.csv the way JLCPCB does, and check every pad
lands on our pad with the same number. Finding IDs (A-nn) refer to
docs/assembly-review.md.

SPDX-License-Identifier: CERN-OHL-S-2.0
"""
import csv
import json
import math
import sys
from pathlib import Path

import pytest
import yaml

HW = Path(__file__).resolve().parents[1]
FAB = HW / "fab"
sys.path.insert(0, str(HW / "tools"))
import fab  # noqa: E402

BOARD = yaml.safe_load((HW / "board.yaml").read_text())["board"]
JLCFP = yaml.safe_load((Path(__file__).parent / "jlc_footprints.yaml").read_text())["parts"]
BOM = list(csv.DictReader((FAB / "irl2fa-bom-jlcpcb.csv").open()))
FULL_BOM = list(csv.DictReader((FAB / "irl2fa-bom.csv").open()))
CPL = {r["Designator"]: r for r in csv.DictReader((FAB / "irl2fa-cpl-jlcpcb.csv").open())}
LCSC_OF = {ref.strip(): row["LCSC Part #"] for row in BOM for ref in row["Designator"].split(",")}
PRICES = json.loads((HW / "bom_prices.json").read_text())["parts"]
BOARDS = 10  # first production run

# JLCPCB published limits (jlcpcb.com/capabilities, read 2026-10-30)
JLC_MASK_DAM = 0.10          # mm, green/red/yellow/blue/purple mask, 1 oz
JLC_SILK_LINE = 0.15         # mm
JLC_SILK_TEXT = 1.0          # mm
JLC_STANDARD_PCBA_MIN = 70.0  # mm, smallest single board or panel side for Standard PCBA


def cpl_row(ref):
    r = CPL[ref]
    return float(r["Mid X"][:-2]), float(r["Mid Y"][:-2]), float(r["Rotation"]), r["Layer"]


def rot(p, deg):
    a = math.radians(deg)
    return p[0] * math.cos(a) - p[1] * math.sin(a), p[0] * math.sin(a) + p[1] * math.cos(a)


def jlc_to_board(ref, p):
    """A point of JLCPCB's footprint (its frame, y up) -> board CPL frame (y up,
    seen from the front), the way JLCPCB places it: rotated counter-clockwise by
    the CPL angle as seen from the part's side, so on the back the result is
    mirrored left-right to be seen from the front."""
    x, y, r, layer = cpl_row(ref)
    qx, qy = rot(p, r)
    if layer == "Bottom":
        qx = -qx
    return x + qx, y + qy


# ---------------------------------------------------------------- no KiCad needed

def test_every_bom_part_has_a_jlc_placement_correction():
    lcsc = {row["LCSC Part #"] for row in BOM}
    assert lcsc <= set(fab.JLC), f"no entry in fab.JLC for {sorted(lcsc - set(fab.JLC))}"
    for code, (corr, origin, evidence) in fab.JLC.items():
        assert corr in (0, 90, 180, 270), code
        assert len(evidence) > 20, f"{code}: record where the correction came from"


@pytest.mark.parametrize("kicad,side,corr,want", [
    (0, "Top", 0, 0), (90, "Top", 180, 270), (45, "Top", 0, 45),
    (0, "Bottom", 0, 180), (90, "Bottom", 0, 90), (180, "Bottom", 0, 0),
    (0, "Bottom", 180, 0), (90, "Bottom", 180, 270), (0, "Bottom", 270, 90),
])
def test_rotation_convention(kicad, side, corr, want):
    """A-01: back-side angles are mirrored (180 - a) before the correction."""
    code = next(c for c, v in fab.JLC.items() if v[0] == corr and v[1] == (0.0, 0.0))
    assert fab.jlc_placement(code, 10.0, 10.0, kicad, side)[2] == want


def test_origin_offset_follows_the_footprint():
    """A-02: J1's JLCPCB origin is 1.25 mm from our anchor, towards its nails."""
    # back side, KiCad 90 deg: the local +y (y down) offset ends up at -x on the board
    x, y, r = fab.jlc_placement("C324726", 12.5, 15.7, 90, "Bottom")
    assert (round(x, 4), round(y, 4), r) == (11.25, 15.7, 90)
    x, y, r = fab.jlc_placement("C324726", 12.5, 15.7, 0, "Top")
    assert (round(x, 4), round(y, 4)) == (12.5, 14.45)


def test_bom_mpn_matches_lcsc_part():
    norm = lambda s: "".join(ch for ch in s.lower() if ch.isalnum() or ch == ".")
    for row in FULL_BOM:
        lcsc_mpn = PRICES[row["LCSC"]]["mpn"]
        assert norm(row["MPN"]) == norm(lcsc_mpn), f"{row['References']}: BOM {row['MPN']!r} vs LCSC {lcsc_mpn!r}"


def _stock_params():
    out = []
    for row in FULL_BOM:
        mark = []
        if row["LCSC"] == "C5127802":
            mark = [pytest.mark.xfail(reason="A-06: JLCPCB lists 400 RV-3032-C7 but -144 available; "
                                             "pre-order it before ordering assembly", strict=False)]
        out.append(pytest.param(row, id=row["References"].split(",")[0], marks=mark))
    return out


@pytest.mark.parametrize("row", _stock_params())
def test_jlc_has_stock_for_the_first_run(row):
    info = PRICES[row["LCSC"]]
    assert "jlc_stock" in info, "run hardware/tools/bom_doc.py --refresh"
    need = int(row["Quantity"]) * BOARDS
    have = min(info["jlc_stock"], info.get("jlc_available", info["jlc_stock"]))
    assert have >= need, f"{row['References']} {row['LCSC']}: JLCPCB has {have}, {BOARDS} boards need {need}"


def test_readme_order_steps_cover_the_jlc_options():
    """A-04/A-09: a first-time buyer must be told everything the JLCPCB form asks
    that has a wrong answer."""
    text = (HW.parent / "README.md").read_text()
    step = text[text.index("## Build one"):text.index("## Every change")]
    for needle in ("0.8 mm", "ENIG", "Standard", "both sides", "Panel by JLCPCB", "Tented",
                   "Confirm Parts Placement", "docs/assembly-review.md"):
        assert needle.lower() in step.lower(), f"README 'Build one' does not mention {needle!r}"
    if min(BOARD["width"], BOARD["height"]) < JLC_STANDARD_PCBA_MIN:
        assert "panel" in step.lower()


# ------------------------------------------------------------------ needs KiCad

try:
    import pcbnew
except ImportError:  # outside the hardware CI image
    pcbnew = None


@pytest.fixture(scope="module")
def board():
    if pcbnew is None:
        pytest.skip("pcbnew not available")
    return pcbnew.LoadBoard(str(HW / "irl2fa.kicad_pcb"))


def to_cpl(board, pt):
    o = board.GetDesignSettings().GetAuxOrigin()
    return pcbnew.ToMM(pt.x - o.x), pcbnew.ToMM(o.y - pt.y)


def test_cpl_is_kicad_placement_with_jlc_corrections(board):
    """Recomputes every CPL row from the footprints, independently of kicad-cli."""
    for ref, lcsc in LCSC_OF.items():
        fp = board.FindFootprintByReference(ref)
        side = "Bottom" if fp.IsFlipped() else "Top"
        a = fp.GetOrientationDegrees()
        corr, (ox, oy), _ = fab.JLC[lcsc]
        want_r = (a + corr) % 360 if side == "Top" else (180 - a + corr) % 360
        lx, ly = ox, (-oy if side == "Bottom" else oy)          # KiCad local, y down
        t = math.radians(a)
        dx, dy = lx * math.cos(t) + ly * math.sin(t), -lx * math.sin(t) + ly * math.cos(t)
        px, py = to_cpl(board, fp.GetPosition())
        x, y, r, layer = cpl_row(ref)
        assert layer == side, ref
        assert abs(r - want_r) < 0.05 or abs(abs(r - want_r) - 360) < 0.05, f"{ref}: CPL {r}, want {want_r}"
        assert math.hypot(x - (px + dx), y - (py - dy)) < 0.001, f"{ref}: CPL position"


def _placed_parts():
    out = []
    for code, part in JLCFP.items():
        for ref in str(part["refs"]).split():
            out.append(pytest.param(ref, code, id=ref))
    return out


@pytest.mark.parametrize("ref,code", _placed_parts())
def test_jlc_footprint_lands_on_ours(board, ref, code):
    """A-01/A-02/A-03: place JLCPCB's footprint with our CPL; every one of its
    pads must sit on our pad with the same number, and parts with symmetric pads
    must face the right way."""
    part = JLCFP[code]
    ref_part = JLCFP[part["assumed_from"]] if "assumed_from" in part else part
    assert LCSC_OF[ref] == code, f"{ref} is {LCSC_OF[ref]} in the BOM, not {code}"
    fp = board.FindFootprintByReference(ref)
    layer = pcbnew.B_Cu if fp.IsFlipped() else pcbnew.F_Cu
    ours = [(p.GetNumber(), *to_cpl(board, p.GetPosition())) for p in fp.Pads()
            if p.IsOnLayer(layer) and p.GetAttribute() != pcbnew.PAD_ATTRIB_NPTH]
    pad_map = {str(k): str(v) for k, v in (ref_part.get("pad_map") or {}).items()}
    for num, px, py in ref_part["pads"]:
        bx, by = jlc_to_board(ref, (px, py))
        n, ox, oy = min(ours, key=lambda o: math.hypot(o[1] - bx, o[2] - by))
        d = math.hypot(ox - bx, oy - by)
        want = pad_map.get(str(num), str(num))
        assert n == want and d < 0.35, (
            f"{ref}: JLCPCB's pad {num} lands at ({bx:.2f}, {by:.2f}), nearest is our pad {n} "
            f"{d:.2f} mm away (want pad {want}); fix fab.JLC[{code!r}]")
    if "toward_top_edge" in ref_part:
        x0, y0 = jlc_to_board(ref, (0, 0))
        x1, y1 = jlc_to_board(ref, ref_part["toward_top_edge"])
        assert y1 - y0 > 0.9, f"{ref} would face away from the top edge as JLCPCB places it"


def test_cpl_frame_matches_the_gerbers(board):
    """Our pads, in the CPL frame, are where the copper Gerbers flash them, so
    the CPL and the Gerbers JLCPCB receives share one origin and axis sense."""
    gerbonara = pytest.importorskip("gerbonara")
    from gerbonara.graphic_objects import Flash
    from gerbonara.utils import MM
    flashes = {}
    for side, name in (("F", "F_Cu"), ("B", "B_Cu")):
        g = gerbonara.GerberFile.open(FAB / "gerbers" / f"irl2fa-{name}.gbr")
        flashes[side] = [(MM.convert_from(o.unit, o.x), MM.convert_from(o.unit, o.y))
                         for o in g.objects if isinstance(o, Flash)]
    missing = []
    for ref in LCSC_OF:
        fp = board.FindFootprintByReference(ref)
        side, layer = ("B", pcbnew.B_Cu) if fp.IsFlipped() else ("F", pcbnew.F_Cu)
        for p in fp.Pads():
            if not p.IsOnLayer(layer) or p.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
                continue
            x, y = to_cpl(board, p.GetPosition())
            if not any(abs(fx - x) < 0.005 and abs(fy - y) < 0.005 for fx, fy in flashes[side]):
                missing.append(f"{ref}:{p.GetNumber()} at ({x:.3f}, {y:.3f})")
    assert not missing, "pads not found in the copper Gerbers: " + ", ".join(missing[:10])


def test_no_paste_on_the_cell_contact(board):
    """The 12 mm pad is a bare ENIG contact for the cell's negative face."""
    bt = board.FindFootprintByReference("BT1")
    pad = next(p for p in bt.Pads() if p.GetNumber() == "2")
    assert not pad.IsOnLayer(pcbnew.B_Paste) and not pad.IsOnLayer(pcbnew.F_Paste)
    assert pad.IsOnLayer(pcbnew.B_Mask), "the contact pad must be open in the solder mask"


def test_qfn_exposed_pad_paste_coverage(board):
    """Windowed paste on U1's exposed pad: 40-80 % (IPC-7093) so the QFN neither
    floats nor starves its thermal/ground joint."""
    u1 = board.FindFootprintByReference("U1")
    ep = next(p for p in u1.Pads() if p.GetNumber() == "33")
    ex, ey = pcbnew.ToMM(ep.GetSize().x), pcbnew.ToMM(ep.GetSize().y)
    c = ep.GetPosition()
    area = 0.0
    for p in u1.Pads():
        q = p.GetPosition()
        if p.IsOnLayer(pcbnew.B_Paste) and abs(pcbnew.ToMM(q.x - c.x)) < ex / 2 and abs(pcbnew.ToMM(q.y - c.y)) < ey / 2:
            m = p.GetSolderPasteMargin(pcbnew.B_Paste)       # per axis, usually 0
            area += pcbnew.ToMM(p.GetSize().x + 2 * m.x) * pcbnew.ToMM(p.GetSize().y + 2 * m.y)
    cover = area / (ex * ey)
    assert 0.4 <= cover <= 0.8, f"U1 exposed-pad paste covers {cover:.0%}"


@pytest.mark.parametrize("ref", ["U1", "U2", "J1"])
def test_fine_pitch_solder_mask_dams(board, ref):
    """JLCPCB keeps a solder-mask dam only if it is at least 0.1 mm wide; below
    that it opens the whole row and 0.5 mm-pitch pads can bridge."""
    fp = board.FindFootprintByReference(ref)
    layer = pcbnew.B_Mask if fp.IsFlipped() else pcbnew.F_Mask
    boxes = []
    for p in fp.Pads():
        if not p.IsOnLayer(layer):
            continue
        e = pcbnew.ToMM(p.GetSolderMaskExpansion(layer)) if hasattr(p, "GetSolderMaskExpansion") else 0
        bb = p.GetBoundingBox()
        boxes.append((p.GetNumber(), pcbnew.ToMM(bb.GetLeft()) - e, pcbnew.ToMM(bb.GetTop()) - e,
                      pcbnew.ToMM(bb.GetRight()) + e, pcbnew.ToMM(bb.GetBottom()) + e))
    worst = (99, None)
    for i, a in enumerate(boxes):
        for b in boxes[i + 1:]:
            gx = max(b[1] - a[3], a[1] - b[3], 0)
            gy = max(b[2] - a[4], a[2] - b[4], 0)
            g = math.hypot(gx, gy)
            if g < worst[0]:
                worst = (g, (a[0], b[0]))
    assert worst[0] >= JLC_MASK_DAM - 1e-6, f"{ref}: mask dam {worst[0]:.3f} mm between pads {worst[1]}"


def test_vias_are_tented():
    """The order form's 'Via covering: Tented' must match the Gerbers: no mask
    openings at vias (a bare via under the cell clip could short to the can)."""
    gerbonara = pytest.importorskip("gerbonara")
    from gerbonara.graphic_objects import Flash
    from gerbonara.utils import MM
    for name in ("F_Mask", "B_Mask"):
        g = gerbonara.GerberFile.open(FAB / "gerbers" / f"irl2fa-{name}.gbr")
        sizes = {round(MM.convert_from(o.unit, o.aperture.diameter), 3) for o in g.objects
                 if isinstance(o, Flash) and getattr(o.aperture, "diameter", None)}
        assert 0.6 not in sizes, f"{name} opens the 0.6 mm vias"


@pytest.mark.xfail(strict=True, reason="A-07: silkscreen text is 0.65-0.8 mm high with 0.105-0.15 mm "
                                      "strokes; JLCPCB's minimum is 1.0 mm / 0.15 mm")
def test_silkscreen_meets_jlc_minimums(board):
    small = []
    texts = [d for d in board.GetDrawings() if d.GetClass() == "PCB_TEXT"]
    for fp in board.GetFootprints():
        texts += [g for g in fp.GraphicalItems() if g.GetClass() == "PCB_TEXT"]
        texts += [f for f in fp.GetFields() if f.IsVisible()]
    for t in texts:
        if t.GetLayer() not in (pcbnew.F_SilkS, pcbnew.B_SilkS) or not t.IsVisible():
            continue
        h, w = pcbnew.ToMM(t.GetTextHeight()), pcbnew.ToMM(t.GetTextThickness())
        if h < JLC_SILK_TEXT - 1e-6 or w < JLC_SILK_LINE - 1e-6:
            small.append(f"{t.GetText()!r} {h:.2f}/{w:.3f}")
    assert not small, f"{len(small)} silkscreen texts below 1.0 mm / 0.15 mm: " + ", ".join(small[:8])


@pytest.mark.xfail(strict=True, reason="A-08: both copper pours have no net (net 0): they are "
                                      "floating copper, not the ground pour docs/pcb-design.md describes")
def test_copper_pours_are_ground(board):
    pours = [z for z in board.Zones() if not z.GetIsRuleArea() and z.GetParentFootprint() is None]
    assert pours, "no copper pours"
    for z in pours:
        assert z.GetNetname().lstrip("/") == "GND", f"pour on {z.GetLayerName()} has net {z.GetNetname()!r}"
