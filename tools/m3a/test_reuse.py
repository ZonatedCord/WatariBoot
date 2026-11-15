#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Tests of `watari-load.py --ramdisk-reuse` (T16l): the image still in DRAM after a reset is not sent again; every doubt
(no record, other address, magic, size, CRC, SHA-256, a damaged sample, another image) means a normal transfer. Fake proxy
whose memory survives from one "boot" to the next (the Fake's mem dict is carried over). Run: python3 -I tools/m3a/test_reuse.py"""
import contextlib
import hashlib
import importlib.util
import io
import json
import os
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("watari_load", os.path.join(HERE, "watari-load.py"))
wl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wl)
MIB = wl.MIB
FAILED = []


def check(cond, msg):
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        FAILED.append(msg)


def image(size_hdr, body=b""):
    d = bytearray(64 + len(body))
    struct.pack_into("<QQ", d, 8, 0x80000, size_hdr)
    d[0x38:0x3c] = b"ARM\x64"
    d[64:] = body
    return bytes(d)


def make_bundle(tmp):
    files = {"wel2-chain.bin": image(0x100000, b"CHAIN"), "wel2-m1.bin": image(0x100000, b"M1"), "m1guest.bin": image(0x20000, b"G"),
             "j293-minimal.dtb": b"\xd0\x0d\xfe\xed" + struct.pack(">I", 64) + b"\0" * 56, "WATARI_M1_EFI.fd": image(0x300000, b"EDK2")}
    for n, d in files.items():
        open(os.path.join(tmp, n), "wb").write(d)
    open(os.path.join(tmp, "usable.txt"), "w").write("usable 0 base=0x800000000 size=0x200000000 end=0xa00000000\n")
    for name, body in (("fdt_m1_dump", "echo 'platform m1 (stub)'\ncat \"$(dirname \"$0\")/usable.txt\"\n"),
                       ("gfdt_dump", "# options: --ramdisk --pmlog\necho stub\nexit 0\n")):
        p = os.path.join(tmp, name)
        open(p, "w").write("#!/bin/sh\n" + body)
        os.chmod(p, 0o755)
    open(os.path.join(tmp, "SHA256SUMS"), "w").write("".join("%s  %s\n" % (hashlib.sha256(d).hexdigest(), n) for n, d in sorted(files.items())))


class Fake(wl.DryTarget):
    dry_run = False

    def __init__(self, mem=None, start=None):
        super().__init__(echo=False)
        if mem is not None:
            self.mem = dict(mem)
        if start is not None:
            self._next = start


def run(argv, target, tmp):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        rc = wl.main(["--bundle", tmp, "--out", os.path.join(tmp, "out")] + argv, target=target)
    return rc, out.getvalue()


def rd_writes(t, base, rng):
    return [c for c in t.calls if c[0] == "writemem" and base <= c[1] < base + rng]


