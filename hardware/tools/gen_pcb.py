#!/usr/bin/env python3
"""Bootstrap hardware/irl2fa.kicad_pcb from the schematic netlist.

Steps (each can be run alone with --step):
  place  - footprints from the netlist placed per tools/design.py, outline,
           mounting holes; writes the board unrouted
  dsn    - Specctra DSN export (Freerouting then runs in its own container)
  ses    - Specctra SES import
  finish - ground pours on both layers, zone fill, save
  silk   - reference designators and board markings placed clear of pads,
           parts, the coin cell and the edge (touches only silkscreen text)

Run inside the KiCad 9 container (pcbnew Python module):
    hardware/tools/build_pcb.sh

SPDX-License-Identifier: CERN-OHL-S-2.0
"""
import argparse
import math
import os
import sys
from pathlib import Path

import pcbnew

sys.path.insert(0, str(Path(__file__).resolve().parent))
import design  # noqa: E402
from sexpr import find, find_all, parse  # noqa: E402

HW = Path(__file__).resolve().parents[1]
PCB = HW / "irl2fa.kicad_pcb"
STOCK_FP = Path(os.environ.get("KICAD_FOOTPRINT_DIR", "/usr/share/kicad/footprints"))
ORIGIN = (100.0, 100.0)  # board top-left in KiCad coordinates
LAYERS = int(os.environ.get("IRL2FA_LAYERS", "2"))  # 4 = fallback stack-up
B = design.BOARD["board"]


def mm(v):
    return pcbnew.FromMM(v)


def pt(x, y):
    return pcbnew.VECTOR2I(mm(ORIGIN[0] + x), mm(ORIGIN[1] + y))


def read_netlist(path):
    tree = parse(Path(path).read_text())
    comps = {}
    for c in find_all(find(tree, "components"), "comp"):
        ref = find(c, "ref")[1]
        comps[ref] = {
            "footprint": find(c, "footprint")[1] if find(c, "footprint") else "",
            "value": find(c, "value")[1],
            "tstamp": find(c, "tstamps")[1] if find(c, "tstamps") else None,
        }
    nets = {}
    for n in find_all(find(tree, "nets"), "net"):
        name = find(n, "name")[1]
        for node in find_all(n, "node"):
            nets[(find(node, "ref")[1], find(node, "pin")[1])] = name
    return comps, nets


def load_fp(fpid):
    lib, name = fpid.split(":")
    libdir = HW / "lib" / "irl2fa.pretty" if lib == "irl2fa" else STOCK_FP / f"{lib}.pretty"
    fp = pcbnew.FootprintLoad(str(libdir), name)
    if fp is None:
        raise SystemExit(f"footprint {fpid} not found in {libdir}")
    fp.SetFPID(pcbnew.LIB_ID(lib, name))
    return fp


def add_outline(board):
    w, h, r = B["width"], B["height"], B["corner_radius"]
    lw = mm(0.1)

    def seg(x1, y1, x2, y2):
        s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(pt(x1, y1))
        s.SetEnd(pt(x2, y2))
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(lw)
        board.Add(s)

    def arc(cx, cy, a0):
        s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_ARC)
        s.SetCenter(pt(cx, cy))
        s.SetStart(pt(cx + r * math.cos(math.radians(a0)), cy + r * math.sin(math.radians(a0))))
        s.SetArcAngleAndEnd(pcbnew.EDA_ANGLE(90, pcbnew.DEGREES_T))
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(lw)
        board.Add(s)

    seg(r, 0, w - r, 0)
    seg(w, r, w, h - r)
    seg(w - r, h, r, h)
    seg(0, h - r, 0, r)
    arc(w - r, r, 270)
    arc(w - r, h - r, 0)
    arc(r, h - r, 90)
    arc(r, r, 180)


