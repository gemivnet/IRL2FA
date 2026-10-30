"""Electrical design checks against manufacturers' datasheets.

Every expected value comes from hardware/tests/electrical_ref.yaml, which is
transcribed from primary sources (datasheet, page, figure) - not from our own
design files. The checks read the schematic netlist (conftest.netlist),
hardware/pins.yaml and, where geometry matters, the routed board.

Findings from docs/electrical-review.md that the current design does not meet
yet are encoded as xfail(strict=True) tests named after the finding ID, so CI
stays green until the fix lands and then forces the marker to be removed.

SPDX-License-Identifier: CERN-OHL-S-2.0
"""
import re
from pathlib import Path

import pytest
import yaml
from conftest import needs_kicad

HW = Path(__file__).resolve().parents[1]
REF = yaml.safe_load((Path(__file__).with_name("electrical_ref.yaml")).read_text())
PINS = yaml.safe_load((HW / "pins.yaml").read_text())
BOARD = yaml.safe_load((HW / "board.yaml").read_text())


# --------------------------------------------------------------------------- helpers

def net(nl, ref, pin):
    n = nl["pins"].get((ref, str(pin)))
    return None if n is None or n.startswith("unconnected-") else n.lstrip("/")


def pin_by_name(nl, ref, name):
    """Net on the pin of `ref` whose symbol pin name is `name`."""
    lib = nl["comps"][ref]["libpart"]
    for num, nm in nl["libparts"][lib].items():
        if nm == name:
            return net(nl, ref, num)
    raise KeyError(f"{ref} has no pin named {name}")


def two_pin_parts_between(nl, prefix, a, b):
    return [r for r in nl["comps"] if r.startswith(prefix) and not r.startswith("#")
            and {net(nl, r, 1), net(nl, r, 2)} == {a, b}]


def value_ohms(v):
    m = re.fullmatch(r"([0-9.]+)([kM]?)", v)
    return float(m.group(1)) * {"": 1, "k": 1e3, "M": 1e6}[m.group(2)]


def mpn(nl, ref):
    return nl["comps"][ref]["fields"].get("MPN", "")


# --------------------------------------------------------------------------- symbols

@needs_kicad
@pytest.mark.parametrize("lib", sorted(REF["symbol_pins"]))
def test_symbol_pins_match_datasheet(netlist, lib):
    key = tuple(lib.split(":", 1))
    if key not in netlist["libparts"]:
        pytest.skip(f"{lib} not used")
    got = {k: v for k, v in netlist["libparts"][key].items() if k in REF["symbol_pins"][lib]["pins"]}
    assert got == REF["symbol_pins"][lib]["pins"], REF["symbol_pins"][lib]["source"]


# --------------------------------------------------------------------------- MCU

def test_pins_yaml_pinout_matches_datasheet():
    ds = REF["mcu"]["pinout"]
    bad = [f"pin {p['pin']}: pins.yaml {p['name']}, datasheet {ds[p['pin']]}"
           for p in PINS["pins"] if ds[p["pin"]] != p["name"]]
    assert not bad, "\n".join(bad) + "\n" + REF["mcu"]["pinout_source"]


def test_alternate_functions_exist_on_their_pins():
    """Each AF pin in pins.yaml carries the function its net needs, at that AF number."""
    errors = []
    for p in PINS["pins"]:
        if p.get("mode") != "af":
            continue
        want = REF["mcu"]["net_function"].get(p["net"])
        assert want, f"{p['net']}: add the function it needs to electrical_ref.yaml"
        row = REF["mcu"]["af"].get(p["name"])
        if row is None or row.get(p["af"]) != want:
            errors.append(f"{p['name']} AF{p['af']} is {row.get(p['af']) if row else '?'}, "
                          f"{p['net']} needs {want}")
    assert not errors, "\n".join(errors) + "\n" + REF["mcu"]["af_source"]


def test_peripheral_signals_share_one_instance():
    """SCK/MOSI on one SPI, TX/RX on one USART, SCL/SDA on one I2C."""
    fn = {p["net"]: REF["mcu"]["af"][p["name"]][p["af"]] for p in PINS["pins"]
          if p.get("mode") == "af" and p["net"] not in ("SWDIO", "SWCLK")}
    inst = lambda n: fn[n].split("_")[0]  # noqa: E731
    assert inst("EPD_SCK") == inst("EPD_SDI")
    assert inst("IR_TX") == inst("IR_RX")
    assert inst("I2C_SCL") == inst("I2C_SDA")


