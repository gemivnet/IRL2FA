"""Footprints against the manufacturers' land patterns (hardware/tests/footprints.yaml).

Every footprint placed on the board is checked twice: as embedded in
hardware/irl2fa.kicad_pcb (what gets fabricated) and as the library copy
(hardware/lib/irl2fa.pretty, or KiCad's stock library when installed). Then
each placed part's pads are checked against its schematic symbol's pins.
SPDX-License-Identifier: CERN-OHL-S-2.0"""
import math
import os
import re
from pathlib import Path

import pytest
import yaml
from conftest import needs_kicad
from sexpr import find, find_all, parse

HW = Path(__file__).resolve().parents[1]
TABLE = yaml.safe_load((Path(__file__).with_name("footprints.yaml")).read_text())
FPS = TABLE["footprints"]
PINS = yaml.safe_load((HW / "pins.yaml").read_text())
STOCK = Path(os.environ.get("KICAD_FOOTPRINT_DIR", "/usr/share/kicad/footprints"))
EPS = 1e-6


# --------------------------------------------------------------------------- parsing

def _num(x):
    return float(x)


def _flip_layer(layer):
    return "F." + layer[2:] if layer.startswith("B.") else layer


def footprint_geometry(node, back=False, fp_angle=0.0):
    """Pads, holes and graphics of a footprint s-expression in its own TOP-view
    frame. `back`: the footprint sits on B.Cu, so y is mirrored back."""
    sy = -1.0 if back else 1.0
    pads, holes = [], []
    for p in find_all(node, "pad"):
        num, kind, shape = str(p[1]), p[2], p[3]
        at = find(p, "at")
        x, y = _num(at[1]), sy * _num(at[2])
        ang = _num(at[3]) if len(at) > 3 else 0.0
        w, h = (_num(v) for v in find(p, "size")[1:3])
        rel = (ang - fp_angle) % 180.0
        if abs(rel - 90.0) < 1.0:
            w, h = h, w
        elif min(rel, 180.0 - rel) > 1.0:
            raise AssertionError(f"pad {num} at {ang} deg is not axis-aligned in its footprint")
        dr = find(p, "drill")
        drill = _num(dr[1]) if dr and len(dr) > 1 and not isinstance(dr[1], list) else None
        layers = [str(v) for v in (find(p, "layers") or [])[1:]]
        item = dict(num=num, kind=kind, shape=shape, at=(x, y), size=(w, h), drill=drill,
                    layers=layers)
        if kind == "np_thru_hole":
            holes.append(item)
        elif any(lay.endswith(".Cu") for lay in layers):
            pads.append(item)
    segs, gfx = {}, {}
    for g in node:
        if not (isinstance(g, list) and g and g[0] in ("fp_line", "fp_rect", "fp_poly", "fp_arc",
                                                        "fp_circle")):
            continue
        layer = _flip_layer(str(find(g, "layer")[1]))
        pts = []
        if g[0] in ("fp_line", "fp_rect", "fp_arc"):
            for key in ("start", "mid", "end"):
                v = find(g, key)
                if v:
                    pts.append((_num(v[1]), sy * _num(v[2])))
            if g[0] == "fp_rect":
                (x1, y1), (x2, y2) = pts
                pts = [(x1, y1), (x2, y1), (x2, y2), (x1, y2), (x1, y1)]
        elif g[0] == "fp_poly":
            pts = [(_num(v[1]), sy * _num(v[2])) for v in find_all(find(g, "pts"), "xy")]
            pts.append(pts[0])
        else:  # circle: centre + a point on it
            c, e = find(g, "center"), find(g, "end")
            cx, cy, r = _num(c[1]), sy * _num(c[2]), math.dist((_num(c[1]), _num(c[2])),
                                                                (_num(e[1]), _num(e[2])))
            pts = [(cx - r, cy - r), (cx + r, cy + r)]
        gfx.setdefault(layer, []).extend(pts)
        if g[0] in ("fp_line", "fp_rect", "fp_poly"):
            segs.setdefault(layer, []).extend(zip(pts, pts[1:]) if g[0] != "fp_line" else [tuple(pts)])
    return dict(pads=pads, holes=holes, gfx=gfx, segs=segs)


def _pcb():
    return parse((HW / "irl2fa.kicad_pcb").read_text())


PCB = _pcb()


def board_footprints():
    out = []
    for fp in find_all(PCB, "footprint"):
        ref = next(str(p[2]) for p in find_all(fp, "property") if p[1] == "Reference")
        at = find(fp, "at")
        out.append(dict(ref=ref, lib_id=str(fp[1]), node=fp, back=find(fp, "layer")[1] == "B.Cu",
                        angle=_num(at[3]) if len(at) > 3 else 0.0))
    return out


