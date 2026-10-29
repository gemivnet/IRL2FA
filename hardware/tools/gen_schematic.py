#!/usr/bin/env python3
"""Bootstrap hardware/irl2fa.kicad_sch from tools/design.py.

Every pin gets a short wire stub and a net label (or a no-connect flag), and
parts are grouped into titled blocks. Symbols are embedded from the KiCad
stock libraries (CC-BY-SA-4.0 with the KiCad libraries exception) and from
hardware/lib/irl2fa.kicad_sym.

Run inside the KiCad 9 container so the stock libraries are available:
    docker run --rm -v "$PWD":/w -w /w kicad/kicad:9.0 \
        python3 hardware/tools/gen_schematic.py

SPDX-License-Identifier: CERN-OHL-S-2.0
"""
import copy
import math
import os
import sys
import uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sexpr import Q, dump, find, find_all, parse  # noqa: E402

import design  # noqa: E402

HW = Path(__file__).resolve().parents[1]
STOCK = Path(os.environ.get("KICAD_SYMBOL_DIR", "/usr/share/kicad/symbols"))
LOCAL = {"irl2fa": HW / "lib" / "irl2fa.kicad_sym"}
OUT = HW / "irl2fa.kicad_sch"
PROJECT = "irl2fa"
GRID = 2.54
STUB = 2.54

NS = uuid.UUID("6f1c7f3e-6d4b-4d61-9a0e-2a1b9b3c0f5a")


def uid(*parts):
    """Deterministic UUIDs keep regenerated files diff-friendly."""
    return Q(str(uuid.uuid5(NS, "/".join(str(p) for p in parts))))


ROOT = uid("root")

_lib_cache = {}


def load_lib(lib):
    if lib not in _lib_cache:
        path = LOCAL.get(lib, STOCK / f"{lib}.kicad_sym")
        tree = parse(path.read_text())
        _lib_cache[lib] = {s[1]: s for s in find_all(tree, "symbol")}
    return _lib_cache[lib]


def flatten(lib, name):
    """Returns the symbol with `extends` resolved, renamed to lib:name."""
    syms = load_lib(lib)
    sym = copy.deepcopy(syms[name])
    ext = find(sym, "extends")
    if ext:
        parent = flatten(lib, ext[1])
        pname = ext[1]
        body = [x for x in parent if not (isinstance(x, list) and x[0] == "property")]
        props = find_all(sym, "property")
        merged = [Q(f"{lib}:{name}")]
        for x in body[2:]:
            if isinstance(x, list) and x[0] == "symbol":
                x = copy.deepcopy(x)
                x[1] = Q(x[1].replace(pname, name, 1))
            merged.append(x)
        # properties go right after the flags, as KiCad writes them
        out = ["symbol", merged[0]]
        flags = [x for x in merged[1:] if not (isinstance(x, list) and x[0] == "symbol")]
        units = [x for x in merged[1:] if isinstance(x, list) and x[0] == "symbol"]
        return out + flags + props + units
    sym[1] = Q(f"{lib}:{name}")
    return sym


def pins_of(sym):
    """{number: (x, y, angle, name, type)} in symbol (y-up) coordinates."""
    out = {}

    def walk(node):
        for x in node:
            if isinstance(x, list) and x:
                if x[0] == "pin":
                    at = find(x, "at")
                    num = find(x, "number")[1]
                    name = find(x, "name")[1]
                    out[str(num)] = (float(at[1]), float(at[2]), float(at[3]) if len(at) > 3 else 0.0,
                                     name, x[1])
                else:
                    walk(x)

    walk(sym)
    return out


def snap(v):
    return round(v / GRID) * GRID


def fmt(v):
    return f"{v:.4f}".rstrip("0").rstrip(".")


def font(size=1.27, bold=False):
    f = ["font", ["size", fmt(size), fmt(size)]]
    if bold:
        f += [["thickness", fmt(size / 5)], ["bold", "yes"]]
    return f