def add_edge_keepout(board, band=0.45):
    """Rule areas along the edges keep tracks and vias (not pads) 0.3 mm+
    from the outline, so the autorouter respects the fab's edge clearance."""
    w, h = B["width"], B["height"]
    for x0, y0, x1, y1 in ((0, 0, w, band), (0, h - band, w, h), (0, 0, band, h), (w - band, 0, w, h)):
        z = pcbnew.ZONE(board)
        z.SetIsRuleArea(True)
        z.SetDoNotAllowTracks(True)
        z.SetDoNotAllowVias(True)
        z.SetDoNotAllowPads(False)
        z.SetDoNotAllowFootprints(False)
        z.SetDoNotAllowCopperPour(False)
        ls = pcbnew.LSET.AllCuMask(LAYERS) if hasattr(pcbnew.LSET, "AllCuMask") else pcbnew.LSET()
        if not hasattr(pcbnew.LSET, "AllCuMask"):
            ls.AddLayer(pcbnew.F_Cu)
            ls.AddLayer(pcbnew.B_Cu)
        z.SetLayerSet(ls)
        z.SetZoneName("edge keepout")
        poly = z.Outline()
        poly.NewOutline()
        for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
            poly.Append(pt(x, y))
        board.Add(z)


def rule_area(board, name, points, layers, tracks=True, vias=True, pour=True):
    """A rule area (keep-out) over `points` (board mm) on `layers`; the flags
    say what is still allowed."""
    z = pcbnew.ZONE(board)
    z.SetIsRuleArea(True)
    z.SetDoNotAllowTracks(not tracks)
    z.SetDoNotAllowVias(not vias)
    z.SetDoNotAllowPads(False)
    z.SetDoNotAllowFootprints(False)
    z.SetDoNotAllowCopperPour(not pour)
    ls = pcbnew.LSET()
    for layer in layers:
        ls.AddLayer(layer)
    z.SetLayerSet(ls)
    z.SetZoneName(name)
    poly = z.Outline()
    poly.NewOutline()
    for x, y in points:
        poly.Append(pt(x, y))
    board.Add(z)
    return z


def circle_pts(cx, cy, r, segments=48):
    return [(cx + r * math.cos(2 * math.pi * i / segments), cy + r * math.sin(2 * math.pi * i / segments))
            for i in range(segments)]


# The cell's insertion path: it slides in from the bottom edge over this strip
# (docs/mechanical-review.md MECH-05), as wide as the cell plus 0.35 mm a side.
CELL_PATH_MARGIN = 0.35
# Under the cell's negative cap (Ø21, Murata CR2450R p.1) nothing on B.Cu may stand
# higher than the bare contact pad: copper under solder mask does (MECH-04). The
# only copper there is the pad's own connection, one spoke running straight down
# to the pour, with the solder mask opened over it so it is bare like the pad.
NEG_CAP_R = 10.5
PAD2_R = 6.0            # BT1 pad 2, the contact (gen_footprints.py MY2450 neg_pad_d / 2)
SPOKE_W = 1.2