BOARD = board_footprints()
USED = sorted({b["lib_id"] for b in BOARD})


def library_file(lib_id):
    lib, name = lib_id.split(":")
    d = HW / "lib" / "irl2fa.pretty" if lib == "irl2fa" else STOCK / f"{lib}.pretty"
    return d / f"{name}.kicad_mod"


# --------------------------------------------------------------------------- table

def tol(entry, key, pad=None):
    t = dict(TABLE["defaults"]["tol"])
    t.update(entry.get("tol") or {})
    if pad:
        t.update(pad.get("tol") or {})
    if key == "heel_out" and key not in t:
        return t["edge_out"]
    return t[key]


def expected_pads(entry):
    pads = []
    for r in entry.get("rows") or []:
        for i in range(r["count"]):
            pads.append(dict(num=str(r["first"] + i), size=r["size"],
                             at=[r["at"][0] + i * r["step"][0], r["at"][1] + i * r["step"][1]]))
    for p in entry.get("pads") or []:
        pads.append(dict(p, num=str(p["num"])))
    return pads


def check_footprint(entry, geo):
    """Returns a list of discrepancies (empty = matches the datasheet)."""
    errors = []
    want = expected_pads(entry)
    got = list(geo["pads"])

    # pads: same numbers; match duplicates (MP) by nearest centre
    want_nums = sorted(p["num"] for p in want)
    got_nums = sorted(p["num"] for p in got)
    if want_nums != got_nums:
        errors.append(f"pad numbers differ: footprint {got_nums}, datasheet {want_nums}")
    pool = list(got)
    for w in want:
        cands = [g for g in pool if g["num"] == w["num"]]
        if not cands:
            continue
        g = min(cands, key=lambda c: math.dist(c["at"], w["at"]))
        pool.remove(g)
        out_t, in_t = tol(entry, "edge_out", w), tol(entry, "edge_in", w)
        for axis, name in ((0, "x"), (1, "y")):
            lo_w, hi_w = w["at"][axis] - w["size"][axis] / 2, w["at"][axis] + w["size"][axis] / 2
            lo_g, hi_g = g["at"][axis] - g["size"][axis] / 2, g["at"][axis] + g["size"][axis] / 2
            # positive = footprint edge outside the datasheet pad. The heel is
            # the edge facing the footprint centre (under the package body).
            c = w["at"][axis]
            for side, dev, heel in ((f"-{name}", lo_w - lo_g, c > 1e-3), (f"+{name}", hi_g - hi_w, c < -1e-3)):
                allow = tol(entry, "heel_out", w) if heel else out_t
                if dev > allow + EPS or -dev > in_t + EPS:
                    errors.append(
                        f"pad {w['num']} at {tuple(round(v, 4) for v in g['at'])} size "
                        f"{tuple(round(v, 4) for v in g['size'])}: {side} edge {dev:+.3f} from the "
                        f"datasheet pad (at {tuple(w['at'])} size {tuple(w['size'])}); allowed "
                        f"+{allow}/-{in_t}")
        if w.get("shape") and g["shape"] != w["shape"]:
            errors.append(f"pad {w['num']}: shape {g['shape']}, datasheet {w['shape']}")
        if w.get("drill"):
            lo, hi = w["drill"]
            if g["drill"] is None or not lo - EPS <= g["drill"] <= hi + EPS:
                errors.append(f"pad {w['num']}: drill {g['drill']}, datasheet {lo}..{hi}")
        elif g["drill"] is not None:
            errors.append(f"pad {w['num']}: drilled ({g['drill']}), datasheet pad is SMD")
        if w.get("exposed") and g["kind"] != "smd":
            errors.append(f"exposed pad {w['num']} is {g['kind']}, not smd")
        if entry.get("no_paste") and any("Paste" in lay for lay in g["layers"]):
            errors.append(f"pad {w['num']} has solder paste; the datasheet forbids it")

    # non-plated holes
    want_h, pool = entry.get("holes") or [], list(geo["holes"])
    if len(want_h) != len(pool):
        errors.append(f"{len(pool)} NPTH holes, datasheet {len(want_h)}")
    for w in want_h:
        if not pool:
            break
        g = min(pool, key=lambda c: math.dist(c["at"], w["at"]))
        pool.remove(g)
        if math.dist(g["at"], w["at"]) > tol(entry, "hole_pos") + EPS:
            errors.append(f"hole at {g['at']}, datasheet {w['at']}")
        lo, hi = w["drill"]
        if g["drill"] is None or not lo - EPS <= g["drill"] <= hi + EPS:
            errors.append(f"hole at {w['at']}: drill {g['drill']}, datasheet {lo}..{hi}")

    # pin 1 location and pitch
    p1 = [g for g in got if g["num"] == "1"]
    if "pin1" in entry:
        if not p1:
            errors.append("no pad 1")
        else:
            sx, sy = entry["pin1"]
            x, y = p1[0]["at"]
            sgn = lambda v: 0 if abs(v) < 1e-3 else (1 if v > 0 else -1)
            if (sgn(x), sgn(y)) != (sx, sy):
                errors.append(f"pad 1 at {(x, y)}: datasheet puts pin 1 at sign {(sx, sy)}")
    if "pitch" in entry:
        a, b = (next((g for g in got if g["num"] == n), None) for n in entry["pitch"]["pads"])
        if a and b:
            d = math.dist(a["at"], b["at"])
            if abs(d - entry["pitch"]["value"]) > tol(entry, "pitch") + EPS:
                errors.append(f"pitch {entry['pitch']['pads']} = {d:.4f}, datasheet "
                              f"{entry['pitch']['value']}")

    # body outline on F.Fab: an edge on each of the four sides
    body = entry.get("body")
    extents = []
    if body:
        (w, h), (cx, cy) = body["size"], body.get("at", [0, 0])
        t = tol(entry, "body")
        bx0, by0, bx1, by1 = cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2
        extents.append((bx0, by0, bx1, by1))
        segs = geo["segs"].get("F.Fab", [])
        vert = [s for s in segs if abs(s[0][0] - s[1][0]) < 1e-3 and abs(s[0][1] - s[1][1]) > 0.1]
        horz = [s for s in segs if abs(s[0][1] - s[1][1]) < 1e-3 and abs(s[0][0] - s[1][0]) > 0.1]
        for name, ok in (
                ("left", any(abs(s[0][0] - bx0) <= t + EPS for s in vert)),
                ("right", any(abs(s[0][0] - bx1) <= t + EPS for s in vert)),
                ("top", any(abs(s[0][1] - by0) <= t + EPS for s in horz)),
                ("bottom", any(abs(s[0][1] - by1) <= t + EPS for s in horz))):
            if not ok:
                errors.append(f"F.Fab has no {name} body edge within {t} of the datasheet body "
                              f"{w} x {h} at {(cx, cy)}")

    # courtyard: encloses pads, holes, body, envelope; not much bigger
    for g in got:
        (x, y), (w, h) = g["at"], g["size"]
        extents.append((x - w / 2, y - h / 2, x + w / 2, y + h / 2))
    for g in geo["holes"]:
        r = g["drill"] / 2
        extents.append((g["at"][0] - r, g["at"][1] - r, g["at"][0] + r, g["at"][1] + r))
    if entry.get("envelope"):
        extents.append(tuple(entry["envelope"]))
    if entry.get("edge_cuts"):
        ec = geo["gfx"].get("Edge.Cuts", [])
        if not ec:
            errors.append("no Edge.Cuts graphics")
        else:
            box = (min(p[0] for p in ec), min(p[1] for p in ec), max(p[0] for p in ec), max(p[1] for p in ec))
            if any(abs(a - b) > tol(entry, "body") + EPS for a, b in zip(box, entry["edge_cuts"])):
                errors.append(f"Edge.Cuts box {tuple(round(v, 3) for v in box)}, expected {entry['edge_cuts']}")
            extents.append(tuple(entry["edge_cuts"]))
    crt = geo["gfx"].get("F.CrtYd", [])
    if extents:
        need = (min(e[0] for e in extents), min(e[1] for e in extents),
                max(e[2] for e in extents), max(e[3] for e in extents))
        if not crt:
            errors.append("no courtyard")
        else:
            have = (min(p[0] for p in crt), min(p[1] for p in crt), max(p[0] for p in crt), max(p[1] for p in crt))
            margins = (need[0] - have[0], need[1] - have[1], have[2] - need[2], have[3] - need[3])
            if min(margins) < -0.01:
                errors.append(f"courtyard {tuple(round(v, 3) for v in have)} does not enclose the "
                              f"part {tuple(round(v, 3) for v in need)}")
            if max(margins) > tol(entry, "courtyard") + EPS:
                errors.append(f"courtyard margin {max(margins):.2f} > {tol(entry, 'courtyard')}")
    return errors


