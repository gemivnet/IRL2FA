"""Shared fixtures: the KiCad netlist of the schematic (needs kicad-cli).
SPDX-License-Identifier: CERN-OHL-S-2.0"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import pytest

HW = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HW / "tools"))
from sexpr import find, find_all, parse  # noqa: E402

needs_kicad = pytest.mark.skipif(shutil.which("kicad-cli") is None, reason="kicad-cli not installed")


@pytest.fixture(scope="session")
def netlist():
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "irl2fa.net"
        subprocess.run(["kicad-cli", "sch", "export", "netlist", "--format", "kicadsexpr", "-o",
                        str(out), str(HW / "irl2fa.kicad_sch")], check=True, capture_output=True)
        tree = parse(out.read_text())
    comps = {}
    for c in find_all(find(tree, "components"), "comp"):
        fields = {}
        for f in find_all(find(c, "fields") or [], "field"):
            fields[find(f, "name")[1]] = next((x for x in f[2:] if not isinstance(x, list)), "")
        for pr in find_all(c, "property"):
            fields.setdefault(find(pr, "name")[1], find(pr, "value")[1] if find(pr, "value") else "")
        src = find(c, "libsource")
        comps[find(c, "ref")[1]] = {
            "value": find(c, "value")[1],
            "footprint": find(c, "footprint")[1] if find(c, "footprint") else "",
            "fields": fields,
            "libpart": (find(src, "lib")[1], find(src, "part")[1]) if src else None,
        }
    # symbol pins (number -> name) per library part
    libparts = {}
    for lp in find_all(find(tree, "libparts") or [], "libpart"):
        pins_ = find(lp, "pins")
        libparts[(find(lp, "lib")[1], find(lp, "part")[1])] = {
            find(p, "num")[1]: find(p, "name")[1] for p in find_all(pins_, "pin")} if pins_ else {}
    pins, nets = {}, {}
    for n in find_all(find(tree, "nets"), "net"):
        name = find(n, "name")[1]
        nodes = [(find(x, "ref")[1], find(x, "pin")[1]) for x in find_all(n, "node")]
        nets[name] = nodes
        for node in nodes:
            pins[node] = name
    return {"comps": comps, "pins": pins, "nets": nets, "libparts": libparts}