def test_exti_lines_do_not_collide():
    """EXTI line n is shared by Pxn of every port (EXTICR picks one): wake pins need distinct n."""
    lines = {}
    for p in PINS["pins"]:
        if p.get("exti"):
            n = int(re.fullmatch(r"P[A-F](\d+)", p["name"]).group(1))
            assert n not in lines, f"{p['name']} and {lines[n]} both need EXTI{n}"
            lines[n] = p["name"]
    # every wake source of the Stop 2 design is on an EXTI line
    wake = {p["net"] for p in PINS["pins"] if p.get("exti")}
    assert {"JOY_CENTER", "JOY_A", "JOY_B", "JOY_C", "JOY_D", "RTC_INT"} <= wake


def test_lse_pins_not_used_as_outputs():
    """PC14/PC15 sit on the backup domain switch: no current sourcing (DS14548 3.6)."""
    for p in PINS["pins"]:
        if p["name"] in ("PC13", "PC14", "PC15"):
            assert p.get("mode") not in ("output", "af"), p


@needs_kicad
def test_mcu_supply_and_decoupling(netlist):
    """DS14548 Figure 13 p.42: n x 100 nF + 4.7 uF on VDD, 10 nF + 1 uF on VDDA."""
    for pin in ("1", "5", "17"):
        assert net(netlist, "U1", pin) == "VBAT"
    for pin in ("16", "32", "33"):
        assert net(netlist, "U1", pin) == "GND"
    vals = sorted(netlist["comps"][r]["value"] for r in two_pin_parts_between(netlist, "C", "VBAT", "GND"))
    for need in ("100n", "100n", "4.7u", "1u", "10n"):
        assert need in vals, f"missing {need} on VBAT: {vals}"
        vals.remove(need)
    assert two_pin_parts_between(netlist, "C", "NRST", "GND"), "NRST needs its 100 nF"
    assert two_pin_parts_between(netlist, "R", "BOOT0", "GND"), "BOOT0 (PF3) pull-down"


# --------------------------------------------------------------------------- e-paper FPC

@needs_kicad
def test_fpc_pins_match_panel_reference_circuit(netlist):
    gnd_caps = {}
    for r, c in netlist["comps"].items():
        if r.startswith("C"):
            a, b = net(netlist, r, 1), net(netlist, r, 2)
            if "GND" in (a, b):
                gnd_caps.setdefault(b if a == "GND" else a, []).append(r)
    errors = []
    supply = set()
    for pin, kind in REF["panel"]["pins"].items():
        n = net(netlist, "J1", pin)
        if kind == "nc" and n is not None:
            errors.append(f"J1.{pin} must be unconnected, is {n}")
        elif kind == "gnd" and n != "GND":
            errors.append(f"J1.{pin} must be GND, is {n}")
        elif kind == "cap":
            if not n or n == "GND" or not gnd_caps.get(n):
                errors.append(f"J1.{pin} ({n}) needs its own capacitor to GND")
        elif kind == "supply":
            supply.add(n)
        elif kind.startswith("net:") and n != kind[4:]:
            errors.append(f"J1.{pin} must be {kind[4:]}, is {n}")
    assert len(supply) == 1 and None not in supply, f"VDDIO and VCI must share one supply: {supply}"
    assert not errors, "\n".join(errors) + "\n" + REF["panel"]["source"]


@needs_kicad
def test_fpc_connector_accepts_either_contact_face(netlist):
    assert mpn(netlist, "J1").startswith(REF["fpc_connector"]["dual_contact_mpn_prefix"]), \
        REF["fpc_connector"]["source"]


def test_fpc_pin1_lands_on_connector_pin1():
    """Panel pin k must sit level with J1 pad k after the 180-degree fold.

    The fold is about an axis along the FPC width, so every FPC pin keeps its
    board y. From the panel drawing (p.7, front view, FPC exiting left) pin 1 is
    the top one, glass top + 8.15 + 0.50 mm."""
    pcbnew = pytest.importorskip("pcbnew")
    b = pcbnew.LoadBoard(str(HW / "irl2fa.kicad_pcb"))
    fp = b.FindFootprintByReference("J1")
    pad_y = {p.GetNumber(): pcbnew.ToMM(p.GetPosition().y) - 100.0 for p in fp.Pads()}
    glass = BOARD["display"]["glass"]
    assert BOARD["display"]["fpc_exit"] == "left"
    p1 = glass["y"] + REF["panel"]["fpc_edge_from_glass_top"] + REF["panel"]["pin1_from_fpc_edge"]
    for k in range(1, 25):
        want = p1 + (k - 1) * REF["panel"]["pitch"]
        assert abs(pad_y[str(k)] - want) <= REF["panel"]["fpc_edge_tol"], \
            f"J1 pad {k} at y={pad_y[str(k)]:.2f}, panel pin {k} at y={want:.2f}"


