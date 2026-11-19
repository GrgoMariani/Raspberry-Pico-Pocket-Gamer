#!/usr/bin/env python3
"""Converts a PPM frame dump of the emulator into a PNG.

Uses Pillow when it is installed, otherwise falls back to a small pure
stdlib PNG writer so the shot workflow works on any Python installation.
"""
import struct
import sys
import zlib


def write_png(path, w, h, rgb):
    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    # each scanline is prefixed with filter byte 0 (None)
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(raw, 9)) +
           chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


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

try:
    from PIL import Image
    img = Image.frombytes("RGB", (w, h), data)
    img.save(sys.argv[2])
except ImportError:
    write_png(sys.argv[2], w, h, data)
print(f"{sys.argv[2]}: {w}x{h}")