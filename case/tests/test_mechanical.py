# SPDX-License-Identifier: CC-BY-SA-4.0
"""Mechanical checks against the manufacturers' drawings (docs/mechanical-review.md).

The case tests in test_case.py check the case against our own model of the
board. These check that model, and the board itself, against the parts'
drawings:

* our 3D models (as placed on the board, from hardware/fab/irl2fa.glb, which
  `make fab` exports from the KiCad board with every part model) have the
  drawing dimensions in DRAWINGS;
* no part model reaches into a printed part in the assembled position;
* the display flex fits its slot, reaches J1 pin 1 to pin 1, and folds where the
  drawing allows;
* the joystick cap matches the real stem, and the 45 deg rotation gives the
  directions in hardware/pins.yaml;
* nothing on the board lies in the coin cell's insertion path.

A test that encodes a requirement the current board does not meet is marked
xfail with the finding ID from docs/mechanical-review.md.

Run:  .venv/bin/python -m pytest case/tests -q
"""
from __future__ import annotations

import math
import sys
from functools import lru_cache
from pathlib import Path

import numpy as np
import pytest
import trimesh
import yaml

from test_case import REPO, assembled, board, info

sys.path.insert(0, str(REPO / "hardware" / "tools"))
from sexpr import parse  # noqa: E402

GLB = REPO / "hardware" / "fab" / "irl2fa.glb"
PCB = REPO / "hardware" / "irl2fa.kicad_pcb"
ORIGIN = (100.0, 100.0)          # board top-left in KiCad coordinates (tools/gen_pcb.py)

# ----------------------------------------------------------------------------- drawings
# Every dimension we rely on, with its source. Pages are the PDF page numbers.
GD = "Good Display GDEY0213B74 spec rev 1.0, p.7 (sec. 4 drawing)"
GDWT = "Good Display GDEM0213B74WT spec, p.6 (same panel and flex, fully dimensioned)"
ALPS = "Alps SKRH catalog (update 2510), p.2, Drawing No.2 (SKRHABE010)"
HRS = "Hirose FH34SRJ catalog (2018.11), p.3-4"
MY = "MYOUNG drawing MY-CP-0106 rev A/0 (LCSC C964845)"
VSH = "Vishay VSMB2948SL doc 83498 p.5 / VEMT2023SLX01 doc 84166 (drawing 6.544-5410.01-4, VEMT2x23SLX01)"
MURATA = "Murata CR2450R data sheet (Dec 2019), p.1"

