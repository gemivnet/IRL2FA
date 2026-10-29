"""Battery life must meet the requirement in hardware/power/model.json in the
worst case for the 'normal' profile, using the emulator-measured profile in
hardware/power/measured.json, and everything generated from them (the doc,
its timeline chart and the simulator's model) must be current.
SPDX-License-Identifier: CERN-OHL-S-2.0"""
import sys
from pathlib import Path

HW = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HW / "power"))
import power  # noqa: E402

REGEN = "run: python3 hardware/power/power.py --update"


def test_battery_life_requirement():
    # 20 uses a day with worst-case currents (power.REQUIRED).
    assert power.requirement_problems() == []


def test_measured_profile_is_sane():
    m = power.load_measured()
    use, pair = m["sessions"]["use"], m["sessions"]["pairing"]
    for s in (use, pair):
        assert s["stop2_entries"] >= 1, "the session must end in Stop 2"
        assert s["full_refreshes"] >= 2, "wake and sleep are full refreshes"
        assert 0 < s["mcu_run_s"] < s["awake_s"]
    assert use["ir_on_s"] == 0, "IR must stay off outside pairing"
    assert pair["ir_tx_bytes"] > 0 and pair["ir_on_s"] > 0


def test_doc_is_current():
    assert power.DOC.read_text() == power.markdown(), REGEN


def test_timeline_chart_is_current():
    assert power.SVG.read_text() == power.svg(), REGEN


def test_sim_model_is_current():
    assert power.SIM_JS.read_text() == power.sim_js(), REGEN
