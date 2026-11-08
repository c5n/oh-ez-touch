#!/usr/bin/env python3
"""Write the PNG fixtures for test_icon_png.cpp.

Synthetic line-art-like pictures -- strokes, antialiased edges, transparent
background, a little noise so that the encoder has to use every kind of block
-- in every format icon_png_decode() reads, and two it refuses. Every row's
filter type is chosen in turn (0 to 4), which no real encoder does but which
is what the unfilter has to get right. Standard library only.

    python3 test/host/fixtures/make_icon_pngs.py
"""

import os
import random
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))


def chunk(kind, data):
    return (struct.pack(">I", len(data)) + kind + data
            + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def filtered(rows, bpp):
    out = bytearray()
    prev = bytes(len(rows[0]))
    for y, row in enumerate(rows):
        kind = y % 5
        out.append(kind)
        for i, x in enumerate(row):
            a = row[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            pred = (0, a, b, (a + b) >> 1, paeth(a, b, c))[kind]
            out.append((x - pred) & 0xFF)
        prev = row
    return bytes(out)


def png(name, width, height, color_type, rows, bpp, compress, depth=8,
        interlace=0, extra=b"", idat_split=0):
    raw = filtered(rows, bpp)
    data = compress(raw)
    ihdr = struct.pack(">IIBBBBB", width, height, depth, color_type, 0, 0, interlace)
    body = chunk(b"IHDR", ihdr) + extra
    if idat_split:
        for i in range(0, len(data), idat_split):
            body += chunk(b"IDAT", data[i:i + idat_split])
    else:
        body += chunk(b"IDAT", data)
    with open(os.path.join(HERE, name), "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + body + chunk(b"IEND", b""))


def level(n):
    return lambda raw: zlib.compress(raw, n)


def fixed(raw):
    c = zlib.compressobj(9, zlib.DEFLATED, 15, 9, zlib.Z_FIXED)
    return c.compress(raw) + c.flush()


def rgba(x, y, size, rnd):
    """A ring with a soft edge and a bar through it, on transparency."""
    cx = cy = size / 2
    d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
    edge = max(0.0, 1.0 - abs(d - size * 0.35) / (size * 0.06))
    bar = 1.0 if abs(y - cy) < size * 0.08 and abs(x - cx) < size * 0.3 else 0.0
    a = int(255 * max(edge, bar))
    if a == 0:
        return (rnd.randrange(256), 0, 0, 0)  # a colour under full transparency
    return ((x * 4) & 0xFF, (y * 4) & 0xFF, (rnd.randrange(64) + 128), a)


def main():
    rnd = random.Random(7)

    size = 64
    px = [[rgba(x, y, size, rnd) for x in range(size)] for y in range(size)]
    rows = [bytes(v for p in r for v in p) for r in px]
    png("icon_rgba64_dynamic.png", size, size, 6, rows, 4, level(9), idat_split=300)
    png("icon_rgba64_fixed.png", size, size, 6, rows, 4, fixed)

    size = 32
    px = [[rgba(x, y, size, rnd) for x in range(size)] for y in range(size)]
    rows = [bytes(v for p in r for v in p[:3]) for r in px]
    png("icon_rgb32_stored.png", size, size, 2, rows, 3, level(0))

    size = 40
    palette = [(i * 16, 255 - i * 16, 128) for i in range(16)]
    trns = bytes(i * 17 for i in range(12))  # entries 12..15 stay opaque
    rows = [bytes((x // 3 + y // 5) % 16 for x in range(size)) for y in range(size)]
    plte = chunk(b"PLTE", bytes(v for c in palette for v in c)) + chunk(b"tRNS", trns)
    png("icon_pal40_trns.png", size, size, 3, rows, 1, level(6), extra=plte)

    size = 32
    rows = [bytes((x * 8) & 0xFF if (x + y) % 7 else 0 for x in range(size)) for y in range(size)]
    png("icon_grey32_trns.png", size, size, 0, rows, 1, level(6),
        extra=chunk(b"tRNS", struct.pack(">H", 0)))

    size = 128
    px = [[rgba(x, y, size, rnd) for x in range(size)] for y in range(size)]
    rows = [bytes(v for p in r for v in (p[1], p[3])) for r in px]
    png("icon_ga128.png", size, size, 4, rows, 2, level(9))

    size = 32
    rows = [bytes(8 * size) for _ in range(size)]
    png("icon_rgba32_16bit.png", size, size, 6, rows, 8, level(9), depth=16)


if __name__ == "__main__":
    main()