DRAWINGS = {
    # display
    "glass_w": (59.20, 0.20, GD), "glass_h": (29.20, 0.20, GD), "glass_t": (1.0, 0.1, GD),
    "aa_w": (48.55, 0.05, GD), "aa_h": (23.7046, 0.05, GD), "aa_from_fpc_edge": (7.95, 0.2, GD + " / " + GDWT),
    "fpc_len": (14.30, 0.30, GD), "fpc_w": (12.50, 0.10, GD), "fpc_edge_from_top": (8.15, 0.30, GD),
    "fpc_flare_w": (14.30, 0.20, GDWT), "fpc_flare_from_top": (6.77, 0.20, GDWT + " / " + GD),
    "fpc_flare_len": (3.9, 0.3, GD + " (scaled, not dimensioned)"),
    "fpc_contact_pitch": (0.50, 0.0, GD), "fpc_pin1_from_edge": (0.50, 0.05, GD),
    "fpc_t_stiffener": (0.30, 0.03, GD), "fpc_stiffener_len": (6.00, 0.50, GD),
    "fpc_silicone_back": (1.0, 0.0, GD + ", p.35 inspection item 12 (silicone glue on the FPC <= 1.0 back / 0.5 front)"),
    # joystick
    "skrh_stem_top": (5.0, 0.05, ALPS), "skrh_stem_square": (1.95, 0.05, ALPS),
    "skrh_stem_fit_max": (1.7, 0.0, ALPS + " (keytop fitting area 1.7 max)"),
    "skrh_body_h": (1.8, 0.05, ALPS), "skrh_body_diag_c": (8.6, 0.15, ALPS),
    "skrh_shoulder_d": (3.0, 0.1, ALPS), "skrh_shoulder_top": (3.0, 0.15, ALPS),
    # FPC connector
    "fh34_height": (1.0, 0.05, HRS), "fh34_A": (14.0, 0.15, HRS), "fh34_depth": (3.8, 0.15, HRS),
    "fh34_fpc_t": (0.30, 0.03, HRS), "fh34_fpc_G": (12.5, 0.05, HRS),
    "fh34_insert_depth": (1.5, 0.3, HRS + " (uncovered area 1.5 +-0.3 ends at the housing opening)"),
    # battery clip and cell
    "clip_height": (5.70, 0.20, MY), "clip_plate_w": (25.50, 0.30, MY), "clip_feet_span": (35.80, 0.30, MY),
    "cell_d": (24.5, 0.3, MURATA), "cell_t": (5.0, 0.4, MURATA), "cell_neg_d": (21.0, 0.5, MURATA + " (reference)"),
    # IR side-lookers
    "ir_height": (2.3, 0.2, VSH), "ir_overall_depth": (2.55, 0.2, VSH), "ir_lens_d": (1.8, 0.2, VSH),
    "ir_lead_span": (3.8, 0.2, VSH), "ir_axis_h": (1.15, 0.15, VSH + " (lens centred on the 2.3 square face)"),
}


def dwg(key):
    return DRAWINGS[key][0]


def tol(key):
    return DRAWINGS[key][1]


def near(measured, key, extra=0.0):
    v, t, src = DRAWINGS[key]
    assert abs(measured - v) <= t + extra + 1e-6, f"{key}: model {measured:.3f}, drawing {v} +-{t} ({src})"


# ----------------------------------------------------------------------------- board file
@lru_cache
def pcb_tree():
    return parse(PCB.read_text(encoding="utf-8"))


def _find(node, key):
    return next((c for c in node if isinstance(c, list) and c and c[0] == key), None)


@lru_cache
def footprints() -> dict:
    """ref -> dict(x, y, rot, side, pads={num: [(x, y), ...]}) in board coordinates (mm, y down)."""
    out = {}
    for fp in (c for c in pcb_tree() if isinstance(c, list) and c and c[0] == "footprint"):
        at = _find(fp, "at")
        fx, fy = float(at[1]) - ORIGIN[0], float(at[2]) - ORIGIN[1]
        rot = float(at[3]) if len(at) > 3 else 0.0
        side = "B" if _find(fp, "layer")[1] == "B.Cu" else "F"
        ref = next(c[2] for c in fp if isinstance(c, list) and c[:2] == ["property", "Reference"])
        a = math.radians(rot)
        pads = {}
        for p in (c for c in fp if isinstance(c, list) and c and c[0] == "pad"):
            pat = _find(p, "at")
            px, py = float(pat[1]), float(pat[2])
            # KiCad: positive rotation is counter-clockwise on screen (y down)
            x = fx + px * math.cos(a) + py * math.sin(a)
            y = fy - px * math.sin(a) + py * math.cos(a)
            pads.setdefault(str(p[1]), []).append((x, y))
        out[ref] = dict(x=fx, y=fy, rot=rot, side=side, pads=pads)
    return out


@lru_cache
def vias() -> list[tuple[float, float]]:
    return [(float(_find(v, "at")[1]) - ORIGIN[0], float(_find(v, "at")[2]) - ORIGIN[1])
            for v in pcb_tree() if isinstance(v, list) and v and v[0] == "via"]


