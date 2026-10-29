#!/usr/bin/env python3
# SPDX-License-Identifier: CC-BY-SA-4.0
"""Pack the case STLs into one multi-object 3MF laid out on a single plate.

OpenSCAD 2021 can only write one merged mesh per 3MF, so the plate is assembled
here: each part keeps its print orientation and gets its own named object.

    python case/tools/make_3mf.py case/stl case/3mf/irl2fa-all.3mf
"""
import sys
from pathlib import Path

import numpy as np
import trimesh

ORDER = ["front", "back", "door", "cap"]
GAP = 6.0


def main(stl_dir: str, out: str) -> int:
    scene = trimesh.Scene()
    x = 0.0
    y = 0.0
    row_h = 0.0
    for name in ORDER:
        m = trimesh.load_mesh(Path(stl_dir) / f"irl2fa-{name}.stl")
        lo, hi = m.bounds
        if x > 0 and x + (hi[0] - lo[0]) > 200:      # keep the plate inside 220 x 220
            x, y, row_h = 0.0, y + row_h + GAP, 0.0
        m.apply_translation([x - lo[0], y - lo[1], -lo[2]])
        scene.add_geometry(m, node_name=f"irl2fa-{name}", geom_name=f"irl2fa-{name}")
        x += hi[0] - lo[0] + GAP
        row_h = max(row_h, hi[1] - lo[1])
    lo, hi = scene.bounds
    scene.apply_translation([-(lo[0] + hi[0]) / 2 + 110, -(lo[1] + hi[1]) / 2 + 110, 0])
    Path(out).parent.mkdir(parents=True, exist_ok=True)
    Path(out).write_bytes(trimesh.exchange.threemf.export_3MF(scene))
    ext = np.round(scene.extents, 1).tolist()
    print(f"wrote {out}: {len(scene.geometry)} objects, plate footprint {ext[0]} x {ext[1]} mm")
    return 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:3]))
