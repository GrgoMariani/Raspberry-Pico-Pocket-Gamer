#!/usr/bin/env python3
"""Converts a PPM frame dump of the emulator into a PNG (needs Pillow)."""
import sys
from PIL import Image

if len(sys.argv) < 3:
    print("usage: ppm2png.py in.ppm out.png")
    sys.exit(2)

with open(sys.argv[1], "rb") as f:
    header = f.readline().decode().split()
    assert header[0] == "P6", "only binary ppm is supported"
    while True:
        line = f.readline().decode()
        if not line.startswith("#"):
            break
    w, h = (int(v) for v in line.split())
    maxval = int(f.readline())
    data = f.read()

img = Image.frombytes("RGB", (w, h), data)
img.save(sys.argv[2])
print(f"{sys.argv[2]}: {w}x{h}")