# ----------------------------------------------------------------------------- board 3D model
# KiCad's GLB is in metres, y up; its board body is 0..0.71 with the front parts
# seated at y = 0.795 and the back parts at y = -0.085. Map each side to the case
# frame (Z = 0 on the PCB front, PCB back at -thickness).
FRONT_MODELS = ("EPD_", "SW_Nav")


@lru_cache
def glb_points(per_mesh: int = 300) -> dict[str, np.ndarray]:
    """model name -> points (board x, board y, Z) on its surface (vertices + samples)."""
    if not GLB.exists():
        pytest.fail(f"{GLB.relative_to(REPO)} is missing: run `make fab`")
    scene = trimesh.load(GLB)
    t = board()["board"]["thickness"]
    out: dict[str, list] = {}
    rng = np.random.default_rng(1)
    for node in scene.graph.nodes_geometry:
        T, g = scene.graph[node]
        if g.startswith("irl2fa_PCB"):
            continue
        m = scene.geometry[g].copy().apply_transform(T)
        v = m.vertices * 1000.0
        if len(m.faces):
            mm = trimesh.Trimesh(v, m.faces, process=False)
            if mm.area > 0:
                v = np.vstack([v, trimesh.sample.sample_surface(mm, per_mesh, seed=rng)[0]])
        front = g.startswith(FRONT_MODELS)
        z = v[:, 1] - (0.795 if front else -0.085 + t)
        name = g.rsplit("_", 1)[0] if g[-1].isdigit() else g
        for k in ("SW_Nav_Alps_SKRHABE010", "EPD_GoodDisplay_GDEY0213B74_Outline", "BatteryHolder_MYOUNG",
                  "Hirose_FH34SRJ", "LED_Vishay_VSMB2948SL", "PhotoTransistor_Vishay_VEMT2023SLX01"):
            if g.startswith(k):
                name = k
        out.setdefault(name, []).append(np.c_[v[:, 0] - ORIGIN[0], v[:, 2] - ORIGIN[1], z])
    return {k: np.vstack(v) for k, v in out.items()}


def model(name: str) -> np.ndarray:
    return glb_points()[name]


def sel(p, x=None, y=None, z=None):
    m = np.ones(len(p), bool)
    for i, r in enumerate((x, y, z)):
        if r is not None:
            m &= (p[:, i] >= r[0]) & (p[:, i] <= r[1])
    return p[m]


def ext(p, i):
    return float(p[:, i].max() - p[:, i].min())


# ----------------------------------------------------------------------------- models vs drawings
def test_display_model_matches_drawing():
    p = model("EPD_GoodDisplay_GDEY0213B74_Outline")
    g = board()["display"]["glass"]
    glass = sel(p, x=(g["x"] - 0.01, g["x"] + g["w"] + 0.01), z=(0.3, 2.0))
    near(ext(glass, 0), "glass_w")
    near(ext(glass, 1), "glass_h")
    near(glass[:, 2].max() - board()["display"]["adhesive"], "glass_t")
    a = board()["display"]["active"]
    near(a["w"], "aa_w")
    near(a["h"], "aa_h")
    near(a["x"] - g["x"], "aa_from_fpc_edge")


def test_display_fpc_model_matches_drawing():
    """Width where the flex passes the slot (the flare), and the stiffened end in J1."""
    p = model("EPD_GoodDisplay_GDEY0213B74_Outline")
    s, g = board()["fpc_slot"], board()["display"]["glass"]
    t = board()["board"]["thickness"]
    in_slot = sel(p, x=(s["x"], s["x"] + s["w"]), z=(-t + 0.05, -0.05))
    near(ext(in_slot, 1), "fpc_flare_w")
    near(in_slot[:, 1].min() - g["y"], "fpc_flare_from_top")
    j1 = footprints()["J1"]
    xs = sorted(x for x, _ in j1["pads"]["1"])
    entry = xs[0] - 3.05                              # FPC opening (footprint: 3.05 from the signal pads)
    tail = sel(p, x=(entry + 0.1, entry + 1.4), z=(-t - 2.0, -t + 0.3))
    near(ext(tail, 2), "fpc_t_stiffener", extra=0.02)
    near(ext(tail, 1), "fpc_w")
    near(tail[:, 1].min() - g["y"], "fpc_edge_from_top")


