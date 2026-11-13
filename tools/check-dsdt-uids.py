#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build-time check (PLAN E4): the processor _UIDs of the DSDT must equal the GICC UIDs of the MADT.

    check-dsdt-uids.py <Dsdt.asl> <acpigen binary> [--model j293] [--aml <Dsdt.aml>]

The MADT comes from `acpigen madt -m <model>`; its GICC entries (type 0x0B) carry the ACPI Processor
UID at offset 8. The ASL file must declare one `Device (CPUn)` per UID with `_HID "ACPI0007"` and
`_UID n`, nothing more and nothing less. With --aml the compiled table is disassembled with iasl
and the same eight devices must come back (the round trip of PLAN E4).
"""
import argparse
import os
import re
import struct
import subprocess
import sys
import tempfile


def madt_uids(acpigen, model):
    with tempfile.TemporaryDirectory() as d:
        out = os.path.join(d, "madt.bin")
        subprocess.run([acpigen, "madt", "-m", model, "-o", out], check=True)
        data = open(out, "rb").read()
    if data[:4] != b"APIC":
        sys.exit("FAIL: acpigen did not produce a MADT")
    length = struct.unpack_from("<I", data, 4)[0]
    uids = []
    off = 44  # 36-byte header + local interrupt controller address + flags
    while off + 2 <= length:
        etype, elen = data[off], data[off + 1]
        if elen == 0:
            sys.exit("FAIL: zero-length MADT entry")
        if etype == 0x0B:  # GICC
            uids.append(struct.unpack_from("<I", data, off + 8)[0])
        off += elen
    return uids


def num(tok):
    """An ASL integer as iasl prints it: Zero, One, Ones aside, decimal or 0x.."""
    return {"Zero": 0, "One": 1}.get(tok) if tok in ("Zero", "One") else int(tok, 0)


def asl_devices(text):
    """Return {device name: (hid, uid)} for every `Device (X) { ... }` of the text (simple form)."""
    text = re.sub(r"//[^\n]*", "", text)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    devs = {}
    for m in re.finditer(r"Device\s*\(\s*(\w+)\s*\)\s*\{(.*?)\}", text, flags=re.S):
        body = m.group(2)
        hid = re.search(r'Name\s*\(\s*_HID\s*,\s*"([^"]+)"\s*\)', body)
        uid = re.search(r"Name\s*\(\s*_UID\s*,\s*(\w+)\s*\)", body)
        devs[m.group(1)] = (hid.group(1) if hid else None, num(uid.group(1)) if uid else None)
    return devs


def check_devices(devs, uids, what):
    errs = []
    got = sorted(u for (_, u) in devs.values() if u is not None)
    if got != sorted(uids):
        errs.append("%s: _UID list %s differs from the MADT GICC UIDs %s" % (what, got, sorted(uids)))
    for name, (hid, uid) in sorted(devs.items()):
        if hid != "ACPI0007":
            errs.append("%s: %s has _HID %r, expected 'ACPI0007'" % (what, name, hid))
        if uid is not None and name != "CPU%d" % uid:
            errs.append("%s: %s has _UID %d (expected the name CPU%d)" % (what, name, uid, uid))
    return errs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("asl")
    ap.add_argument("acpigen")
    ap.add_argument("--model", default="j293")
    ap.add_argument("--aml")
    a = ap.parse_args()

    uids = madt_uids(a.acpigen, a.model)
    errs = check_devices(asl_devices(open(a.asl).read()), uids, "ASL " + a.asl)
    if a.aml:
        with tempfile.TemporaryDirectory() as d:
            subprocess.run(["iasl", "-p", os.path.join(d, "out"), "-d", a.aml], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            dsl = open(os.path.join(d, "out.dsl")).read()
        errs += check_devices(asl_devices(dsl), uids, "iasl -d of " + a.aml)
    if errs:
        print("FAIL: DSDT does not match the acpigen '%s' MADT:" % a.model)
        for e in errs:
            print("  " + e)
        return 1
    print("OK: DSDT processors _UID %s == acpigen madt -m %s GICC UIDs%s"
          % (sorted(uids), a.model, " (also after the iasl -d round trip)" if a.aml else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