def add_cell_keepouts(board):
    """Rule areas for the coin cell:
    - no vias under the cell or in its insertion path (MECH-05): it lies on the
      board and slides in over it, so a via whose tenting is imperfect could
      short its + can;
    - no tracks and no pour on B.Cu under the negative cap, except the contact
      pad's spoke (MECH-04)."""
    bt = design.BOARD["battery"]
    cx, cy, rc = bt["x"], bt["y"], bt["cell_diameter"] / 2
    layers = (pcbnew.F_Cu, pcbnew.B_Cu)
    rule_area(board, "no vias under the cell", circle_pts(cx, cy, rc + CELL_PATH_MARGIN), layers, vias=False)
    x0, x1 = cx - rc - CELL_PATH_MARGIN, cx + rc + CELL_PATH_MARGIN
    rule_area(board, "no vias in the cell's insertion path",
              [(x0, cy), (x1, cy), (x1, B["height"]), (x0, B["height"])], layers, vias=False)
    # a ring from the pad's edge to the cap's edge, open where the spoke runs out. It
    # stops at the pad: a pad inside a keep-out makes Freerouting give up on its net.
    r, ri, hw = NEG_CAP_R, PAD2_R, SPOKE_W / 2
    ao, ai = math.asin(hw / r), math.asin(hw / ri)
    n = 72
    pts = []
    for i in range(n + 1):           # outer edge, from the corridor's left wall (y down) the long way round
        a = math.pi / 2 + ao + (2 * math.pi - 2 * ao) * i / n
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    for i in range(n + 1):           # inner edge (the pad's), back again
        a = math.pi / 2 + 2 * math.pi - ai - (2 * math.pi - 2 * ai) * i / n
        pts.append((cx + ri * math.cos(a), cy + ri * math.sin(a)))
    rule_area(board, "nothing higher than the cell contact under the cell's negative cap",
              pts, (pcbnew.B_Cu,), tracks=False, vias=False, pour=False)
    # the spoke's solder-mask opening, from the pad's edge to the cap's edge
    m = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_RECT)
    m.SetStart(pt(cx - hw, cy + 5.9))
    m.SetEnd(pt(cx + hw, cy + r + 0.1))
    m.SetLayer(pcbnew.B_Mask)
    m.SetFilled(True)
    m.SetWidth(0)
    board.Add(m)


def flip(fp):
    try:
        fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
    except (AttributeError, TypeError):
        fp.Flip(fp.GetPosition(), False)


def orient_fpc_connector(fp):
    """The display's FPC folds through the slot left of the connector, so the
    connector must open towards -x and have pin 1 at the top (smaller y),
    matching the panel's pin 1 (top when seen from the front). Try the four
    orientations and keep the one that satisfies both."""
    for angle in (0, 90, 180, 270):
        fp.SetOrientationDegrees(angle)
        pads = {p.GetNumber(): p.GetPosition() for p in fp.Pads()}
        signal_x = pads["12"].x
        mp = [p.GetPosition() for p in fp.Pads() if p.GetNumber() == "MP"]
        opens_left = all(m.x < signal_x for m in mp)
        pin1_top = pads["1"].y < pads["24"].y
        if opens_left and pin1_top:
            print(f"{fp.GetReference()}: orientation {angle}")
            return
    raise SystemExit("no orientation of the FPC connector puts pin 1 at the top opening left")


def step_place(netlist):
    comps, nets = read_netlist(netlist)
    board = pcbnew.NewBoard(str(PCB))
    ds = board.GetDesignSettings()
    ds.SetBoardThickness(mm(B["thickness"]))
    board.SetCopperLayerCount(LAYERS)
    # Drill/place origin at the board's bottom-left corner: fab outputs are
    # exported relative to it, so all coordinates are positive.
    ds.SetAuxOrigin(pt(0, B["height"]))
    ds.SetGridOrigin(pt(0, B["height"]))

    netinfo = {}

    def net(name):
        if name not in netinfo:
            ni = pcbnew.NETINFO_ITEM(board, name)
            board.Add(ni)
            netinfo[name] = ni
        return netinfo[name]

    parts = {p["ref"]: p for p in design.PARTS}
    for ref, p in parts.items():
        if p["pcb"] is None:
            continue
        if os.environ.get("SKIP_LOCAL") and p["footprint"].startswith("irl2fa:"):
            continue
        side, x, y, rot = p["pcb"]
        if p["lib"] is None:
            fpid = p["footprint"]
        else:
            if ref not in comps:
                print("warning: not in netlist, skipped:", ref)
                continue
            fpid = comps[ref]["footprint"] or p["footprint"]
        fp = load_fp(fpid)
        fp.SetReference(ref)
        fp.SetValue(p["value"])
        if p["lib"] is None:
            fp.SetBoardOnly(True)
            fp.SetExcludedFromBOM(True)
            fp.SetExcludedFromPosFiles(True)
        else:
            fp.SetPath(pcbnew.KIID_PATH(f"/{comps[ref]['tstamp']}"))
            if not p["bom"]:
                fp.SetExcludedFromBOM(True)
                fp.SetExcludedFromPosFiles(True)
        board.Add(fp)
        fp.SetPosition(pt(x, y))
        if side == "B":
            flip(fp)
        fp.SetOrientationDegrees(rot if rot is not None else 0)
        if rot is None:
            orient_fpc_connector(fp)
        for pad in fp.Pads():
            key = (ref, pad.GetNumber())
            if key in nets:
                pad.SetNet(net(nets[key]))

    add_outline(board)
    add_edge_keepout(board)
    add_cell_keepouts(board)
    board.Save(str(PCB))
    print("placed", len(board.GetFootprints()), "footprints")


