#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Convert a raw framebuffer dump (QEMU pmemsave of the layer's fake panel) to a PNG. Standard library only (struct, zlib).

Usage: fb-png.py <dump> <out.png> <width> <height> <stride_bytes> <fmt> [--crop-top N] [--scale-down K]
  fmt: x8r8g8b8   (B | G<<8 | R<<16, the top byte ignored)
       x2r10g10b10 (B 0..9, G 10..19, R 20..29: each 10-bit channel is shifted down to 8 bits)
       x2r10-as8   a 10-bit panel that software filled with 8-bit BGR pixels (the --bgr8 firmware): the pixels are read
                   as x8r8g8b8, i.e. what the software meant to show, not what the panel would show
  --crop-top N      drop the first N rows (the layer's band)
  --scale-down K    keep every K-th pixel in both directions (smaller PNG)
Prints "<out.png> nonblack=<n> of <total>" so a caller can tell a black screen from a drawn one (F-10: display sleep).
"""
import struct
import sys
import zlib


def png(w, h, rows):
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + r for r in rows)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def convert(d, w, h, stride, fmt, crop=0, k=1):
    rows = []
    nonblack = 0
    total = 0
    for y in range(crop, h, k):
        px = struct.unpack_from("<%dI" % w, d, y * stride)[::k]
        out = bytearray()
        for v in px:
            if fmt in ("x8r8g8b8", "x2r10-as8"):
                r, g, b = (v >> 16) & 255, (v >> 8) & 255, v & 255
            elif fmt == "x2r10g10b10":
                r, g, b = ((v >> 20) & 1023) >> 2, ((v >> 10) & 1023) >> 2, (v & 1023) >> 2
            else:
                raise SystemExit("unknown format " + fmt)
            out += bytes((r, g, b))
            total += 1
            if r | g | b:
                nonblack += 1
        rows.append(bytes(out))
    return len(rows[0]) // 3, len(rows), rows, nonblack, total


def main(argv):
    if len(argv) < 7:
        sys.stderr.write(__doc__)
        return 2
    dump, out, w, h, stride, fmt = argv[1], argv[2], int(argv[3]), int(argv[4]), int(argv[5]), argv[6]
    crop, k = 0, 1
    i = 7
    while i < len(argv):
        if argv[i] == "--crop-top":
            crop = int(argv[i + 1]); i += 2
        elif argv[i] == "--scale-down":
            k = int(argv[i + 1]); i += 2
        else:
            sys.stderr.write("unknown option " + argv[i] + "\n"); return 2
    d = open(dump, "rb").read()
    if len(d) < stride * h:
        sys.stderr.write("dump is %d bytes, need %d\n" % (len(d), stride * h)); return 1
    ow, oh, rows, nb, tot = convert(d, w, h, stride, fmt, crop, k)
    open(out, "wb").write(png(ow, oh, rows))
    print("%s nonblack=%d of %d" % (out, nb, tot))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
