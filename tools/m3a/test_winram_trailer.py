#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Test of tools/m3a/winram_trailer.py (T21): the trailer WatariM1RamDiskDxe reads. Run: python3 -I tools/m3a/test_winram_trailer.py"""
import binascii
import os
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import winram_trailer as wt  # noqa: E402

FAILED = []


def check(cond, msg):
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        FAILED.append(msg)


img = os.urandom(512 * 33)
t = wt.build_trailer(img)
check(len(t) == 4096 and t[:8] == b"WRAMDSK1", "trailer: 4096 bytes, magic WRAMDSK1")
n, crc = struct.unpack("<QI", t[8:20])
check(n == len(img) and crc == zlib.crc32(img) & 0xFFFFFFFF == binascii.crc32(img) & 0xFFFFFFFF, "image_bytes and CRC-32 (the UEFI CalculateCrc32 polynomial)")
check(t[52:] == b"\0" * (4096 - 52), "zero padding behind the SHA-256")
check(wt.check_trailer(img + t) == (len(img), crc), "check_trailer accepts image + trailer")
bad = bytearray(img + t)
bad[1000] ^= 0x10
for name, blob in (("a flipped byte", bytes(bad)), ("no trailer", img + b"\0" * 4096), ("short", t), ("magic cut", (img + t)[:-1])):
    try:
        wt.check_trailer(blob)
        check(False, "check_trailer refuses " + name)
    except ValueError:
        check(True, "check_trailer refuses " + name)
try:
    wt.build_trailer(b"x" * 513)
    check(False, "an image that is not a multiple of 512 is refused")
except ValueError:
    check(True, "an image that is not a multiple of 512 is refused")
with tempfile.TemporaryDirectory() as d:
    src, out = os.path.join(d, "i.img"), os.path.join(d, "o.img")
    open(src, "wb").write(img)
    r = subprocess.run([sys.executable, "-I", os.path.join(HERE, "winram_trailer.py"), "append", src, out])
    check(r.returncode == 0 and open(out, "rb").read() == img + t, "CLI append writes image + trailer")
    r = subprocess.run([sys.executable, "-I", os.path.join(HERE, "winram_trailer.py"), "--selftest"], capture_output=True)
    check(r.returncode == 0, "CLI --selftest")
print("\n%s" % ("ALL PASSED" if not FAILED else "%d FAILED" % len(FAILED)))
sys.exit(1 if FAILED else 0)
