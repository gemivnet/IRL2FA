"""Runs every SPICE bench in hardware/spice and checks its results against
hardware/spice/checks.yaml. Needs `ngspice` on PATH (the KiCad 9 container
has it).  SPDX-License-Identifier: CERN-OHL-S-2.0"""
import re
import shutil
import subprocess
from pathlib import Path

import pytest
import yaml

SPICE = Path(__file__).resolve().parents[1] / "spice"
CHECKS = yaml.safe_load((SPICE / "checks.yaml").read_text())


def run_bench(name):
    out = subprocess.run(["ngspice", "-b", name], cwd=SPICE, capture_output=True, text=True,
                         timeout=600).stdout
    results = {}
    for m in re.finditer(r"^RESULT (\w+) ([-+0-9.eE]+)\s*$", out, re.M):
        results[m.group(1)] = float(m.group(2))
    return results, out


@pytest.mark.skipif(shutil.which("ngspice") is None, reason="ngspice not installed")
@pytest.mark.parametrize("bench", sorted(CHECKS))
def test_bench(bench):
    results, out = run_bench(bench)
    for key, lim in CHECKS[bench].items():
        assert key in results, f"{bench}: no RESULT {key}\n{out[-2000:]}"
        v = results[key]
        if "min" in lim:
            assert v >= lim["min"], f"{bench} {key} = {v} {lim['unit']} < {lim['min']} ({lim['why']})"
        if "max" in lim:
            assert v <= lim["max"], f"{bench} {key} = {v} {lim['unit']} > {lim['max']} ({lim['why']})"


def test_every_bench_has_checks():
    benches = {p.name for p in SPICE.glob("*.cir")}
    assert benches == set(CHECKS), "add limits for new benches to checks.yaml"
