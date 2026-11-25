#!/usr/bin/env python3
"""Builds assets/gta6menu.txd (RenderWare TXD, D3D9 platform, uncompressed A8R8G8B8).
Edit the PNGs in tools/src/ and run:  python3 tools/make_txd.py
Textures: logo, ic_play, ic_map, ic_gear, ic_script, ic_mods, palms   (names are used by main.cpp)
Open the result in Magic.TXD / TXD Workshop to edit it visually."""
import struct, os, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "src")
OUT = os.path.join(HERE, "..", "assets", "gta6menu.txd")
VER = 0x1803FFFF  # RW 3.6.0.3 (San Andreas)

def chunk(t, payload): return struct.pack("<III", t, len(payload), VER) + payload

def native(name, img):
    img = img.convert("RGBA")
    w, h = img.size
    bgra = b"".join(bytes((b, g, r, a)) for (r, g, b, a) in img.getdata())
    hdr = struct.pack("<I", 9)                         # platform: D3D9
    hdr += struct.pack("<BBH", 0x02, 0x11, 0)          # filter linear, wrap/wrap
    hdr += name.encode().ljust(32, b"\0") + b"".ljust(32, b"\0")
    hdr += struct.pack("<II", 0x0500, 21)              # raster 8888, D3DFMT_A8R8G8B8
    hdr += struct.pack("<HH", w, h)
    hdr += struct.pack("<BBBB", 32, 1, 4, 1)           # depth, levels, type, flags(alpha)
    hdr += struct.pack("<I", len(bgra)) + bgra
    return chunk(0x15, chunk(0x01, hdr) + chunk(0x03, b""))

def main():
    names = ["logo", "ic_play", "ic_map", "ic_gear", "ic_script", "ic_mods", "palms"]
    natives = []
    for n in names:
        p = os.path.join(SRC, n + ".png")
        natives.append(native(n, Image.open(p)))
        print("packed", n)
    body = chunk(0x01, struct.pack("<HH", len(names), 0)) + b"".join(natives) + chunk(0x03, b"")
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    open(OUT, "wb").write(chunk(0x16, body))
    print("wrote", os.path.abspath(OUT), os.path.getsize(OUT), "bytes")

if __name__ == "__main__":
    main()