# --------------------------------------------------------------------------- FETs, diodes

@needs_kicad
def test_reverse_battery_pfet(netlist):
    """Q1: drain to the cell, source to the load, gate to GND. The body diode
    (anode = drain, cathode = source on a P-FET) then conducts cell -> load at
    insertion and the channel shorts it once Vgs = -VBAT; a reversed cell sees
    Vgs = 0 and a reverse-biased body diode."""
    assert pin_by_name(netlist, "Q1", "D") == "VBAT_RAW"
    assert pin_by_name(netlist, "Q1", "S") == "VBAT"
    assert pin_by_name(netlist, "Q1", "G") == "GND"
    assert pin_by_name(netlist, "BT1", "+") == "VBAT_RAW"
    assert pin_by_name(netlist, "BT1", "-") == "GND"


@needs_kicad
def test_display_load_switch(netlist):
    """Q2 high-side P-FET: source on VBAT (body diode cannot feed the panel from VBAT
    when off), drain on VEPD, gate pulled to the source so the panel is off in reset."""
    assert pin_by_name(netlist, "Q2", "S") == "VBAT"
    assert pin_by_name(netlist, "Q2", "D") == "VEPD"
    g = pin_by_name(netlist, "Q2", "G")
    assert two_pin_parts_between(netlist, "R", g, "VBAT")


@needs_kicad
def test_booster_switch_and_sense(netlist):
    assert pin_by_name(netlist, "Q3", "G") == "EPD_GDR"
    assert pin_by_name(netlist, "Q3", "S") == "EPD_RESE"
    assert pin_by_name(netlist, "Q3", "D") == "EPD_SW"
    assert two_pin_parts_between(netlist, "R", "EPD_RESE", "GND")
    assert two_pin_parts_between(netlist, "R", "EPD_GDR", "GND")
    assert two_pin_parts_between(netlist, "L", "VEPD", "EPD_SW")


@needs_kicad
def test_booster_diodes_and_charge_pump(netlist):
    """GDEY0213B74 p.29: D3 SW -> PREVGH; C3 SW..node; D2 node -> GND; D1 PREVGL -> node."""
    def ka(ref):
        return pin_by_name(netlist, ref, "K"), pin_by_name(netlist, ref, "A")
    assert ka("D3") == ("EPD_PREVGH", "EPD_SW")
    assert ka("D2") == ("GND", "EPD_NEG")
    assert ka("D1") == ("EPD_NEG", "EPD_PREVGL")
    assert two_pin_parts_between(netlist, "C", "EPD_SW", "EPD_NEG")


@needs_kicad
def test_booster_values_match_reference(netlist):
    for ref, want in REF["booster"].items():
        if ref == "source":
            continue
        c = netlist["comps"][ref]
        if "value" in want:
            assert c["value"] == want["value"], f"{ref}: {c['value']} != {want['value']} ({REF['booster']['source']})"
        if "mpn" in want:
            assert mpn(netlist, ref).startswith(want["mpn"]), f"{ref}: {mpn(netlist, ref)}"


@needs_kicad
def test_ir_emitter_path(netlist):
    """VBAT -> R8 -> D4 anode, D4 cathode -> Q4 drain, Q4 source GND, gate on IR_TX
    with a pull-down (LED off while the MCU is in reset)."""
    a, k = pin_by_name(netlist, "D4", "A"), pin_by_name(netlist, "D4", "K")
    assert two_pin_parts_between(netlist, "R", "VBAT", a)
    assert pin_by_name(netlist, "Q4", "D") == k
    assert pin_by_name(netlist, "Q4", "S") == "GND"
    assert pin_by_name(netlist, "Q4", "G") == "IR_TX"
    assert two_pin_parts_between(netlist, "R", "IR_TX", "GND")