def step_dsn():
    board = pcbnew.LoadBoard(str(PCB))
    dsn = HW / "build" / "irl2fa.dsn"
    dsn.parent.mkdir(exist_ok=True)
    # collect first, then remove: Remove() while iterating leaves pcbnew 9's list wrappers dangling
    tracks = list(board.GetTracks())
    zones = [z for z in board.Zones() if not z.GetIsRuleArea()]
    for t in tracks:
        board.Remove(t)
    for z in zones:
        board.Remove(z)
    board.Save(str(PCB))
    pcbnew.ExportSpecctraDSN(board, str(dsn))
    print("wrote", dsn)


def step_ses():
    board = pcbnew.LoadBoard(str(PCB))
    pcbnew.ImportSpecctraSES(board, str(HW / "build" / "irl2fa.ses"))
    board.Save(str(PCB))
    print("imported routes:", len(board.GetTracks()))


def widen_necks(board, min_w=0.1):
    """Freerouting necks tracks down at some pads below the fab minimum;
    widen those short stubs back to the minimum (DRC checks the result)."""
    n = 0
    for t in board.GetTracks():
        if t.GetClass() == "PCB_TRACK" and t.GetWidth() < mm(min_w):
            t.SetWidth(mm(min_w))
            n += 1
    return n


def step_finish():
    board = pcbnew.LoadBoard(str(PCB))
    print("widened", widen_necks(board), "necked-down track segments")
    for z in list(board.Zones()):
        if not z.GetIsRuleArea():
            board.Remove(z)
    gnd = board.FindNet("/GND") or board.FindNet("GND")  # schematic nets carry the sheet path
    if gnd is None:
        raise SystemExit("no GND net on the board: the pours would float")
    layers = [pcbnew.F_Cu, pcbnew.B_Cu] + ([pcbnew.In1_Cu, pcbnew.In2_Cu] if board.GetCopperLayerCount() == 4 else [])
    for layer in layers:
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(gnd)
        z.SetLocalClearance(mm(0.25))
        z.SetMinThickness(mm(0.2))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetThermalReliefGap(mm(0.25))
        z.SetThermalReliefSpokeWidth(mm(0.3))
        poly = z.Outline()
        poly.NewOutline()
        for x, y in ((0, 0), (B["width"], 0), (B["width"], B["height"]), (0, B["height"])):
            poly.Append(pt(x, y))
        board.Add(z)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    board.Save(str(PCB))
    print("zones filled")


SILK_REF = 1.0          # text height, mm: JLCPCB's minimum (docs/assembly-review.md A-07)
SILK_STROKE = 0.153     # stroke, mm: JLCPCB's 0.15 minimum plus rounding margin
SILK_GAP = 0.15         # clearance from silk text to pads, other silk and bodies
# Test-pad labels for the programming fixture (TP1-TP5 on the back, one row).
# Short names, so neighbouring labels fit the 2.54 mm pitch at 1.0 mm text.
TP_LABELS = {"TP2": "GND", "TP1": "VBAT", "TP3": "DIO", "TP4": "CLK", "TP5": "RST"}


