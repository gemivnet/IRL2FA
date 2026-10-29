#!/usr/bin/env python3
# SPDX-License-Identifier: CC-BY-SA-4.0
"""Fail if freshly built STLs differ from the committed ones.

Byte equality is checked first; when the bytes differ (a different OpenSCAD/CGAL
build can reorder triangles) the meshes are compared by triangle count, bounding
box and volume within a relative tolerance (default 0.1 %).

    python case/tools/compare_stl.py COMMITTED_DIR BUILT_DIR [--rel 0.001]
"""
import argparse
import sys
from pathlib import Path

import numpy as np
import trimesh


def compare(a: Path, b: Path, rel: float) -> list[str]:
    if a.read_bytes() == b.read_bytes():
        return []
    ma, mb = trimesh.load_mesh(a), trimesh.load_mesh(b)
    errs = []
    if abs(len(ma.faces) - len(mb.faces)) > rel * len(ma.faces):
        errs.append(f"triangles {len(ma.faces)} -> {len(mb.faces)}")
    scale = max(float(np.max(ma.extents)), 1.0)
    d = np.abs(ma.bounds - mb.bounds).max()
    if d > rel * scale:
        errs.append(f"bounding box moved by {d:.4f} mm ({ma.bounds.tolist()} -> {mb.bounds.tolist()})")
    if abs(ma.volume - mb.volume) > rel * abs(ma.volume):
        errs.append(f"volume {ma.volume:.3f} -> {mb.volume:.3f} mm^3")
    return errs


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("committed", type=Path)
    ap.add_argument("built", type=Path)
    ap.add_argument("--rel", type=float, default=1e-3)
    args = ap.parse_args()
    built = sorted(p.name for p in args.built.glob("*.stl"))
    committed = sorted(p.name for p in args.committed.glob("*.stl"))
    if built != committed:
        print(f"STL sets differ: committed {committed}, built {built}")
        return 1
    stale = 0
    for name in built:
        errs = compare(args.committed / name, args.built / name, args.rel)
        print(f"{name}: {'OK' if not errs else 'STALE - ' + '; '.join(errs)}")
        stale += bool(errs)
    if stale:
        print("Committed STLs are stale: run case/build.sh and commit case/stl, case/3mf and docs/images/case.")
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())