@needs_kicad
def test_ir_detector_emitter_follower(netlist):
    """Collector on the switched supply pin, emitter on the UART input with its load to GND:
    light pulls IR_RX high, so the USART runs with RX inverted (idle = dark = low)."""
    assert pin_by_name(netlist, "Q5", "C") == "IR_RX_PWR"
    assert pin_by_name(netlist, "Q5", "E") == "IR_RX"
    assert two_pin_parts_between(netlist, "R", "IR_RX", "GND")
    rx_pwr = next(p for p in PINS["pins"] if p["net"] == "IR_RX_PWR")
    assert rx_pwr["mode"] == "output"


@needs_kicad
def test_ir_detector_has_daylight_filter(netlist):
    """IR-1: ambient light above ~75 uA of photocurrent would hold IR_RX above VIL;
    the detector must block daylight."""
    assert mpn(netlist, "Q5") in REF["ir_detector"]["daylight_filter_mpns"]


# --------------------------------------------------------------------------- RTC

@needs_kicad
def test_rtc_unused_pins(netlist):
    vb = pin_by_name(netlist, "U2", "VBACKUP")
    rs = two_pin_parts_between(netlist, "R", vb, "GND")
    assert rs and netlist["comps"][rs[0]]["value"] == REF["rtc"]["vbackup_resistor"], REF["rtc"]["source"]
    assert pin_by_name(netlist, "U2", "EVI") is not None, "EVI must not float"
    assert pin_by_name(netlist, "U2", "CLKOUT") not in ("GND", "VBAT"), "CLKOUT is push-pull"
    assert pin_by_name(netlist, "U2", "VDD") == "VBAT" and pin_by_name(netlist, "U2", "VSS") == "GND"
    assert two_pin_parts_between(netlist, "C", "VBAT", "GND")
    # I2C pull-ups go to the always-on rail the RTC is on, so the bus idles high in Stop 2
    for line in ("SCL", "SDA"):
        assert two_pin_parts_between(netlist, "R", pin_by_name(netlist, "U2", line), "VBAT")


# --------------------------------------------------------------------------- capacitors

def _samsung(m):
    s = REF["samsung_mlcc"]
    mm = re.fullmatch(r"CL\d\d([A-Z])(\d{2}[0-9R])([A-Z])([A-Z]).*", m)
    assert mm, f"not a Samsung CL part number: {m}"
    return s["dielectric"][mm.group(1)], s["voltage"][mm.group(4)]


@needs_kicad
def test_capacitor_voltage_ratings(netlist):
    """Rated voltage (decoded from the Samsung part number) >= the highest voltage
    across the part; panel/booster parts >= 25 V as the GDEY0213B74 reference asks."""
    vmax = {k: v["v"] for k, v in REF["net_vmax"].items()}
    pair = {tuple(k.split("|")): v["v"] for k, v in REF["pair_vmax"].items()}
    errors = []
    for r, c in netlist["comps"].items():
        if not r.startswith("C") or r.startswith("#"):
            continue
        a, b = net(netlist, r, 1), net(netlist, r, 2)
        diel, rated = _samsung(mpn(netlist, r))
        assert diel in ("X5R", "X7R", "C0G"), f"{r}: {diel}"
        if "GND" in (a, b):
            n = b if a == "GND" else a
            assert n in vmax, f"{r}: add {n} to net_vmax in electrical_ref.yaml"
            work = vmax[n]
        else:
            work = pair.get((a, b), pair.get((b, a)))
            assert work is not None, f"{r}: add {a}|{b} to pair_vmax"
        if rated < work:
            errors.append(f"{r} {c['value']} rated {rated} V sees {work} V")
        if any(x.startswith("EPD_") for x in (a, b) if x) and rated < REF["display_cap_min_v"]["v"]:
            errors.append(f"{r} on {a}/{b} rated {rated} V < {REF['display_cap_min_v']['v']} V "
                          f"({REF['display_cap_min_v']['source']})")
    assert not errors, "\n".join(errors)


# --------------------------------------------------------------------------- IR receiver levels

@needs_kicad
def test_ir_rx_dark_current_reads_low(netlist):
    """Dark current x load must stay far below VIL: ICEO 100 nA max (VEMT2023SLX01 doc 84166 p.2)."""
    rload = value_ohms(netlist["comps"][two_pin_parts_between(netlist, "R", "IR_RX", "GND")[0]]["value"])
    assert 100e-9 * rload < 0.1 * REF["mcu"]["vil_frac"] * 2.2