def test_joystick_model_matches_drawing():
    p = model("SW_Nav_Alps_SKRHABE010")
    j = board()["joystick"]
    near(p[:, 2].max(), "skrh_stem_top")
    stem = sel(p, z=(3.4, 4.95))
    # flats parallel to the board edges: the extent along x and y is the across-flats size
    near(ext(stem, 0), "skrh_stem_square")
    near(ext(stem, 1), "skrh_stem_square")
    body = sel(p, z=(0.2, 1.7))
    near(ext(body, 0), "skrh_body_diag_c")       # the operating diagonal lies along board x
    r = np.hypot(p[:, 0] - j["x"], p[:, 1] - j["y"])
    near(p[(r > 3.0) & (p[:, 2] > 0.2), 2].max(), "skrh_body_h")
    sh = sel(p, z=(2.3, 2.9))
    near(ext(sh, 0), "skrh_shoulder_d")


def test_connector_model_matches_drawing():
    p = model("Hirose_FH34SRJ")
    t = board()["board"]["thickness"]
    near(-t - p[:, 2].min(), "fh34_height")
    near(ext(sel(p, z=(-t - 1.0, -t - 0.6)), 1), "fh34_A")
    near(ext(p, 0), "fh34_depth")


def test_battery_clip_model_matches_drawing():
    p = model("BatteryHolder_MYOUNG")
    t = board()["board"]["thickness"]
    near(-t - p[:, 2].min(), "clip_height")
    near(ext(sel(p, z=(-t - 5.8, -t - 5.4)), 0), "clip_plate_w")
    near(ext(sel(p, z=(-t - 0.2, -t)), 0), "clip_feet_span")
    b = board()["battery"]
    near(b["cell_diameter"], "cell_d")
    near(b["cell_thickness"], "cell_t")
    assert b["holder_height"] == pytest.approx(dwg("clip_height"))


@pytest.mark.parametrize("name,ref", [("LED_Vishay_VSMB2948SL", "D4"), ("PhotoTransistor_Vishay_VEMT2023SLX01", "Q5")])
def test_ir_model_matches_drawing(name, ref):
    p = model(name)
    t = board()["board"]["thickness"]
    near(-t - p[:, 2].min(), "ir_height")
    near(ext(p, 1), "ir_overall_depth")
    near(ext(sel(p, z=(-t - 0.3, -t)), 0), "ir_lead_span")
    lens = sel(p, y=(-0.01, 0.9))
    near(-t - (lens[:, 2].max() + lens[:, 2].min()) / 2, "ir_axis_h")
    assert abs(p[:, 1].min()) <= 0.05, f"{ref}: lens tip at y = {p[:, 1].min():.2f}, not on the top edge"


# ----------------------------------------------------------------------------- interference
def test_no_part_model_reaches_into_the_case():
    """Every part model on the board, in its assembled position, against every printed part.

    The only intended overlap is the cap's crush ribs on the joystick stem."""
    i = info()
    bad = []
    for part in ("front", "back", "door", "cap"):
        m = assembled(part)
        lo, hi = m.bounds[0] - 0.01, m.bounds[1] + 0.01
        for name, p in glb_points().items():
            p = np.c_[p[:, 0], board()["board"]["height"] - p[:, 1], p[:, 2]]    # board -> case frame
            q = p[np.all((p >= lo) & (p <= hi), axis=1)]
            if part == "cap" and name.startswith("SW_Nav"):
                q = q[q[:, 2] < i["cap_socket_bottom"] - 1e-3]   # the stem in the socket is a press fit
            if not len(q):
                continue
            inside = m.contains(q)
            if inside.any():
                r = q[inside]
                bad.append(f"{name} into {part}: {inside.sum()} points, "
                           f"x {r[:, 0].min():.2f}..{r[:, 0].max():.2f}, y {r[:, 1].min():.2f}..{r[:, 1].max():.2f} (model Y), "
                           f"z {r[:, 2].min():.2f}..{r[:, 2].max():.2f}")
    assert not bad, "\n".join(bad)