class Obstacles:
    """Axis-aligned keep-out boxes (mm, board coordinates) per side, plus the
    coin cell and its insertion path, which nothing printed should sit under."""

    def __init__(self, board):
        self.boxes = {pcbnew.F_SilkS: [], pcbnew.B_SilkS: []}
        bt, slot = design.BOARD["battery"], design.BOARD["fpc_slot"]
        self.cell = (bt["x"], bt["y"], bt["cell_diameter"] / 2 + 0.3)
        r = self.cell[2]
        # the cell slides in under the clip from the bottom edge
        self.add_both((bt["x"] - r, bt["y"], bt["x"] + r, B["height"]), back_only=True)
        self.add_both((slot["x"] - 0.3, slot["y"] - 0.3, slot["x"] + slot["w"] + 0.3, slot["y"] + slot["h"] + 0.3))
        for fp in board.GetFootprints():
            side = pcbnew.B_SilkS if fp.IsFlipped() else pcbnew.F_SilkS
            for pad in fp.Pads():
                box = self.mm_box(pad.GetBoundingBox())
                if pad.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH):
                    self.add_both(box)
                else:
                    self.boxes[side].append(box)
            for item in fp.GraphicalItems():
                if item.GetLayer() in self.boxes and item.GetClass() != "PCB_TEXT":
                    self.boxes[item.GetLayer()].append(self.mm_box(item.GetBoundingBox()))

    @staticmethod
    def mm_box(b):
        return (pcbnew.ToMM(b.GetLeft()) - ORIGIN[0], pcbnew.ToMM(b.GetTop()) - ORIGIN[1],
                pcbnew.ToMM(b.GetRight()) - ORIGIN[0], pcbnew.ToMM(b.GetBottom()) - ORIGIN[1])

    def add_both(self, box, back_only=False):
        for layer in self.boxes:
            if not back_only or layer == pcbnew.B_SilkS:
                self.boxes[layer].append(box)

    def free(self, box, layer, ignore=()):
        x0, y0, x1, y1 = box
        m = 0.3  # board edge margin
        if x0 < m or y0 < m or x1 > B["width"] - m or y1 > B["height"] - m:
            return False
        if layer == pcbnew.B_SilkS:  # nothing readable under the cell
            cx, cy, r = self.cell
            nx, ny = min(max(cx, x0), x1), min(max(cy, y0), y1)
            if (nx - cx) ** 2 + (ny - cy) ** 2 < r * r:
                return False
        g = SILK_GAP
        for i, (a0, b0, a1, b1) in enumerate(self.boxes[layer]):
            if i in ignore:
                continue
            if x0 < a1 + g and x1 > a0 - g and y0 < b1 + g and y1 > b0 - g:
                return False
        return True


def text_box(t):
    return Obstacles.mm_box(t.GetBoundingBox())


def place_text(t, obs, candidates):
    """Move text t to the first candidate (x, y, angle) where it is clear of
    everything; returns False when none is."""
    layer = t.GetLayer()
    for x, y, angle in candidates:
        t.SetTextAngleDegrees(angle)
        t.SetPosition(pt(x, y))
        box = text_box(t)
        if obs.free(box, layer):
            obs.boxes[layer].append(box)
            return True
    return False


def ring(cx, cy, x0, y0, x1, y1, w, h):
    """Candidate centres around a part's pad/body box, nearest first."""
    out = []
    for step in (0.0, 0.5, 1.0, 1.6, 2.4):
        d = SILK_GAP + 0.1 + step
        for dx in (0.0, -w / 3, w / 3):
            out.append((cx + dx, y0 - d - h / 2, 0, step))
            out.append((cx + dx, y1 + d + h / 2, 0, step))
        for dy in (0.0, -h, h):
            out.append((x0 - d - w / 2, cy + dy, 0, step))
            out.append((x1 + d + w / 2, cy + dy, 0, step))
        out.append((x0 - d - h / 2, cy, 90, step))
        out.append((x1 + d + h / 2, cy, 90, step))
        for sx in (-1, 1):
            for sy in (-1, 1):
                out.append((cx + sx * ((x1 - x0) / 2 + d + w / 2), cy + sy * ((y1 - y0) / 2 + d + h / 2), 0, step))
    return [(x, y, a) for x, y, a, _ in sorted(out, key=lambda c: c[3])]


