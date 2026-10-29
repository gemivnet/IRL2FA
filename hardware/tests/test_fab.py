"""Fabrication outputs: committed files are current, and the Gerbers parse
and respect the board contract and JLCPCB's standard capabilities.
SPDX-License-Identifier: CERN-OHL-S-2.0"""
import csv
import subprocess
import sys
from pathlib import Path

import pytest
import yaml
from conftest import needs_kicad

HW = Path(__file__).resolve().parents[1]
FAB = HW / "fab"
BOARD = yaml.safe_load((HW / "board.yaml").read_text())["board"]


@needs_kicad
def test_fab_outputs_are_current():
    r = subprocess.run([sys.executable, str(HW / "tools" / "fab.py"), "--check"],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr


def test_gerbers_parse_and_board_size():
    gerbonara = pytest.importorskip("gerbonara")
    stack = gerbonara.LayerStack.open(FAB / "gerbers")
    outline = stack.outline
    assert outline is not None
    (x0, y0), (x1, y1) = outline.bounding_box(unit=gerbonara.utils.MM)
    assert abs((x1 - x0) - BOARD["width"]) < 0.2
    assert abs((y1 - y0) - BOARD["height"]) < 0.2


def test_drills_within_jlcpcb_limits():
    gerbonara = pytest.importorskip("gerbonara")
    stack = gerbonara.LayerStack.open(FAB / "gerbers")
    sizes = set()
    for layer in stack.drill_layers:
        for obj in layer.objects:
            d = getattr(obj, "tool", None) or getattr(obj, "aperture", None)
            dia = getattr(d, "diameter", None)
            if dia:
                sizes.add(round(gerbonara.utils.MM.convert_from(layer.unit, dia), 3))
    assert sizes, "no drills found"
    assert min(sizes) >= 0.3 - 1e-6, f"drill below 0.3 mm: {sorted(sizes)}"
    assert max(sizes) <= 6.3


def test_bom_and_cpl_consistent():
    bom = list(csv.DictReader((FAB / "irl2fa-bom-jlcpcb.csv").open()))
    cpl = list(csv.DictReader((FAB / "irl2fa-cpl-jlcpcb.csv").open()))
    refs = {r.strip() for row in bom for r in row["Designator"].split(",")}
    placed = {row["Designator"] for row in cpl}
    assert refs == placed, f"BOM-only: {refs - placed}, CPL-only: {placed - refs}"
    for row in bom:
        assert row["LCSC Part #"].startswith("C"), row
    for row in cpl:
        assert 0 <= float(row["Mid X"][:-2]) <= BOARD["width"] + 0.01
        assert 0 <= float(row["Mid Y"][:-2]) <= BOARD["height"] + 0.01
