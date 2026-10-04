#!/usr/bin/env python3
"""Turns the desktop previews' PPM frames into 2x PNGs:  python3 tools/ppm2png.py DIR"""
import glob
import os
import sys

from PIL import Image

d = sys.argv[1] if len(sys.argv) > 1 else "."
for f in sorted(glob.glob(os.path.join(d, "*.ppm"))):
    im = Image.open(f)
    im.resize((im.width * 2, im.height * 2), Image.NEAREST).save(f[:-4] + ".png")
print("converted", len(glob.glob(os.path.join(d, "*.ppm"))))
