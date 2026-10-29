#!/usr/bin/env python3
# SPDX-License-Identifier: CC-BY-SA-4.0
"""Generate case/board_dims.scad from hardware/board.yaml.

board.yaml is the mechanical contract between the PCB and the case. This script
flattens it into plain OpenSCAD constants so the case never hard-codes a board
dimension:

    board:   {width: 80}                 -> board_width = 80;
    display: {glass: {x: 6, ...}}        -> display_glass_x = 6;
    board:   {mounting_holes: [{x,y}..]} -> board_mounting_holes = [[3, 3], ...];
    display: {fpc_exit: left}            -> display_fpc_exit = "left";

New keys in board.yaml therefore show up automatically. Usage:

    python case/tools/gen_dims.py            # (re)write case/board_dims.scad
    python case/tools/gen_dims.py --check    # exit 1 if the file is stale
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import yaml

CASE_DIR = Path(__file__).resolve().parents[1]
REPO = CASE_DIR.parent
DEFAULT_IN = REPO / "hardware" / "board.yaml"
DEFAULT_OUT = CASE_DIR / "board_dims.scad"


def _ident(parts: list[str]) -> str:
    name = "_".join(parts)
    name = re.sub(r"[^A-Za-z0-9_]", "_", name)
    if name[0].isdigit():
        name = "_" + name
    return name


def _num(v) -> str:
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        return str(v)
    r = repr(float(v))
    return r[:-2] if r.endswith(".0") else r


def _value(v) -> str:
    """Render a YAML value as an OpenSCAD expression."""
    if isinstance(v, (int, float, bool)):
        return _num(v)
    if isinstance(v, str):
        return '"' + v.replace("\\", "\\\\").replace('"', '\\"') + '"'
    if isinstance(v, dict):
        # an inline record such as {x: 3, y: 3} becomes a vector in key order
        return "[" + ", ".join(_value(x) for x in v.values()) + "]"
    if isinstance(v, list):
        return "[" + ", ".join(_value(x) for x in v) + "]"
    if v is None:
        return "undef"
    raise TypeError(f"unsupported YAML value {v!r}")


def flatten(node, prefix: list[str], out: list[tuple[str, str, str]]) -> None:
    if isinstance(node, dict):
        for k, v in node.items():
            path = prefix + [str(k)]
            if isinstance(v, dict):
                # numeric records get a vector form as well as one constant per key
                if all(isinstance(x, (int, float)) and not isinstance(x, bool) for x in v.values()):
                    out.append((_ident(path), _value(v), "[" + ", ".join(map(str, v.keys())) + "]"))
                flatten(v, path, out)
            elif isinstance(v, list):
                keys = ""
                if v and isinstance(v[0], dict):
                    keys = "each [" + ", ".join(map(str, v[0].keys())) + "]"
                out.append((_ident(path), _value(v), keys))
            else:
                out.append((_ident(path), _value(v), ""))


def render(data: dict, src_name: str) -> str:
    rows: list[tuple[str, str, str]] = []
    flatten(data, [], rows)
    seen: set[str] = set()
    lines = [
        "// GENERATED FILE - DO NOT EDIT.",
        f"// Source: {src_name}  (regenerate: python case/tools/gen_dims.py)",
        "// Units: mm. Board coordinates: origin at the board's top-left corner seen",
        "// from the FRONT, x right, y down (see board.yaml).",
        "// SPDX-License-Identifier: CC-BY-SA-4.0",
        "",
    ]
    for name, val, note in rows:
        if name in seen:
            raise SystemExit(f"duplicate constant {name}")
        seen.add(name)
        lines.append(f"{name} = {val};" + (f"  // {note}" if note else ""))
    return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--input", type=Path, default=DEFAULT_IN)
    ap.add_argument("--output", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--check", action="store_true", help="fail if output is stale")
    args = ap.parse_args(argv)

    data = yaml.safe_load(args.input.read_text(encoding="utf-8"))
    text = render(data, "hardware/board.yaml")
    if args.check:
        cur = args.output.read_text(encoding="utf-8") if args.output.exists() else ""
        if cur != text:
            print(f"{args.output} is stale; run python case/tools/gen_dims.py", file=sys.stderr)
            return 1
        print(f"{args.output} is up to date")
        return 0
    args.output.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