def step_refresh():
    """Replace the board's copies of our own (irl2fa:) footprints with the
    current library versions, keeping position, side, rotation, fields and pad
    nets. For silkscreen/fab changes to a footprint without re-routing: the
    pads must be unchanged, which DRC (0 unconnected) confirms afterwards."""
    board = pcbnew.LoadBoard(str(PCB))
    old_fps = [fp for fp in board.GetFootprints() if fp.GetFPID().GetLibNickname() == "irl2fa"]
    for old in old_fps:
        new = load_fp(f"irl2fa:{old.GetFPID().GetLibItemName()}")
        new.SetFPID(old.GetFPID())
        new.SetReference(old.GetReference())
        new.SetValue(old.GetValue())
        new.SetPath(old.GetPath())
        new.SetAttributes(old.GetAttributes())
        new.SetBoardOnly(old.IsBoardOnly())
        new.SetExcludedFromBOM(old.IsExcludedFromBOM())
        new.SetExcludedFromPosFiles(old.IsExcludedFromPosFiles())
        for f in old.GetFields():
            if not f.IsMandatory() and not new.HasField(f.GetName()):
                new.SetField(f.GetName(), f.GetText())
        board.Add(new)
        new.SetPosition(old.GetPosition())
        if old.IsFlipped():
            flip(new)
        new.SetOrientation(old.GetOrientation())
        nets = {p.GetNumber(): p.GetNetCode() for p in old.Pads()}
        for pad in new.Pads():
            pad.SetNetCode(nets.get(pad.GetNumber(), 0))
        old_pads = sorted((p.GetNumber(), p.GetPosition().x, p.GetPosition().y) for p in old.Pads())
        new_pads = sorted((p.GetNumber(), p.GetPosition().x, p.GetPosition().y) for p in new.Pads())
        if old_pads != new_pads:
            raise SystemExit(f"{old.GetReference()}: pads moved in the library; re-place and re-route instead")
        board.Remove(old)
    board.Save(str(PCB))
    print("refreshed", len(old_fps), "footprints from the library")


