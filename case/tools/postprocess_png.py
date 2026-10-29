#!/usr/bin/env python3
# SPDX-License-Identifier: CC-BY-SA-4.0
"""Trim the flat background around OpenSCAD renders and re-save them compactly."""
import sys

from PIL import Image, ImageChops

PAD = 40

for path in sys.argv[1:]:
    im = Image.open(path).convert("RGB")
    bg = Image.new("RGB", im.size, im.getpixel((0, 0)))
    box = ImageChops.difference(im, bg).getbbox()
    if box:
        l, t, r, b = box
        im = im.crop((max(l - PAD, 0), max(t - PAD, 0), min(r + PAD, im.width), min(b + PAD, im.height)))
    im.save(path, optimize=True)
    print(f"{path}: {im.width}x{im.height}")