# --------------------------------------------------------------------------- tests

def test_every_board_footprint_has_an_entry():
    missing = [f for f in USED if f not in FPS]
    assert not missing, ("footprints on the board with no datasheet entry in "
                         f"hardware/tests/footprints.yaml: {missing}")


def test_no_stale_entries():
    stale = [f for f in FPS if f not in USED]
    assert not stale, f"footprints.yaml entries not used on the board: {stale}"


def test_entries_cite_a_source():
    for name, e in FPS.items():
        assert e.get("datasheet") and e.get("source"), f"{name}: datasheet and source are required"


@pytest.mark.parametrize("fp", BOARD, ids=lambda b: f"{b['ref']}-{b['lib_id'].split(':')[1]}")
def test_board_footprint_matches_datasheet(fp):
    if fp["lib_id"] not in FPS:
        pytest.fail(f"{fp['lib_id']} has no entry in footprints.yaml")
    geo = footprint_geometry(fp["node"], back=fp["back"], fp_angle=fp["angle"])
    errors = check_footprint(FPS[fp["lib_id"]], geo)
    assert not errors, f"{fp['ref']} ({fp['lib_id']}):\n  " + "\n  ".join(errors)


@pytest.mark.parametrize("lib_id", USED)
def test_library_footprint_matches_datasheet(lib_id):
    path = library_file(lib_id)
    if not path.exists():
        pytest.skip(f"{path} not installed (run in the irl2fa-hw image)")
    if lib_id not in FPS:
        pytest.fail(f"{lib_id} has no entry in footprints.yaml")
    errors = check_footprint(FPS[lib_id], footprint_geometry(parse(path.read_text())))
    assert not errors, f"{path.name}:\n  " + "\n  ".join(errors)