def prop(name, value, x, y, hide=False, justify=None, angle=0):
    eff = ["effects", font()]
    if justify:
        eff.append(["justify", *justify])
    if hide:
        eff.append(["hide", "yes"])
    return ["property", Q(name), Q(value or ""), ["at", fmt(x), fmt(y), str(angle)], eff]


class Sheet:
    def __init__(self):
        self.items = []
        self.lib_symbols = {}

    def add_symbol_def(self, lib_id):
        if lib_id not in self.lib_symbols:
            lib, name = lib_id.split(":")
            self.lib_symbols[lib_id] = flatten(lib, name)
        return self.lib_symbols[lib_id]

    def wire(self, x1, y1, x2, y2, key):
        self.items.append(["wire", ["pts", ["xy", fmt(x1), fmt(y1)], ["xy", fmt(x2), fmt(y2)]],
                           ["stroke", ["width", "0"], ["type", "default"]], ["uuid", uid("w", key)]])

    def label(self, net, x, y, angle, key):
        just = {0: ["left", "bottom"], 90: ["left", "bottom"], 180: ["right", "bottom"],
                270: ["right", "bottom"]}[angle]
        self.items.append(["label", Q(net), ["at", fmt(x), fmt(y), str(angle)],
                           ["fields_autoplaced", "yes"],
                           ["effects", font(), ["justify", *just]], ["uuid", uid("l", key)]])

    def no_connect(self, x, y, key):
        self.items.append(["no_connect", ["at", fmt(x), fmt(y)], ["uuid", uid("nc", key)]])

    def text(self, s, x, y, size=2.0, key=None):
        self.items.append(["text", Q(s), ["exclude_from_sim", "no"],
                           ["at", fmt(x), fmt(y), "0"],
                           ["effects", font(size, bold=True), ["justify", "left", "bottom"]],
                           ["uuid", uid("t", key or s)]])

    def place(self, p, x, y):
        lib_id = p["lib"]
        sym = self.add_symbol_def(lib_id)
        pins = pins_of(sym)
        ref = p["ref"]
        is_power = ref.startswith("#")
        node = ["symbol", ["lib_id", Q(lib_id)], ["at", fmt(x), fmt(y), "0"], ["unit", "1"],
                ["exclude_from_sim", "no"], ["in_bom", "yes" if p.get("bom", True) else "no"],
                ["on_board", "no" if is_power else "yes"], ["dnp", "no"],
                ["fields_autoplaced", "yes"], ["uuid", uid("s", ref)]]
        top = min((-py for (_, py, *_r) in pins.values()), default=0)
        right_free = not any(a == 180 for (_x, _y, a, *_r) in pins.values())
        if right_free:
            # fields beside the body, clear of the vertical pin labels
            rx = x + max((px for (px, *_r) in pins.values()), default=0) + 2.0
            if rx < x + 2.0:
                rx = x + 2.0
            node.append(prop("Reference", ref, rx, y - 1.27, hide=is_power, justify=["left"]))
            node.append(prop("Value", p["value"], rx, y + 1.27, hide=is_power, justify=["left"]))
        else:
            node.append(prop("Reference", ref, x, y + top - 3.0, hide=is_power))
            node.append(prop("Value", p["value"], x, y + top - 1.0, hide=is_power))
        node.append(prop("Footprint", p.get("footprint", ""), x, y, hide=True))
        node.append(prop("Datasheet", p.get("datasheet", ""), x, y, hide=True))
        node.append(prop("Description", p.get("desc", ""), x, y, hide=True))
        for k, field in (("lcsc", "LCSC"), ("mpn", "MPN"), ("mfr", "Manufacturer")):
            if p.get(k):
                node.append(prop(field, p[k], x, y, hide=True))
        for num in sorted(pins, key=lambda s: (len(s), s)):
            node.append(["pin", Q(num), ["uuid", uid("p", ref, num)]])
        node.append(["instances", ["project", Q(PROJECT),
                                   ["path", Q(f"/{ROOT}"), ["reference", Q(ref)], ["unit", "1"]]]])
        self.items.append(node)

        # wire stub + label for each pin
        for num, (px, py, pa, _name, _ptype) in pins.items():
            cx, cy = x + px, y - py
            net = p["pins"].get(num, None) if p["pins"] else None
            # direction from the pin end away from the body
            ox, oy = -math.cos(math.radians(pa)), math.sin(math.radians(pa))
            ox, oy = round(ox), round(oy)
            if net is None:
                self.no_connect(cx, cy, f"{ref}.{num}")
                continue
            ex, ey = cx + ox * STUB, cy + oy * STUB
            self.wire(cx, cy, ex, ey, f"{ref}.{num}")
            angle = {(1, 0): 0, (-1, 0): 180, (0, -1): 90, (0, 1): 270}[(ox, oy)]
            self.label(net, ex, ey, angle, f"{ref}.{num}")


