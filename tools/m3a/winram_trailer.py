#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The RAM-disk trailer of docs/design/watari-winram/DESIGN.md 2.2, as WatariM1RamDiskDxe reads it (T21).

The firmware registers a RAM disk from a range of guest RAM whose LAST 4 KiB is this trailer:

    +0   8 bytes  magic "WRAMDSK1"
    +8   u64 LE   image_bytes  (the disk the firmware sees; a multiple of 512, <= range size - 4096)
    +16  u32 LE   CRC-32 of the first image_bytes bytes (the UEFI CalculateCrc32 polynomial = standard CRC-32, zlib.crc32)
    +20  32 bytes SHA-256 of the image (informational)
    +52  zero padding to 4096 bytes

Usage:  winram_trailer.py append <image> <out>   write image + trailer to <out> (the range to load: len = image + 4096)
        winram_trailer.py trailer <image>        write only the 4096 trailer bytes to stdout
        winram_trailer.py --selftest
Standard library only; also imported by the loader and the bench scripts (build_trailer, check_trailer).
"""
import hashlib
import struct
import sys
import zlib

MAGIC = b"WRAMDSK1"
TRAILER_SIZE = 4096


def build_trailer(image: bytes) -> bytes:
    if len(image) == 0 or len(image) % 512:
        raise ValueError("image size must be a non-zero multiple of 512")
    t = MAGIC + struct.pack("<QI", len(image), zlib.crc32(image) & 0xFFFFFFFF) + hashlib.sha256(image).digest()
    return t + b"\0" * (TRAILER_SIZE - len(t))


def check_trailer(blob: bytes):
    """blob = image + trailer. Returns (image_bytes, crc) when consistent, else raises ValueError."""
    if len(blob) <= TRAILER_SIZE:
        raise ValueError("too short")
    t = blob[-TRAILER_SIZE:]
    if t[:8] != MAGIC:
        raise ValueError("no magic")
    n, crc = struct.unpack("<QI", t[8:20])
    if n % 512 or n < 512 or n > len(blob) - TRAILER_SIZE:
        raise ValueError("image_bytes does not fit")
    if zlib.crc32(blob[:n]) & 0xFFFFFFFF != crc:
        raise ValueError("CRC mismatch")
    if hashlib.sha256(blob[:n]).digest() != t[20:52]:
        raise ValueError("SHA-256 mismatch")
    return n, crc


def _selftest():
    img = bytes(range(256)) * 8                       # 2048 bytes
    blob = img + build_trailer(img)
    assert len(blob) == len(img) + TRAILER_SIZE
    assert check_trailer(blob) == (len(img), zlib.crc32(img) & 0xFFFFFFFF)
    assert check_trailer(blob)[1] == __import__("binascii").crc32(img) & 0xFFFFFFFF   # the polynomial of UEFI CalculateCrc32
    bad = bytearray(blob)
    bad[100] ^= 1
    for b, why in ((bytes(bad), "CRC"), (blob[:-1], "magic"), (img + b"\0" * TRAILER_SIZE, "magic")):
        try:
            check_trailer(b)
        except ValueError:
            continue
        raise AssertionError("accepted a bad blob: " + why)
    try:
        build_trailer(b"x" * 100)
    except ValueError:
        pass
    else:
        raise AssertionError("accepted an image that is not a multiple of 512")
    print("winram_trailer selftest: ok")


def main(argv):
    if argv[1:] == ["--selftest"]:
        _selftest()
        return 0
    if len(argv) == 4 and argv[1] == "append":
        img = open(argv[2], "rb").read()
        with open(argv[3], "wb") as f:
            f.write(img + build_trailer(img))
        return 0
    if len(argv) == 3 and argv[1] == "trailer":
        sys.stdout.buffer.write(build_trailer(open(argv[2], "rb").read()))
        return 0
    sys.stderr.write(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
