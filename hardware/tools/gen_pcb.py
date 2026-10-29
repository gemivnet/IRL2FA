#!/usr/bin/env python3
"""Bootstrap hardware/irl2fa.kicad_pcb from the schematic netlist.

Steps (each can be run alone with --step):
  place  - footprints from the netlist placed per tools/design.py, outline,
           mounting holes, silkscreen; writes the board unrouted
  dsn    - Specctra DSN export (Freerouting then runs in its own container)
  ses    - Specctra SES import
  finish - ground pours on both layers, zone fill, save

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


def add_text(board, text, x, y, layer, size=1.0, mirror=False):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(text)
    t.SetPosition(pt(x, y))
    t.SetLayer(layer)
    t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
    t.SetTextThickness(mm(size * 0.15))
    if mirror:
        t.SetMirrored(True)
    board.Add(t)


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
    add_text(board, "IRL2FA", 40.0, 30.6, pcbnew.B_SilkS, 1.6, mirror=True)
    add_text(board, "v0.1  irl2fa.com", 40.0, 32.6, pcbnew.B_SilkS, 0.8, mirror=True)
    add_text(board, "CERN-OHL-S-2.0", 12.0, 31.8, pcbnew.B_SilkS, 0.7, mirror=True)
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--step", choices=["place", "dsn", "ses", "finish"], required=True)
    ap.add_argument("--netlist", default=str(HW / "build" / "irl2fa.net"))
    a = ap.parse_args()
    {"place": lambda: step_place(a.netlist), "dsn": step_dsn, "ses": step_ses,
     "finish": step_finish}[a.step]()


if __name__ == "__main__":
    main()