# --------------------------------------------------------------------------- symbol pins

def _norm(name):
    return re.sub(r"~\{([^}]*)\}", r"\1", str(name)).upper()


def _board_pads(ref):
    fp = next(b for b in BOARD if b["ref"] == ref)
    return fp, footprint_geometry(fp["node"], back=fp["back"], fp_angle=fp["angle"])


@needs_kicad
def test_symbol_pins_match_footprint_pads(netlist):
    """Every symbol pin has a pad and every numbered pad a symbol pin."""
    errors = []
    for fp in BOARD:
        comp = netlist["comps"].get(fp["ref"])
        if comp is None:     # board-only items (the e-paper outline)
            continue
        sym = set(netlist["libparts"][comp["libpart"]])
        _, geo = _board_pads(fp["ref"])
        pads = {p["num"] for p in geo["pads"] if p["num"]}
        if sym != pads:
            errors.append(f"{fp['ref']}: symbol pins {sorted(sym)} vs pads {sorted(pads)}")
        if comp["footprint"] != fp["lib_id"]:
            errors.append(f"{fp['ref']}: schematic footprint {comp['footprint']}, board {fp['lib_id']}")
    assert not errors, "\n".join(errors)


@needs_kicad
def test_symbol_pin_names_match_datasheets(netlist):
    errors = []
    for ref, comp in netlist["comps"].items():
        want = TABLE["parts"].get(comp["value"])
        if not want:
            continue
        sym = netlist["libparts"][comp["libpart"]]
        for num, name in want.items():
            if _norm(sym.get(str(num), "")) != _norm(name):
                errors.append(f"{ref} pin {num}: symbol {sym.get(str(num))!r}, datasheet {name!r}")
    assert not errors, "\n".join(errors)


@needs_kicad
def test_mcu_pads_match_pins_yaml(netlist):
    ref = "U1"
    sym = netlist["libparts"][netlist["comps"][ref]["libpart"]]
    _, geo = _board_pads(ref)
    pads = {p["num"]: p for p in geo["pads"]}
    errors = []
    for p in PINS["pins"]:
        num = str(p["pin"])
        if num not in pads:
            errors.append(f"pins.yaml pin {num} ({p['name']}) has no pad")
        want = "VSS" if p["name"] == "EP" else p["name"]
        if _norm(sym.get(num, "")) != want:
            errors.append(f"pin {num}: symbol {sym.get(num)!r}, pins.yaml {p['name']!r}")
    # the exposed pad is the big one in the middle, on GND, on the board too
    ep = pads.get("33")
    assert ep and ep["at"] == (0.0, 0.0) and min(ep["size"]) > 3, "pad 33 is not the exposed pad"
    assert netlist["pins"].get((ref, "33"), "").lstrip("/") == "GND", "EP not on GND in the schematic"
    pcb_net = next(find(p, "net")[2] for p in find_all(_board_pads(ref)[0]["node"], "pad")
                   if str(p[1]) == "33")
    assert pcb_net.lstrip("/") == "GND", f"EP on {pcb_net} on the board"
    assert not errors, "\n".join(errors)


@needs_kicad
def test_panel_fpc_pinout(netlist):
    panel = TABLE["panel"]
    errors = []
    for num, (name, net) in panel["pins"].items():
        got = netlist["pins"].get((panel["ref"], str(num)))
        got = None if got is None or got.startswith("unconnected-") else got.lstrip("/")
        if got != net:
            errors.append(f"{panel['ref']} pin {num} (panel {name}): net {got!r}, expected {net!r}")
    assert not errors, "\n".join(errors)