with tempfile.TemporaryDirectory() as tmp:
    make_bundle(tmp)
    FD, GD = os.path.join(tmp, "fdt_m1_dump"), os.path.join(tmp, "gfdt_dump")
    N = 40 * MIB + 12345
    rd_path = os.path.join(tmp, "winram-test.img")
    with open(rd_path, "wb") as f:
        for i in range(0, N, MIB):
            f.write(wl.test_pattern(i // MIB, min(MIB, N - i)))
    data = open(rd_path, "rb").read()
    open(rd_path + ".sha256", "w").write("%s  winram-test.img\n" % hashlib.sha256(data).hexdigest())
    RNG = wl.ramdisk_range_size(N)
    ARGS = ["--rung", "D2", "--go", "--edk2", "WATARI_M1_EFI.fd", "--ramdisk", rd_path, "--fdt-dump", FD, "--gfdt-dump", GD]
    REC = os.path.join(tmp, "out", wl.RD_RECORD)

    # attempt 1: nothing in DRAM, no record: --ramdisk-reuse falls back to the transfer and records the block
    t1 = Fake()
    rc, out = run(ARGS + ["--ramdisk-reuse"], t1, tmp)
    base_addr = [c for c in t1.calls if c[0] == "memalign"][0]
    base_addr = sorted(a for a in t1.mem if len(t1.mem[a]) == 16 * MIB)[0]
    check(rc == 0 and "RAMDISK REUSE refused: no record of an earlier transfer" in out and "RAMDISK written:" in out,
          "attempt 1: no record -> normal transfer")
    rec = json.load(open(REC))
    check(int(rec["base"], 0) == base_addr and rec["size"] == N and rec["sha256"] == hashlib.sha256(data).hexdigest(),
          "attempt 1: the block is recorded in out/ramdisk-block.json")
    check(len(rd_writes(t1, base_addr, RNG)) == 4, "attempt 1: three slices + trailer written")
    mem_after = t1.mem
    trl = base_addr + RNG - 4096

    def again(mem, start=None):
        t = Fake(mem, start)
        rc, out = run(ARGS + ["--ramdisk-reuse"], t, tmp)
        return t, rc, out

    # attempt 2: DRAM kept, same allocation order -> same address -> reuse
    t2, rc, out = again(mem_after)
    check(rc == 0 and "REUSING image in DRAM, not verified by the loader; EDK2's CRC-32 check will verify it" in out
          and "RAMDISK reused: %d bytes" % N in out and "RAMDISK written:" not in out, "attempt 2: reused, the banner is printed")
    check(not rd_writes(t2, base_addr, RNG), "attempt 2: not one write into the RAM-disk block (no slice, no trailer)")
    check(sum(1 for c in t2.calls if c[0] == "readmem" and base_addr <= c[1] < base_addr + RNG) == 1 + len(wl.sample_offsets(N)),
          "attempt 2: the trailer and %d samples were read back" % len(wl.sample_offsets(N)))
    ba = [c for c in t2.calls if c[0] == "kboot_set_chosen"][0][2]
    check("wel2.ramdisk=0x%x,0x%x" % (base_addr, RNG) in ba and t2.calls[-1][0] == "kboot_boot", "attempt 2: boot arguments as ever, the jump is last")
    check(any(c[0] == "dc_cvac" and c[1] == base_addr for c in t2.calls), "attempt 2: cache maintenance still covers the RAM-disk range")
    dec = json.load(open(os.path.join(tmp, "out", wl.RD_REUSE_LOG)))
    check(dec["decision"] == "reuse" and dec["base"] == "0x%x" % base_addr, "attempt 2: the decision is recorded in out/ramdisk-reuse.json")

    # without the flag: full transfer even with the image in DRAM
    t = Fake(mem_after)
    rc, out = run(ARGS, t, tmp)
    check(rc == 0 and len(rd_writes(t, base_addr, RNG)) == 4 and "REUS" not in out, "without --ramdisk-reuse the image is always transferred")

    def refused(label, mem, want):
        t, rc, out = again(mem)
        ok = rc == 0 and ("RAMDISK REUSE refused: " + want) in out and "RAMDISK written:" in out and len(rd_writes(t, base_addr, RNG)) == 4
        check(ok, "%s -> refused, normal transfer" % label)

    def patched(addr, off, newbytes):
        m = {a: bytearray(d) for a, d in mem_after.items()}
        for a, d in m.items():
            if a <= addr < a + len(d):
                d[addr - a + off:addr - a + off + len(newbytes)] = newbytes
        return {a: bytes(d) for a, d in m.items()}

    refused("trailer magic wiped", patched(trl, 0, b"\0" * 8), "no trailer magic")
    refused("trailer size differs", patched(trl, 8, struct.pack("<Q", N + 512)), "trailer image size")
    refused("trailer SHA differs", patched(trl, 20, b"\xff" * 32), "trailer SHA-256 differs")
    refused("trailer CRC differs", patched(trl, 16, struct.pack("<I", 0xdeadbeef)), "trailer CRC-32")
    for off in wl.sample_offsets(N):
        refused("sample at 0x%x damaged" % off, patched(base_addr + off, 7, b"\xa5\xa5"), "sample of the image at offset 0x%x differs" % off)
    t, rc, out = again(patched(base_addr + 5 * MIB, 100, b"\x00\x00"))
    check("REUSING image" in out, "damage outside the samples is not seen here (EDK2's CRC-32 over the whole image is the real guard)")

    # the block lands at another address than recorded (the heap was different)
    t, rc, out = again(mem_after, start=0x8_4000_0000 + 0x4000_0000)
    check(rc == 0 and "RAMDISK REUSE refused: the block is at 0x" in out and "RAMDISK written:" in out, "block at another address than recorded -> refused, normal transfer")
    # that run transferred and re-recorded; make the record name another image
    r2 = json.load(open(REC))
    r2["sha256"] = "0" * 64
    json.dump(r2, open(REC, "w"))
    t, rc, out = again(mem_after, start=0x8_4000_0000 + 0x4000_0000)
    check("RAMDISK REUSE refused: the recorded transfer was another image" in out, "record of another image -> refused")
    os.remove(REC)
    t, rc, out = again(mem_after)
    check("no record of an earlier transfer" in out, "no record -> refused even with the image in DRAM")
    open(REC, "w").write(json.dumps(dict(base="0x%x" % base_addr, range_size=RNG, size=N, sha256=hashlib.sha256(data).hexdigest(), crc32="0x0")))
    t, rc, out = again({})
    check("no trailer magic" in out and "RAMDISK written:" in out, "DRAM without the image -> refused")
    open(REC, "w").write("{not json")
    t, rc, out = again(mem_after)
    check("no record of an earlier transfer" in out, "unreadable record -> refused")

    # usage error; a dry run never reuses and writes no block record
    try:
        with contextlib.redirect_stderr(io.StringIO()):
            wl.parse_args(["--rung", "D2", "--ramdisk-reuse"])
        check(False, "--ramdisk-reuse without --ramdisk is a usage error")
    except SystemExit as e:
        check(e.code == 2, "--ramdisk-reuse without --ramdisk is a usage error")
    os.remove(REC)
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        rc = wl.main(["--bundle", tmp, "--out", os.path.join(tmp, "out"), "--dry-run", "--rung", "D0", "--edk2", "WATARI_M1_EFI.fd",
                      "--ramdisk", rd_path, "--ramdisk-reuse", "--fdt-dump", FD, "--gfdt-dump", GD])
    check(rc == 0 and "RAMDISK REUSE refused" in out.getvalue() and not os.path.exists(REC), "dry run: refused, no block record written")

print("\n%s" % ("ALL PASSED" if not FAILED else "%d FAILED" % len(FAILED)))
sys.exit(1 if FAILED else 0)