def step_silk():
    """Place every reference designator and the board markings where they are
    legible: clear of pads, other silk, part bodies, the coin cell and its
    insertion path, the FPC slot and the board edge. A reference with no such
    place is hidden (the interactive BOM still identifies the part). Runs on
    the routed board and touches only silkscreen text."""
    board = pcbnew.LoadBoard(str(PCB))
    # existing markings are reused, not removed: Remove() from Python leaves
    # pcbnew 9's footprint list wrappers dangling
    existing = {d.GetText(): d for d in board.GetDrawings()
                if d.GetClass() == "PCB_TEXT" and d.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS)}
    obs = Obstacles(board)
    # other parts' bodies: courtyards, so a label never ends up under a part
    body = {}
    for fp in board.GetFootprints():
        layer = pcbnew.B_SilkS if fp.IsFlipped() else pcbnew.F_SilkS
        cy_layer = pcbnew.B_CrtYd if fp.IsFlipped() else pcbnew.F_CrtYd
        crt = fp.GetCourtyard(cy_layer)
        box = Obstacles.mm_box(crt.BBox()) if crt.OutlineCount() else Obstacles.mm_box(fp.GetBoundingBox(False, False))
        body[fp.GetReference()] = (layer, box)
        if fp.GetReference() != "BT1":  # the cell check covers the clip
            obs.boxes[layer].append(box)

    w, h = B["width"], B["height"]
    def marking(text, size):
        t = existing.get(text)
        if t is None:
            t = pcbnew.PCB_TEXT(board)
            t.SetText(text)
            board.Add(t)
        t.SetLayer(pcbnew.B_SilkS)
        t.SetMirrored(True)
        t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
        t.SetTextThickness(mm(max(SILK_STROKE, size * 0.15)))
        return t

    # the test-pad labels first: each must sit next to its own pad
    for k, (ref, text) in enumerate(TP_LABELS.items()):
        fp = board.FindFootprintByReference(ref)
        if fp is None:
            continue
        t = marking(text, SILK_REF)
        px, py = (pcbnew.ToMM(v) - o for v, o in zip((fp.GetPosition().x, fp.GetPosition().y), ORIGIN))
        t.SetTextAngleDegrees(0)
        tb = text_box(t)
        tw, th = tb[2] - tb[0], tb[3] - tb[1]
        crt_r = 1.32                 # TestPoint_Pad_D1.5mm courtyard radius (1.25) and its line
        cands = []
        for line in (k % 2, 1 - k % 2):   # alternate lines above the row
            y = py - crt_r - SILK_GAP - 0.02 - th / 2 - line * (th + SILK_GAP + 0.02)
            for dx in (0, -0.4, 0.4, -0.8, 0.8, -1.2, 1.2):
                cands.append((px + dx, y, 0))
        for side in (1, -1):         # or beside the pad
            cands.append((px + side * (1.25 + SILK_GAP + 0.05 + tw / 2), py, 0))
        if not place_text(t, obs, cands):
            raise SystemExit(f"no free place for the {ref} label {text!r}")

    marks = [("IRL2FA", 1.2, w / 2, h - 1.6), ("v0.1  irl2fa.com", SILK_REF, w / 2, h - 0.6),
             ("CERN-OHL-S-2.0", SILK_REF, 11.0, h - 1.0)]
    for text, size, px, py in marks:
        t = marking(text, size)
        grid = [(px + dx * 0.5, py + dy * 0.5, 0) for dx in range(-150, 151) for dy in range(-60, 61)]
        grid.sort(key=lambda c: (c[0] - px) ** 2 + (c[1] - py) ** 2)
        if not place_text(t, obs, grid):
            raise SystemExit(f"no free place on the back for the marking {text!r}")

    hidden = []
    fps = sorted(board.GetFootprints(), key=lambda f: -len(list(f.Pads())))
    for fp in fps:
        ref = fp.Reference()
        if fp.GetReference().startswith("H") or fp.GetReference() in TP_LABELS:
            ref.SetVisible(False)   # mounting holes unlabelled; test pads carry their signal name
            continue
        ref.SetVisible(True)  # re-runs start over, including labels hidden last time
        ref.SetKeepUpright(True)
        layer, (x0, y0, x1, y1) = body[fp.GetReference()]
        ref.SetLayer(layer)
        # a part's own body box was added as an obstacle; its label may touch it
        own = obs.boxes[layer].index((x0, y0, x1, y1)) if (x0, y0, x1, y1) in obs.boxes[layer] else None
        if own is not None:
            saved = obs.boxes[layer].pop(own)
        ref.SetTextSize(pcbnew.VECTOR2I(mm(SILK_REF), mm(SILK_REF)))
        ref.SetTextThickness(mm(SILK_STROKE))
        ref.SetTextAngleDegrees(0)
        tb = text_box(ref)
        tw, th = tb[2] - tb[0], tb[3] - tb[1]
        ok = place_text(ref, obs, ring((x0 + x1) / 2, (y0 + y1) / 2, x0, y0, x1, y1, tw, th))
        if own is not None:
            obs.boxes[layer].append(saved)
        if not ok:
            ref.SetVisible(False)
            hidden.append(fp.GetReference())
    board.Save(str(PCB))
    print("silkscreen placed; hidden references:", ", ".join(sorted(hidden)) or "none")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--step", choices=["place", "dsn", "ses", "finish", "refresh", "silk"], required=True)
    ap.add_argument("--netlist", default=str(HW / "build" / "irl2fa.net"))
    a = ap.parse_args()
    {"place": lambda: step_place(a.netlist), "dsn": step_dsn, "ses": step_ses,
     "finish": step_finish, "refresh": step_refresh, "silk": step_silk}[a.step]()


if __name__ == "__main__":
    main()
