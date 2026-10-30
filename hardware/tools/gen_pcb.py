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


def add_cell_via_keepout(board, segments=48):
    """No vias under the coin cell: it lies on the board (the clip presses it
    onto the negative pad) and slides in over it, so a via whose tenting is
    imperfect could short its + can. Tracks under solder mask are fine."""
    bt = design.BOARD["battery"]
    r = bt["cell_diameter"] / 2 + 0.2
    z = pcbnew.ZONE(board)
    z.SetIsRuleArea(True)
    z.SetDoNotAllowTracks(False)
    z.SetDoNotAllowVias(True)
    z.SetDoNotAllowPads(False)
    z.SetDoNotAllowFootprints(False)
    z.SetDoNotAllowCopperPour(False)
    ls = pcbnew.LSET()
    ls.AddLayer(pcbnew.F_Cu)
    ls.AddLayer(pcbnew.B_Cu)
    z.SetLayerSet(ls)
    z.SetZoneName("no vias under the cell")
    poly = z.Outline()
    poly.NewOutline()
    for i in range(segments):
        a = 2 * math.pi * i / segments
        poly.Append(pt(bt["x"] + r * math.cos(a), bt["y"] + r * math.sin(a)))
    board.Add(z)


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
    add_cell_via_keepout(board)
    board.Save(str(PCB))
    print("placed", len(board.GetFootprints()), "footprints")


def step_dsn():
    board = pcbnew.LoadBoard(str(PCB))
    dsn = HW / "build" / "irl2fa.dsn"
    dsn.parent.mkdir(exist_ok=True)
    for z in list(board.Zones()):
        if not z.GetIsRuleArea():
            board.Remove(z)
    for t in list(board.GetTracks()):
        board.Remove(t)
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
    gnd = board.FindNet("GND")
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


SILK_REF = 0.8          # reference designator height, mm (JLCPCB's legible minimum)
SILK_REF_SMALL = 0.65   # fallback where 0.8 mm has no free place (DRC minimum is 0.6)
SILK_GAP = 0.15         # clearance from silk text to pads, other silk and bodies


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
    marks = [("IRL2FA", 1.2, w / 2, h - 1.6), ("v0.1  irl2fa.com", 0.7, w / 2, h - 0.6),
             ("CERN-OHL-S-2.0", 0.7, 11.0, h - 1.0)]
    for text, size, px, py in marks:
        t = existing.get(text)
        if t is None:
            t = pcbnew.PCB_TEXT(board)
            t.SetText(text)
            board.Add(t)
        t.SetLayer(pcbnew.B_SilkS)
        t.SetMirrored(True)
        t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
        t.SetTextThickness(mm(size * 0.15))
        grid = [(px + dx * 0.5, py + dy * 0.5, 0) for dx in range(-150, 151) for dy in range(-60, 61)]
        grid.sort(key=lambda c: (c[0] - px) ** 2 + (c[1] - py) ** 2)
        if not place_text(t, obs, grid):
            raise SystemExit(f"no free place on the back for the marking {text!r}")

    hidden = []
    fps = sorted(board.GetFootprints(), key=lambda f: -len(list(f.Pads())))
    for fp in fps:
        ref = fp.Reference()
        if fp.GetReference().startswith("H"):  # mounting holes: unlabelled
            ref.SetVisible(False)
            continue
        ref.SetVisible(True)  # re-runs start over, including labels hidden last time
        ref.SetKeepUpright(True)
        layer, (x0, y0, x1, y1) = body[fp.GetReference()]
        ref.SetLayer(layer)
        # a part's own body box was added as an obstacle; its label may touch it
        own = obs.boxes[layer].index((x0, y0, x1, y1)) if (x0, y0, x1, y1) in obs.boxes[layer] else None
        if own is not None:
            saved = obs.boxes[layer].pop(own)
        ok = False
        for size in (SILK_REF, SILK_REF_SMALL):  # the smaller size only where the full one can't fit
            ref.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
            ref.SetTextThickness(mm(0.12 if size < SILK_REF else 0.15))
            ref.SetTextAngleDegrees(0)
            tb = text_box(ref)
            tw, th = tb[2] - tb[0], tb[3] - tb[1]
            ok = place_text(ref, obs, ring((x0 + x1) / 2, (y0 + y1) / 2, x0, y0, x1, y1, tw, th))
            if ok:
                break
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
