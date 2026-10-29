#!/usr/bin/env python3
"""Build and run tools/screens.c, then turn the PBMs into e-paper-looking PNGs
in docs/images/screens/.  SPDX-License-Identifier: AGPL-3.0-or-later"""
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

FW = Path(__file__).resolve().parents[1]
OUT = FW.parent / "docs" / "images" / "screens"
SRC = ["tools/screens.c", "vhal/vhal.c"] + [
    f"core/{n}.c" for n in "hash base32 totp words wordlist pairing store gfx fonts timeutil app buttons".split()
] + ["core/third_party/monocypher/monocypher.c"]

PAPER = (233, 231, 222)
INK = (28, 28, 30)
SCALE = 3


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "screens"
        subprocess.run(["cc", "-std=c11", "-O1", "-o", str(exe), *SRC], cwd=FW, check=True)
        subprocess.run([str(exe), tmp], check=True)
        for pbm in sorted(Path(tmp).glob("*.pbm")):
            img = Image.open(pbm).convert("L")
            rgb = Image.new("RGB", img.size, PAPER)
            rgb.paste(INK, mask=img.point(lambda v: 255 if v == 0 else 0))
            rgb = rgb.resize((img.width * SCALE, img.height * SCALE), Image.NEAREST)
            rgb.save(OUT / (pbm.stem + ".png"))
            print("wrote", OUT / (pbm.stem + ".png"))


if __name__ == "__main__":
    sys.exit(main())