# ----------------------------------------------------------------------------- display flex
def fpc_flare_span():
    """Board y range the 14.3 mm flare can occupy, with its position and width tolerances."""
    gy = board()["display"]["glass"]["y"]
    y0 = gy + dwg("fpc_flare_from_top")
    return y0 - tol("fpc_edge_from_top") - tol("fpc_flare_w") / 2, \
        y0 + dwg("fpc_flare_w") + tol("fpc_edge_from_top") + tol("fpc_flare_w") / 2


def test_fpc_flare_fits_the_slot():
    s = board()["fpc_slot"]
    g = board()["display"]["glass"]
    # the slot sits inside the flare's length: the flare (not the 12.5 tail) goes through it
    assert g["x"] - s["x"] < dwg("fpc_flare_len") + tol("fpc_flare_len")
    lo, hi = fpc_flare_span()
    clear = 0.2
    # the flex runs along the slot's wall, so only the straight part between the round ends counts
    y0, y1 = s["y"] + s["w"] / 2, s["y"] + s["h"] - s["w"] / 2
    assert y0 <= lo - clear and y1 >= hi + clear, \
        f"slot straight part y {y0}..{y1} must cover the flare {lo:.2f}..{hi:.2f} + {clear}"


def test_fpc_fold_starts_clear_of_the_glued_root():
    """The fold must start beyond the silicone on the flex (1.0 mm on the back face), with a bend
    radius of at least 6x the 0.12 mm flex (IPC-2223 single-sided, bend to install)."""
    s, g = board()["fpc_slot"], board()["display"]["glass"]
    room = (g["x"] - (s["x"] + s["w"])) - dwg("fpc_silicone_back")
    assert room >= 6 * 0.12, f"only {room:.2f} mm between the silicone and the slot for the first bend"


def test_fpc_pin1_meets_j1_pin1():
    gy = board()["display"]["glass"]["y"]
    j1 = footprints()["J1"]
    p1 = dwg("fpc_edge_from_top") + dwg("fpc_pin1_from_edge")
    (x1, y1), = j1["pads"]["1"]
    (x24, y24), = j1["pads"]["24"]
    assert y1 == pytest.approx(gy + p1, abs=0.05), "J1 pin 1 is not level with the panel's pin 1"
    assert y24 == pytest.approx(gy + p1 + 23 * dwg("fpc_contact_pitch"), abs=0.05)
    # the opening faces the slot: the fitting nails (FPC side) are nearer the slot than the signal pads
    assert max(x for x, _ in j1["pads"]["MP"]) < x1
    assert x1 > board()["fpc_slot"]["x"]


def test_fpc_reaches_j1_and_the_stiffener_lies_flat():
    """Shortest path: off the glass at the TFT face, round the slot's right wall, under the board into
    J1 at its opening height. The flex must be long enough at its minimum length, and the
    6 mm stiffener (at its longest) must lie flat between the fold and J1."""
    s, g = board()["fpc_slot"], board()["display"]["glass"]
    t, adh = board()["board"]["thickness"], board()["display"]["adhesive"]
    j1 = footprints()["J1"]
    x_pad = min(x for x, _ in j1["pads"]["1"])
    entry = x_pad - 3.05
    tip = entry + dwg("fh34_insert_depth") + tol("fh34_insert_depth")
    R = 6 * 0.12 + 0.06
    ze, zj = adh + 0.6 + 0.06, -t - 0.5
    xr = s["x"] + s["w"]
    need = (g["x"] - xr) + (ze - zj) + (tip - xr) - 2 * (2 - math.pi / 2) * R
    shortest = dwg("fpc_len") - tol("fpc_len")
    assert need <= shortest, f"the flex needs {need:.2f} mm, the shortest is {shortest:.2f}"
    stiff = dwg("fpc_stiffener_len") + tol("fpc_stiffener_len")
    assert tip - stiff >= xr + R, "the stiffener would sit in the fold"
    slack = dwg("fpc_len") + tol("fpc_len") - need
    print(f"FPC: needs {need:.2f} mm, {slack:.2f} mm of slack at the longest (forms a loop under the board)")


