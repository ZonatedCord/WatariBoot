#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Tests of the post-mortem log side of watari-load.py (T16j): the region parser, --pmlog at the jump (order, zeroing, boot
arguments, record file, BLOCK CHECK), --postmortem (reads only, valid / absent / torn / wrapped logs). Fake proxy, no device.
The C writer (el2/vgic/pmlog.c) is checked against this parser by el2/boot/run-pmlog.sh. Run: python3 -I tools/m3a/test_pmlog.py"""
import contextlib
import hashlib
import importlib.util
import io
import json
import os
import re
import struct
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("watari_load", os.path.join(HERE, "watari-load.py"))
wl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wl)

FAILED = []


def check(cond, msg):
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        FAILED.append(msg)


def build_region(size, chunks, boots=1, seal=True):
    """The ring as the layer writes it (include/wel2_pmlog.h), written independently of the parser under test."""
    cap = size - 64
    data = bytearray(size)
    total = 0
    for c in chunks:
        if len(c) > cap:
            skip = len(c) - cap
            c, total = c[skip:], total + skip
        pos = total % cap
        for i, b in enumerate(c):
            data[64 + (pos + i) % cap] = b
        total += len(c)
    hdr = struct.pack("<8sIIIIQII20s", b"WEL2PMLG", 1, 64, cap, boots, total, total // cap, 0, b"\0" * 20)
    crc = zlib.crc32(hdr) & 0xffffffff
    data[:64] = hdr + struct.pack("<I", crc if seal else crc ^ 1)
    return bytes(data)


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
                       ("gfdt_dump", "# options: --ramdisk --pmlog\necho \"$@\" >> \"$(dirname \"$0\")/gfdt-args.txt\"\necho stub\nexit 0\n")):
        p = os.path.join(tmp, name)
        open(p, "w").write("#!/bin/sh\n" + body)
        os.chmod(p, 0o755)
    open(os.path.join(tmp, "SHA256SUMS"), "w").write("".join("%s  %s\n" % (hashlib.sha256(d).hexdigest(), n) for n, d in sorted(files.items())))


class Fake(wl.DryTarget):
    dry_run = False

    def __init__(self):
        super().__init__(echo=False)


def run(argv, target, tmp):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        rc = wl.main(["--bundle", tmp, "--out", os.path.join(tmp, "out")] + argv, target=target)
    return rc, out.getvalue()


def names(t):
    return [c[0] for c in t.calls]


# ---- the parser -----------------------------------------------------------------------------------------------------
SZ = 0x4000
r = wl.pmlog_parse_region(build_region(SZ, [b"hello\n", b"world\n"]))
check(r["status"] == "ok" and r["text"] == b"hello\nworld\n" and r["hdr"]["total"] == 12 and r["hdr"]["boots"] == 1, "a short log parses")
r = wl.pmlog_parse_region(build_region(SZ, [b"x" * 100 for _ in range(500)], boots=3))
check(r["status"] == "ok" and r["hdr"]["wraps"] == 3 and len(r["text"]) == SZ - 64 and r["hdr"]["boots"] == 3, "a wrapped log keeps one capacity")
chunks = [("line %05d\n" % i).encode() for i in range(3000)]
r = wl.pmlog_parse_region(build_region(SZ, chunks))
check(r["status"] == "ok" and r["text"].endswith(b"line 02999\n") and r["text"][-11 * 5:].decode().split("\n")[:-1] ==
      ["line %05d" % i for i in range(2995, 3000)], "a wrapped log is returned oldest first, newest last")
r = wl.pmlog_parse_region(build_region(SZ, [b"y" * (3 * SZ)]))
check(r["status"] == "ok" and r["text"] == b"y" * (SZ - 64), "a write longer than the ring keeps the tail")
r = wl.pmlog_parse_region(build_region(SZ, [b"torn\n"], seal=False))
check(r["status"] == "crc" and r["text"] == b"torn\n" and not r["hdr"]["crc_ok"], "a bad header CRC salvages the text and says crc")
check(wl.pmlog_parse_region(b"\0" * SZ)["status"] == "nomagic", "zeros: no magic")
check(wl.pmlog_parse_region(b"\0" * 100)["status"] == "short", "a short region")
bad = bytearray(build_region(SZ, [b"a\n"]))
struct.pack_into("<I", bad, 0x20, 9)                 # wraps wrong ...
struct.pack_into("<I", bad, 0x3c, zlib.crc32(bytes(bad[:60])) & 0xffffffff)      # ... with a valid CRC
check(wl.pmlog_parse_region(bytes(bad))["status"] == "inconsistent", "valid CRC but inconsistent fields: not trusted")
check(wl.pmlog_parse_region(build_region(SZ, [b"a\n"]) + b"\0" * 0x4000)["status"] == "inconsistent", "capacity that does not match the region size")

# ---- the loader -----------------------------------------------------------------------------------------------------
with tempfile.TemporaryDirectory() as tmp:
    make_bundle(tmp)
    FD, GD = os.path.join(tmp, "fdt_m1_dump"), os.path.join(tmp, "gfdt_dump")

    t = Fake()
    rc, out = run(["--rung", "D2", "--edk2", "WATARI_M1_EFI.fd", "--pmlog", "--fdt-dump", FD, "--gfdt-dump", GD, "--go"], t, tmp)
    n = names(t)
    check(rc == 0, "D2 --pmlog hands over")
    allocs = [c for c in t.calls if c[0] == "memalign"]
    check(len(allocs) == 3 and all(c[1] == wl.BLOCK for c in allocs), "D2 --pmlog: still three 32 MB aligned blocks (layer, spacer, edk2): the ring takes no block of its own")
    pm = [c for c in t.calls if c[0] == "writemem" and c[2] == 2048 * 1024]
    check(len(pm) == 1 and pm[0][1] % wl.BLOCK == 0, "the region is written once, 1 MiB, at its block")
    spacer_addr = int(re.search(r"^SPACER 32 MB aligned block at 0x([0-9a-f]+)", out, re.M).group(1), 16)
    edk2_addr = int(re.search(r"^EDK2 .* bytes at 0x([0-9a-f]+)", out, re.M).group(1), 16)
    check(pm[0][1] == spacer_addr and spacer_addr + wl.BLOCK == edk2_addr,
          "the ring sits at the start of the SPACER block, below EDK2: the range that holds EDK2's entry stays whole (placement rule: 64 MB above the FDT)")
    check(t.mem[pm[0][1]] == b"\0" * 2048 * 1024, "the region is zeroed")
    ba = [c for c in t.calls if c[0] == "kboot_set_chosen"][0][2]
    check("wel2.pmlog=0x%x,0x200000" % pm[0][1] in ba.split(), "boot arguments carry wel2.pmlog=0x<base>,0x200000: %r" % ba)
    check(any(c[0] == "dc_cvac" and c[1] == pm[0][1] and c[2] == 2048 * 1024 for c in t.calls), "the region is cleaned to the point of coherency")
    rec = json.load(open(os.path.join(tmp, "out", wl.PM_RECORD)))
    check(int(rec["base"], 0) == pm[0][1] and rec["size"] == 2048 * 1024 and rec["rung"] == "D2", "the base is recorded in out/pmlog-base.json")
    check("--pmlog 0x%x,0x200000" % pm[0][1] in open(os.path.join(tmp, "gfdt-args.txt")).read(), "gfdt_dump gets --pmlog 0x<base>,0x200000")
    check("BLOCK CHECK: PASS" in out and "pmlog" in out, "BLOCK CHECK covers the pmlog region")
    check(n[-1] == "kboot_boot", "kboot_boot is still the last call")

    # without --pmlog nothing changes: no region, no argument, no record
    os.remove(os.path.join(tmp, "out", wl.PM_RECORD))
    t = Fake()
    rc, out = run(["--rung", "D2", "--edk2", "WATARI_M1_EFI.fd", "--fdt-dump", FD, "--gfdt-dump", GD, "--go"], t, tmp)
    ba = [c for c in t.calls if c[0] == "kboot_set_chosen"][0][2]
    check(rc == 0 and "pmlog" not in ba and t.calls.count(("memalign", wl.BLOCK, wl.BLOCK)) == 3 and not os.path.exists(os.path.join(tmp, "out", wl.PM_RECORD)),
          "no --pmlog: no pmlog argument, three blocks, no record")
    t = Fake()
    rc, out = run(["--rung", "D0", "--edk2", "WATARI_M1_EFI.fd", "--fdt-dump", FD, "--gfdt-dump", GD], t, tmp)
    d0_blocks = re.findall(r"^BLOCK  (layer|guest|edk2) .*at (0x[0-9a-f]+)", out, re.M)
    t = Fake()
    rc, out = run(["--rung", "D0", "--edk2", "WATARI_M1_EFI.fd", "--pmlog", "--fdt-dump", FD, "--gfdt-dump", GD], t, tmp)
    pm_blocks = re.findall(r"^BLOCK  (layer|guest|edk2) .*at (0x[0-9a-f]+)", out, re.M)
    pmr = re.search(r"^PMLOG  region (0x[0-9a-f]+)", out, re.M).group(1)
    check(d0_blocks == pm_blocks and len(pm_blocks) == 3 and pmr == dict(pm_blocks)["guest"], "D0 --pmlog: the same three block addresses as D0 without it; the ring is in the 'guest' (spacer) block")

    # D0 previews: the region is allocated and checked, not written, not recorded, not passed
    t = Fake()
    rc, out = run(["--rung", "D0", "--edk2", "WATARI_M1_EFI.fd", "--pmlog", "--pmlog-size", "2048", "--fdt-dump", FD, "--gfdt-dump", GD], t, tmp)
    check(rc == 0 and "kboot_set_chosen" not in names(t) and "kboot_boot" not in names(t) and "not written" in out
          and "BLOCK CHECK: PASS" in out and not os.path.exists(os.path.join(tmp, "out", wl.PM_RECORD)), "D0 --pmlog: previewed, nothing written or recorded")

    # fixed base: no extra allocation; the check fails when the base is outside usable RAM
    t = Fake()
    rc, out = run(["--rung", "D2", "--edk2", "WATARI_M1_EFI.fd", "--pmlog-base", "0x9e0000000", "--pmlog-size", "256", "--fdt-dump", FD,
                   "--gfdt-dump", GD, "--go"], t, tmp)
    ba = [c for c in t.calls if c[0] == "kboot_set_chosen"][0][2]
    check(rc == 0 and "wel2.pmlog=0x9e0000000,0x40000" in ba and t.calls.count(("memalign", wl.BLOCK, wl.BLOCK)) == 3, "--pmlog-base: fixed, no extra block, 256 KiB: %r" % ba)
    t = Fake()
    rc, out = run(["--rung", "D0", "--edk2", "WATARI_M1_EFI.fd", "--pmlog-base", "0xb00000000", "--fdt-dump", FD, "--gfdt-dump", GD], t, tmp)
    check(rc == 4 and "BLOCK CHECK: FAIL" in out and "pmlog" in out, "a fixed base outside usable RAM fails the BLOCK CHECK")

    # static refusals (before any proxy call)
    for argv, why in ((["--pmlog", "--pmlog-size", "10"], "size not a multiple of 16 KiB"), (["--pmlog", "--pmlog-size", "999999"], "size too big"),
                      (["--pmlog-base", "0x9e0000400"], "base not 16 KB aligned")):
        t = Fake()
        rc, out = run(["--rung", "L3"] + argv, t, tmp)
        check(rc == 2 and t.calls == [], "refused before any proxy call: %s" % why)

    # L-rung (no EDK2): the argument is passed too
    t = Fake()
    rc, out = run(["--rung", "L3", "--pmlog", "--pmlog-size", "512", "--go"], t, tmp)
    ba = [c for c in t.calls if c[0] == "kboot_set_chosen"][0][2]
    check(rc == 0 and "wel2.pmlog=0x" in ba and ",0x80000" in ba, "L3 --pmlog 512: %r" % ba)

    # dry run: nothing recorded
    if os.path.exists(os.path.join(tmp, "out", wl.PM_RECORD)):
        os.remove(os.path.join(tmp, "out", wl.PM_RECORD))
    rc, out = run(["--rung", "L3", "--pmlog", "--dry-run"], None, tmp)
    check(rc == 0 and "not recorded" in out and not os.path.exists(os.path.join(tmp, "out", wl.PM_RECORD)), "--dry-run does not record a base")

    # ---- --postmortem ----------------------------------------------------------------------------------------------
    outd = os.path.join(tmp, "out")
    region = build_region(0x100000, [b"Watari EL2: hello from EL2\n", b"Watari EL2: PSCI CPU_ON cpu 1 accepted\n", b"Watari EL2: FATAL: x\n"], boots=1)
    base = 0x9e0000000
    wl.write_pmlog_record(outd, base, 0x100000, "D2", "x")
    t = Fake()
    t.mem[base] = region
    rc, out = run(["--postmortem"], t, tmp)
    check(rc == 0 and open(os.path.join(outd, "pmlog.txt"), "rb").read().endswith(b"FATAL: x\n") and os.path.getsize(os.path.join(outd, "pmlog.bin")) == 0x100000,
          "--postmortem: base from the record, header validated, text and raw region saved")
    check("CPU_ON cpu 1 accepted" in out and "CRC ok" in out, "--postmortem prints the last lines and the header")
    check(all(c[0] in ("readmem", "timeout") for c in t.calls) and not any(c[0] in ("writemem", "malloc", "memalign") for c in t.calls),
          "--postmortem only reads (no write, no allocation): %s" % sorted(set(names(t))))
    t = Fake()
    t.mem[0x900000000] = region
    rc, out = run(["--postmortem", "--pmlog-base", "0x900000000"], t, tmp)
    check(rc == 0 and "base from --pmlog-base" in out, "--postmortem --pmlog-base overrides the record")
    t = Fake()                                       # DRAM did not keep it: zeros
    rc, out = run(["--postmortem"], t, tmp)
    check(rc == 5 and "no log header" in out and "hypothesis" in out, "zeros at the base: exit 5 with the explanation")
    t = Fake()
    t.mem[base] = build_region(0x100000, [b"torn line\n"], seal=False)
    rc, out = run(["--postmortem"], t, tmp)
    check(rc == 6 and "UNVERIFIED" in out and "torn line" in out and open(os.path.join(outd, "pmlog.txt"), "rb").read() == b"torn line\n",
          "a torn header: exit 6, text saved and flagged UNVERIFIED")
    t = Fake()
    t.mem[base] = build_region(0x100000, [b"z" * 5000 for _ in range(300)])
    rc, out = run(["--postmortem"], t, tmp)
    check(rc == 0 and "wrapped" in out, "a wrapped ring is reported")
    os.remove(wl.pmlog_record_path(outd))
    t = Fake()
    rc, out = run(["--postmortem"], t, tmp)
    check(rc == 2 and t.calls == [], "no record and no --pmlog-base: refused before any proxy call")
    rc, out = run(["--postmortem", "--rung", "L3"], Fake(), tmp)
    check(rc == 2, "--postmortem excludes --rung")

print("\n%s" % ("ALL PASSED" if not FAILED else "%d FAILED" % len(FAILED)))
sys.exit(1 if FAILED else 0)