def extents(sheet, p):
    sym = sheet.add_symbol_def(p["lib"])
    pins = pins_of(sym)
    xs = [px for (px, *_r) in pins.values()] or [0]
    ys = [-py for (_, py, *_r) in pins.values()] or [0]
    longest = max((len(n) for n in p["pins"].values() if n), default=4)
    lab = STUB + 1.3 * longest + 2
    left = min(xs) - (lab if any(pins[k][2] == 0 for k in pins) else 2)
    right = max(xs) + (lab if any(pins[k][2] == 180 for k in pins) else 2 + 1.3 * max(
        len(p["ref"]), len(p["value"])))
    top = min(ys) - (lab if any(pins[k][2] == 270 for k in pins) else 5)
    bottom = max(ys) + (lab if any(pins[k][2] == 90 for k in pins) else 3)
    return left, right, top, bottom


# Block regions on an A3 sheet (x0, y0, width)
REGIONS = {
    "mcu": (20, 25, 175),
    "power": (205, 25, 195),
    "rtc": (205, 95, 195),
    "input": (205, 150, 90),
    "ir": (300, 150, 100),
    "display": (20, 160, 175),
    "mech": (205, 230, 195),
}


def main():
    sh = Sheet()
    parts = [p for p in design.PARTS if p["lib"]]
    if os.environ.get("SKIP_LOCAL"):  # pipeline testing before hardware/lib exists
        parts = [p for p in parts if not p["lib"].startswith("irl2fa:")]
    flags = [dict(ref=f"#FLG{i + 1:02d}", lib="power:PWR_FLAG", value="PWR_FLAG",
                  pins={"1": net}, block="power", bom=False)
             for i, net in enumerate(design.POWER_FLAGS)]
    by_block = {}
    for p in parts + flags:
        by_block.setdefault(p["block"], []).append(p)

    for block, items in by_block.items():
        x0, y0, width = REGIONS[block]
        sh.text(design.BLOCK_TITLES[block], x0, y0 - 6, 2.0, key=block)
        x, y, row_h = x0, y0, 0.0
        for p in items:
            left, right, top, bottom = extents(sh, p)
            w = right - left
            if x + w > x0 + width and x > x0:
                x = x0
                y += row_h + 6
                row_h = 0
            sx, sy = snap(x - left), snap(y - top)
            sh.place(p, sx, sy)
            x += w + 4
            row_h = max(row_h, bottom - top)

    tree = ["kicad_sch", ["version", "20250114"], ["generator", Q("irl2fa_gen_schematic")],
            ["generator_version", Q("9.0")], ["uuid", ROOT], ["paper", Q("A3")],
            ["title_block", ["title", Q("IRL2FA - air-gapped person-to-person 2FA")],
             ["date", Q("2026-10-29")], ["rev", Q("0.1")], ["company", Q("irl2fa.com")],
             ["comment", "1", Q("github.com/gemivnet/IRL2FA")],
             ["comment", "2", Q("Licence: CERN-OHL-S-2.0")]],
            ["lib_symbols", *sh.lib_symbols.values()],
            *sh.items,
            ["sheet_instances", ["path", Q("/"), ["page", Q("1")]]],
            ["embedded_fonts", "no"]]
    OUT.write_text(dump(tree) + "\n")
    print("wrote", OUT, len(parts), "parts")


if __name__ == "__main__":
    main()