# ----------------------------------------------------------------------------- joystick
def test_cap_socket_matches_the_stem():
    i = info()
    assert i["stem_square"] == pytest.approx(dwg("skrh_stem_square"))
    sw = footprints()["SW1"]
    # the stem flats are at 45 deg to the body; the body is at sw["rot"] on the board
    assert (sw["rot"] + 45 - i["stem_angle"]) % 90 == pytest.approx(0, abs=0.01), \
        "the cap socket is not aligned with the stem flats"
    fit = dwg("skrh_stem_top") - i["cap_socket_bottom"]
    assert 1.0 <= fit <= dwg("skrh_stem_fit_max") + 1e-6, f"cap grips {fit:.2f} mm of stem (Alps: 1.7 max)"
    assert i["cap_socket_bottom"] > i["joystick_collars"][3], "socket floor would land on the stem shoulder"
    # measure the printed socket across its flats, at mid engagement
    j = board()["joystick"]
    cap = assembled("cap")
    z = (i["cap_socket_bottom"] + dwg("skrh_stem_top")) / 2
    for d in ((1, 0), (0, 1)):
        hits = []
        for sgn in (1, -1):
            o = (j["x"], board()["board"]["height"] - j["y"], z)
            dv = (sgn * d[0], sgn * d[1], 0)
            loc, _, _ = cap.ray.intersects_location([o], [dv])
            hits.append(min(np.linalg.norm(loc - o, axis=1)))
        # the rib on each face grips the stem: half-flat minus the interference
        assert hits[0] + hits[1] == pytest.approx(dwg("skrh_stem_square") - 2 * i["stem_rib_interference"], abs=0.06), \
            f"socket across the ribs along {d}: {hits[0] + hits[1]:.3f} mm"


def test_joystick_directions_match_pins_yaml():
    """Alps: pushing towards a body corner closes that corner's contact (A top-left, B top-right,
    C bottom-left, D bottom-right in the top view; pads 1/4/3/6). Turn by the footprint rotation."""
    pins = yaml.safe_load((REPO / "hardware" / "pins.yaml").read_text(encoding="utf-8"))["joystick"]
    a = math.radians(footprints()["SW1"]["rot"])
    want = {"left": (-1, 0), "right": (1, 0), "up": (0, -1), "down": (0, 1)}
    for net, (cx, cy) in {"JOY_A": (-1, -1), "JOY_B": (1, -1), "JOY_C": (-1, 1), "JOY_D": (1, 1)}.items():
        x = cx * math.cos(a) + cy * math.sin(a)
        y = -cx * math.sin(a) + cy * math.cos(a)
        wx, wy = want[pins[net]]
        assert (x * wx + y * wy) / math.hypot(x, y) > 0.99, f"{net} points ({x:.2f}, {y:.2f}), pins.yaml says {pins[net]}"


# ----------------------------------------------------------------------------- coin cell
def test_cell_lands_on_its_negative_pad():
    bt = footprints()["BT1"]
    (px, py), = bt["pads"]["2"]
    b = board()["battery"]
    assert (px, py) == pytest.approx((b["x"], b["y"]), abs=0.05)
    # pad 2 (12 mm) well inside the negative cap; the clip's + legs outside the cell
    assert 12.0 <= dwg("cell_neg_d") - 2 * tol("cell_neg_d") - 2.0
    for x, y in bt["pads"]["1"]:
        assert math.hypot(x - b["x"], y - b["y"]) - 6.15 / 2 >= b["cell_diameter"] / 2 - 0.05


