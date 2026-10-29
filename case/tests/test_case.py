# SPDX-License-Identifier: CC-BY-SA-4.0
"""Geometry tests for the IRL2FA case.

They load the committed/built STLs (case/stl), move them back from print
orientation into assembly coordinates (using case/stl/irl2fa-case-info.json,
written by build.sh from the .scad), and check them against a simple model of
the PCB and its parts built from hardware/board.yaml.

Assembly coordinates (same as irl2fa_case.scad): X = board x, Y = board_height -
board y (the IR edge is at +Y), Z = 0 on the PCB front, screen towards +Z.

Run:  .venv/bin/python -m pytest case/tests -q
"""
from __future__ import annotations

import json
import subprocess
import sys
from functools import lru_cache
from pathlib import Path

import manifold3d as mf
import numpy as np
import pytest
import trimesh
import yaml

CASE = Path(__file__).resolve().parents[1]
REPO = CASE.parent
STL = CASE / "stl"
PARTS = ("front", "back", "door", "cap")
CLEAR = 0.3          # required clearance case <-> parts, mm
MIN_WALL = 1.2       # required wall thickness, mm
BED = (220.0, 220.0, 250.0)


# --------------------------------------------------------------------------- data
@lru_cache
def board() -> dict:
    return yaml.safe_load((REPO / "hardware" / "board.yaml").read_text(encoding="utf-8"))


@lru_cache
def info() -> dict:
    return json.loads((STL / "irl2fa-case-info.json").read_text(encoding="utf-8"))


def M(x: float, y: float) -> tuple[float, float]:
    """board (x, y) -> model (X, Y)"""
    return x, board()["board"]["height"] - y


@lru_cache
def printed(part: str) -> trimesh.Trimesh:
    return trimesh.load_mesh(STL / f"irl2fa-{part}.stl")


@lru_cache
def assembled(part: str) -> trimesh.Trimesh:
    m = printed(part).copy()
    t = info()["print"][part]
    v = m.vertices.copy()
    if t["flip"]:   # print = Rx(180) then translate(0, ty, tz); it is an involution
        v[:, 1] = t["ty"] - v[:, 1]
        v[:, 2] = t["tz"] - v[:, 2]
        m = trimesh.Trimesh(v, m.faces[:, ::-1], process=False)   # keep outward normals
    else:
        v[:, 2] = v[:, 2] - t["tz"]
        m = trimesh.Trimesh(v, m.faces, process=False)
    return m


def to_manifold(m: trimesh.Trimesh) -> mf.Manifold:
    mesh = mf.Mesh(vert_properties=np.asarray(m.vertices, dtype=np.float32),
                   tri_verts=np.asarray(m.faces, dtype=np.uint32))
    out = mf.Manifold(mesh)
    assert out.status() == mf.Error.NoError, f"not a valid manifold: {out.status()}"
    return out


@lru_cache
def solid(part: str) -> mf.Manifold:
    return to_manifold(assembled(part))


# --------------------------------------------------------------------------- part model
def board_xs(grow: float = 0.0) -> mf.CrossSection:
    b = board()["board"]
    r = b["corner_radius"]
    return mf.CrossSection.square([b["width"] - 2 * r, b["height"] - 2 * r]).translate([r, r]) \
        .offset(r + grow, mf.JoinType.Round, circular_segments=64)


def rect_xs(x, y, w, h) -> mf.CrossSection:
    X, Y = M(x, y + h)
    return mf.CrossSection.square([w, h]).translate([X, Y])


def prism(xs: mf.CrossSection, z0: float, z1: float) -> mf.Manifold:
    return mf.Manifold.extrude(xs, z1 - z0).translate([0, 0, z0])


def boss_zones(grow: float) -> mf.CrossSection:
    """Mounting-hole zones the case bosses occupy: disc + the corner quadrant."""
    b = board()["board"]
    out = []
    for h in b["mounting_holes"]:
        X, Y = M(h["x"], h["y"])
        out.append(mf.CrossSection.circle(info()["boss_d"] / 2 + grow, 48).translate([X, Y]))
        sx = -1 if X < b["width"] / 2 else 1
        sy = -1 if Y < b["height"] / 2 else 1
        q = mf.CrossSection.square([10, 10]).translate([0 if sx > 0 else -10, 0 if sy > 0 else -10])
        out.append(q.offset(grow, mf.JoinType.Miter).translate([X, Y]))
    return mf.CrossSection.batch_boolean(out, mf.OpType.Add)


