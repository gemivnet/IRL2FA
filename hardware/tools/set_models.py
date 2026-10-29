#!/usr/bin/env python3
"""Points every footprint's 3D model at the STEP files bundled in
hardware/lib/, so renders and the STEP export work with a bare KiCad install
(and in CI). Run in the KiCad container after placing footprints:
    python3 hardware/tools/set_models.py
SPDX-License-Identifier: CERN-OHL-S-2.0"""
from pathlib import Path

import pcbnew

HW = Path(__file__).resolve().parents[1]
PCB = HW / "irl2fa.kicad_pcb"
# Footprints whose exact model KiCad doesn't ship: closest bundled shape.
ALIASES = {"UFQFPN-32-1EP_5x5mm_P0.5mm_EP3.5x3.5mm": "QFN-32-1EP_5x5mm_P0.5mm_EP3.45x3.45mm"}


def main():
    board = pcbnew.LoadBoard(str(PCB))
    changed = 0
    for fp in board.GetFootprints():
        name = fp.GetFPID().GetLibItemName().wx_str() if hasattr(fp.GetFPID().GetLibItemName(), "wx_str") \
            else str(fp.GetFPID().GetLibItemName())
        for model in fp.Models():
            if "KIPRJMOD" in model.m_Filename:
                continue
            base = ALIASES.get(name, Path(model.m_Filename).stem)
            for folder in ("kicad.3dshapes", "irl2fa.3dshapes"):
                if (HW / "lib" / folder / f"{base}.step").exists():
                    model.m_Filename = f"${{KIPRJMOD}}/lib/{folder}/{base}.step"
                    changed += 1
                    break
            else:
                print(f"no bundled model for {fp.GetReference()} ({name})")
    board.Save(str(PCB))
    print("updated", changed, "model paths")


if __name__ == "__main__":
    main()