def test_nothing_on_bcu_under_the_negative_cap_but_the_contact():
    """MECH-04: copper under solder mask stands higher than the bare contact pad, so on
    B.Cu under the cell's negative cap (Ø21) there is only pad 2 and its one spoke,
    straight down (+y), whose solder mask is open. No other track, no pour."""
    b = board()["battery"]
    cx, cy = b["x"], b["y"]
    r_cap = dwg("cell_neg_d") / 2
    spoke = 0.6 + 1e-3                      # half width of the spoke's corridor (gen_pcb.py SPOKE_W)

    def in_spoke(x, y):
        return abs(x - cx) <= spoke and y > cy

    bad = []
    for s in (c for c in pcb_tree() if isinstance(c, list) and c and c[0] in ("segment", "arc")):
        if _find(s, "layer")[1] != "B.Cu":
            continue
        (x0, y0), (x1, y1) = ((float(_find(s, k)[1]) - ORIGIN[0], float(_find(s, k)[2]) - ORIGIN[1])
                              for k in ("start", "end"))
        hw = float(_find(s, "width")[1]) / 2
        for i in range(21):
            x, y = x0 + (x1 - x0) * i / 20, y0 + (y1 - y0) * i / 20
            if math.hypot(x - cx, y - cy) < r_cap + hw - 0.05 and not (in_spoke(x - hw, y) and in_spoke(x + hw, y)):
                bad.append(f"track at ({x:.2f}, {y:.2f})")
                break
    for z in (c for c in pcb_tree() if isinstance(c, list) and c and c[0] == "zone"):
        for fp in (c for c in z if isinstance(c, list) and c and c[0] == "filled_polygon"):
            if _find(fp, "layer")[1] != "B.Cu":
                continue
            for xy in (c for c in _find(fp, "pts") if isinstance(c, list) and c[0] == "xy"):
                x, y = float(xy[1]) - ORIGIN[0], float(xy[2]) - ORIGIN[1]
                r = math.hypot(x - cx, y - cy)
                if 6.0 + 0.05 < r < r_cap - 0.05 and not in_spoke(x, y):
                    bad.append(f"pour at ({x:.2f}, {y:.2f})")
                    break
    assert not bad, f"B.Cu copper under the cell's negative cap: {bad[:5]}"


def test_cell_insertion_strip_has_no_vias():
    b = board()["battery"]
    r = b["cell_diameter"] / 2 + 0.3
    H = board()["board"]["height"]
    bad = [(x, y) for x, y in vias() if abs(x - b["x"]) <= r + 0.3 and b["y"] <= y <= H]
    bad += [(x, y) for x, y in vias() if math.hypot(x - b["x"], y - b["y"]) <= r + 0.3]
    assert not bad, f"vias in the coin cell's path: {bad}"


def test_cell_insertion_strip_has_no_parts():
    b = board()["battery"]
    r = b["cell_diameter"] / 2
    H = board()["board"]["height"]
    bad = []
    for ref, fp in footprints().items():
        if ref in ("BT1",) or fp["side"] != "B":
            continue
        for num, pts in fp["pads"].items():
            for x, y in pts:
                if abs(x - b["x"]) <= r + 0.5 and b["y"] <= y <= H:
                    bad.append(f"{ref}.{num} at ({x:.2f}, {y:.2f})")
    assert not bad, "pads in the coin cell's path: " + ", ".join(bad)


def test_clip_clears_the_battery_door():
    i = info()
    t = board()["board"]["thickness"]
    top = -t - (dwg("clip_height") + tol("clip_height"))
    assert top - i["z_cav_bot"] >= 0.25, f"tallest clip ends {top - i['z_cav_bot']:.2f} mm from the door"