def front_parts(c: float) -> dict[str, mf.Manifold]:
    """Front-side parts, each grown by c."""
    d, j = board()["display"], board()["joystick"]
    g = d["glass"]
    glass_top = d["adhesive"] + g["t"]
    parts = {"glass": prism(rect_xs(g["x"], g["y"], g["w"], g["h"]).offset(c, mf.JoinType.Miter), 0, glass_top + c)}
    jx, jy = M(j["x"], j["y"])
    body = mf.CrossSection.square([j["body"], j["body"]], center=True).rotate(45) \
        .offset(c, mf.JoinType.Miter).translate([jx, jy])
    parts["joystick body"] = prism(body, 0, j["body_height"] + c)
    # collars between body and stem (Alps STEP), mirrors joystick_collars in the .scad
    for i, (dia, top) in enumerate([(5.2, 2.15), (4.0, 2.95)]):
        parts[f"joystick collar {i}"] = prism(mf.CrossSection.circle(dia / 2 + c, 48).translate([jx, jy]), 0, top + c)
    s = board()["fpc_slot"]
    fy = g["y"] + g["h"] / 2
    parts["fpc"] = prism(rect_xs(s["x"], fy - d["fpc_width"] / 2, g["x"] - s["x"], d["fpc_width"])
                         .offset(c, mf.JoinType.Miter), 0, 0.3 + c)
    return parts


def back_parts(c: float) -> dict[str, mf.Manifold]:
    """Everything on the back: the full keepout prism (board outline minus the boss zones)."""
    t = board()["board"]["thickness"]
    k = board()["back_keepout_height"]
    xs = board_xs() - boss_zones(c + 0.05)
    return {"back keepout": prism(xs.offset(c, mf.JoinType.Round, circular_segments=48), -t - k - c, -t)}


def pcb(c: float) -> mf.Manifold:
    t = board()["board"]["thickness"]
    return prism(board_xs(c), -t + 0.02, -0.02)   # grown sideways only: the bosses clamp it


# --------------------------------------------------------------------------- helpers
def ray_hits(mesh: trimesh.Trimesh, origin, direction) -> np.ndarray:
    """Distances along the ray to every surface crossing, sorted."""
    loc, idx, _ = mesh.ray.intersects_location([origin], [direction], multiple_hits=True)
    if len(loc) == 0:
        return np.array([])
    d = np.dot(loc - np.asarray(origin, float), np.asarray(direction, float))
    return np.unique(np.round(np.sort(d), 6))


def thickness_at(mesh, origin, direction) -> float:
    d = ray_hits(mesh, origin, direction)
    assert len(d) >= 2, f"ray from {origin} along {direction} does not cross a wall"
    return float(d[1] - d[0])


