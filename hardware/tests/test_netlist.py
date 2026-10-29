"""Connectivity checks of the schematic against hardware/pins.yaml and basic
design rules (decoupling, pull-ups, no floating nets, sourcing data).
SPDX-License-Identifier: CERN-OHL-S-2.0"""
from pathlib import Path

import yaml
from conftest import needs_kicad

HW = Path(__file__).resolve().parents[1]
PINS = yaml.safe_load((HW / "pins.yaml").read_text())
MCU = "U1"

pytestmark = needs_kicad


def net_of(nl, ref, pin):
    n = nl["pins"].get((ref, str(pin)))
    return None if n is None or n.startswith("unconnected-") else n.lstrip("/")


def test_mcu_pins_match_pins_yaml(netlist):
    errors = []
    for p in PINS["pins"]:
        got = net_of(netlist, MCU, p["pin"])
        if got != p["net"]:
            errors.append(f"U1 pin {p['pin']} ({p['name']}): schematic {got!r}, pins.yaml {p['net']!r}")
    assert not errors, "\n".join(errors)


def test_mcu_part(netlist):
    assert netlist["comps"][MCU]["value"] == PINS["mcu"]


def test_joystick_wiring(netlist):
    # Alps SKRH pads: 1=A 2=Center 3=C 4=B 5=Common 6=D
    pad = {"JOY_A": "1", "JOY_CENTER": "2", "JOY_C": "3", "JOY_B": "4", "JOY_D": "6"}
    for net, p in pad.items():
        assert net_of(netlist, "SW1", p) == net
        assert net in PINS["joystick"]
    assert net_of(netlist, "SW1", "5") == "GND"


def caps_between(nl, a, b):
    return [r for r, c in nl["comps"].items() if r.startswith("C")
            and {net_of(nl, r, 1), net_of(nl, r, 2)} == {a, b}]


def test_decoupling(netlist):
    assert len(caps_between(netlist, "VBAT", "GND")) >= 5   # MCU x3+, bulk x2, RTC
    assert len(caps_between(netlist, "VEPD", "GND")) >= 2   # panel input + VCI


def test_i2c_pullups(netlist):
    for line in ("I2C_SCL", "I2C_SDA"):
        pulls = [r for r in netlist["comps"] if r.startswith("R")
                 and {net_of(netlist, r, 1), net_of(netlist, r, 2)} == {line, "VBAT"}]
        assert pulls, f"{line} has no pull-up to VBAT"


def test_no_single_pin_nets(netlist):
    lonely = [n for n, nodes in netlist["nets"].items()
              if len(nodes) == 1 and not n.startswith("unconnected-")]
    assert not lonely, f"nets with a single connection: {lonely}"


def test_every_assembled_part_has_lcsc(netlist):
    missing = [r for r, c in netlist["comps"].items()
               if not r.startswith(("TP", "H", "J3", "#")) and not c["fields"].get("LCSC")]
    assert not missing, f"no LCSC part number: {missing}"


def test_power_switch_defaults_off(netlist):
    # e-paper P-FET gate pulled up to VBAT so the panel is off during reset
    pulls = [r for r in netlist["comps"] if r.startswith("R")
             and {net_of(netlist, r, 1), net_of(netlist, r, 2)} == {"EPD_PWR_N", "VBAT"}]
    assert pulls
