"""Battery life must meet the requirement in hardware/power/model.json in the
worst case for the 'normal' profile, and the generated doc must be current.
SPDX-License-Identifier: CERN-OHL-S-2.0"""
import sys
from pathlib import Path

HW = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HW / "power"))
import power  # noqa: E402


def test_battery_life_requirement():
    req = power.MODEL["requirement_years"]
    assert power.life_years("normal", "worst") >= req
    assert power.life_years("heavy", "typ") >= req


def test_doc_is_current():
    doc = (HW.parent / "docs" / "power-calculations.md").read_text()
    assert doc == power.markdown(), "run: python3 hardware/power/power.py --markdown > docs/power-calculations.md"