# --------------------------------------------------------------------------- tests
def test_board_dims_up_to_date():
    r = subprocess.run([sys.executable, str(CASE / "tools" / "gen_dims.py"), "--check"],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr


@pytest.mark.parametrize("part", PARTS)
def test_mesh_is_valid(part):
    m = printed(part)
    assert m.is_watertight, f"{part} is not watertight"
    assert m.is_winding_consistent, f"{part} has inconsistent winding"
    assert m.volume > 0, f"{part} has non-positive volume"
    assert len(m.split(only_watertight=False)) == 1, f"{part} has floating pieces"
    to_manifold(m)   # manifold3d accepts it as a 2-manifold


@pytest.mark.parametrize("part", PARTS)
def test_fits_bed_and_lies_flat(part):
    m = printed(part)
    lo, hi = m.bounds
    assert np.all(hi - lo <= BED), f"{part} ({hi - lo}) does not fit a 220x220 bed"
    assert abs(lo[2]) < 1e-3, f"{part} does not start at z = 0"
    # flat on its largest face: plenty of downward-facing area on the bed
    on_bed = (m.triangles_center[:, 2] < 1e-3) & (m.face_normals[:, 2] < -0.999)
    footprint = (hi[0] - lo[0]) * (hi[1] - lo[1])
    assert m.area_faces[on_bed].sum() > 0.25 * footprint, f"{part} is not printed on a large flat face"


def test_printed_parts_do_not_overlap():
    names = ("front", "back", "door")
    for i, a in enumerate(names):
        for b in names[i + 1:]:
            v = (solid(a) ^ solid(b)).volume()
            assert v < 1e-3, f"{a} and {b} overlap by {v:.4f} mm^3"


@pytest.mark.parametrize("part,samples", [
    # back floor (board coords; clear of door, bosses, lanyard)
    ("back", [("z", (10, 10)), ("z", (10, 24)), ("z", (70, 10)), ("z", (70, 24)), ("z", (18, 17)), ("z", (62, 17))]),
    # back side walls, mid height
    ("back", [("-y", 10), ("-y", 22), ("-y", 40), ("-y", 58), ("-y", 70), ("+y", 10), ("+y", 18), ("+y", 62), ("+y", 70),
              ("+x", 17), ("+x", 10), ("-x", 17), ("-x", 24)]),
    # front skin around the window and its walls
    ("front", [("-z", (8, 10)), ("-z", (8, 24)), ("-z", (40, 3.5)), ("-z", (40, 30.5)), ("-z", (67.5, 8))]),
    ("front", [("-y", 20), ("-y", 40), ("-y", 60), ("+y", 20), ("+y", 40), ("+y", 60), ("+x", 17), ("-x", 17)]),
    ("door", [("z", (40, 17)), ("z", (34, 12)), ("z", (46, 22))]),
])
def test_wall_thickness(part, samples):
    m = assembled(part)
    i = info()
    W, H = board()["board"]["width"], board()["board"]["height"]
    zw = (i["z_bot"] + i["z_seam"]) / 2 if part == "back" else 1.0
    out = []
    for kind, p in samples:
        if kind == "z":
            X, Y = M(*p)
            o, d = (X, Y, i["z_bot"] - 5), (0, 0, 1)
        elif kind == "-z":
            X, Y = M(*p)
            o, d = (X, Y, i["z_top"] + 5), (0, 0, -1)
        elif kind == "-y":   # top (IR) edge, ray going in
            o, d = (p, H + 10, zw), (0, -1, 0)
        elif kind == "+y":   # bottom edge
            o, d = (p, -10, zw), (0, 1, 0)
        elif kind == "+x":   # left end
            o, d = (-15, M(0, p)[1], zw), (1, 0, 0)
        else:                # right end
            o, d = (W + 15, M(0, p)[1], zw), (-1, 0, 0)
        t = thickness_at(m, o, d)
        out.append((kind, p, round(t, 3)))
        assert t >= MIN_WALL - 1e-3, f"{part}: wall {t:.3f} mm < {MIN_WALL} at {kind} {p}"
    print(part, out)


def test_clearance_to_parts():
    """Grow every part by 0.3 mm: it must not touch any printed part."""
    case = {p: solid(p) for p in ("front", "back", "door", "cap")}
    parts = {**front_parts(CLEAR), **back_parts(CLEAR), "pcb edge": pcb(CLEAR)}
    bad = []
    for pn, ps in parts.items():
        for cn, cs in case.items():
            v = (ps ^ cs).volume()
            if v > 1e-4:
                bad.append(f"{pn} vs {cn}: {v:.4f} mm^3")
    assert not bad, "clearance < 0.3 mm:\n" + "\n".join(bad)


def test_cap_clears_case_hole():
    i, j = info(), board()["joystick"]
    jx, jy = M(j["x"], j["y"])
    ring = prism(mf.CrossSection.circle(i["cap_d"] / 2 + CLEAR, 96).translate([jx, jy]),
                 i["cap_bottom"], i["cap_top"])
    v = (ring ^ solid("front")).volume()
    assert v < 1e-4, f"cap is closer than {CLEAR} mm to the front shell ({v:.4f} mm^3)"
    assert i["cap_top"] - i["z_top"] >= 1.5 - 1e-6, "cap does not protrude 1.5 mm"


def test_screen_window_covers_active_area():
    a, i = board()["display"]["active"], info()
    m = assembled("front")
    xs = np.linspace(a["x"] + 1e-3, a["x"] + a["w"] - 1e-3, 25)
    ys = np.linspace(a["y"] + 1e-3, a["y"] + a["h"] - 1e-3, 13)
    origins = [(*M(x, y), i["z_top"] + 5) for x in xs for y in ys]
    hit = m.ray.intersects_any(origins, [(0, 0, -1)] * len(origins))
    assert not hit.any(), f"{hit.sum()} points of the active area are covered by the bezel"


def test_joystick_hole_centred_on_stem():
    i, j = info(), board()["joystick"]
    jx, jy = M(j["x"], j["y"])
    z = (i["z_top"] + j["body_height"] + i["part_clearance"]) / 2   # inside the skin, below the chamfer
    sec = assembled("front").section(plane_origin=(0, 0, z), plane_normal=(0, 0, 1))
    planar, _ = sec.to_2D(to_2D=np.eye(4))
    rings = [np.asarray(r.coords) for poly in planar.polygons_full for r in poly.interiors]
    best = min(rings, key=lambda r: np.hypot(*(r[:-1].mean(axis=0) - (jx, jy))))
    c = best[:-1].mean(axis=0)
    off = float(np.hypot(c[0] - jx, c[1] - jy))
    assert off <= 0.2, f"joystick hole centre is {off:.3f} mm off the stem"
    r = np.hypot(best[:, 0] - c[0], best[:, 1] - c[1])
    assert abs(r.mean() - i["cap_hole_d"] / 2) < 0.1


def _ir_openings() -> list[tuple[float, float]]:
    i, H = info(), board()["board"]["height"]
    m = assembled("back")
    z = i["z_seam"] - i["ir_window_h"] / 2
    xs = np.arange(1.0, board()["board"]["width"] - 1.0, 0.05)
    y_in = H + i["pcb_clearance"]
    open_ = []
    for x in xs:
        d = ray_hits(m, (x, H + 10, z), (0, -1, 0))
        ys = H + 10 - d
        wall = ys[ys >= y_in - 1e-3]
        if i["ir_window"] == "thin":
            open_.append(len(wall) >= 2 and wall[0] - wall[1] < 1.0)
        else:
            open_.append(len(wall) == 0)
    spans, start = [], None
    for x, o in zip(xs, open_ + [False]):
        if o and start is None:
            start = x
        if not o and start is not None:
            spans.append((start, x - 0.05))
            start = None
    return spans


def test_ir_windows_align_with_emitter_and_detector():
    ir = board()["ir"]
    spans = _ir_openings()
    centres = [(a + b) / 2 for a, b in spans]
    assert len(centres) == 2, f"expected 2 IR windows, found {spans}"
    for name in ("emitter", "detector"):
        x = ir[name]["x"]
        off = min(abs(c - x) for c in centres)
        assert off <= 0.3, f"IR window for the {name} is {off:.3f} mm off x = {x}"
    for a, b in spans:
        assert b - a >= info()["ir_window_w"] - 0.2


def test_top_edge_is_flat_and_close():
    """Nothing sticks out past the IR edge, and two butted cases put emitter and detector <= 5 mm apart."""
    H = board()["board"]["height"]
    i = info()
    y_out = H + i["pcb_clearance"] + i["wall"]
    for p in ("front", "back", "door"):
        assert assembled(p).bounds[1][1] <= y_out + 1e-3, f"{p} sticks out past the IR edge"
    m = assembled("back")
    z = i["z_seam"] - i["ir_window_h"] - 1.0   # below the windows, on the flat face
    for x in np.linspace(10, 70, 13):
        d = ray_hits(m, (x, H + 10, z), (0, -1, 0))
        assert abs((H + 10 - d[0]) - y_out) < 1e-3, f"top edge not flat at x = {x:.1f}"
    # the lens tips sit on the board edge (board.yaml): gap = twice the edge-to-face distance
    gap = 2 * (y_out - H)
    assert gap <= 5.0, f"emitter-detector distance {gap:.2f} mm > 5 mm"


@pytest.mark.parametrize("part", PARTS)
def test_printable_without_supports(part):
    """No overhang steeper than 45 deg except flat bridges, and every bridge spans <= 10 mm.

    Tiny spherical features (the door detent bump/dimple) are allowed up to 3 mm^2.
    """
    m = printed(part)
    nz = m.face_normals[:, 2]
    zc = m.triangles_center[:, 2]
    off_bed = zc > 1e-3
    bridge = off_bed & (nz < -0.999)
    steep = off_bed & (nz < -0.72) & ~bridge
    assert m.area_faces[steep].sum() < 3.0, \
        f"{part}: {m.area_faces[steep].sum():.2f} mm^2 of >45 deg overhang"
    # bridges: group connected horizontal down-facing faces; the narrow side must be <= 10 mm
    idx = np.nonzero(bridge)[0]
    if len(idx):
        adj = m.face_adjacency
        keep = bridge[adj[:, 0]] & bridge[adj[:, 1]]
        groups = trimesh.graph.connected_components(adj[keep], nodes=idx)
        for g in groups:
            pts = m.vertices[m.faces[g].ravel()][:, :2]
            span = min(np.ptp(pts[:, 0]), np.ptp(pts[:, 1]))
            assert span <= 10.0 + 1e-3, f"{part}: bridge of {span:.1f} mm at z = {zc[g[0]]:.2f}"
