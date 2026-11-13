#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fake-proxy test of watari-load.py (PLAN S17). Run: python3 -I tools/m3a/test_watari_load.py
No device, no m1n1 checkout. Asserts the proxy call ORDER, the boot arguments, the handling of a lost kboot_boot reply,
and that m1n1.setup is never imported."""
import ast
import contextlib
import hashlib
import importlib.util
import io
import os
import stat
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("watari_load", os.path.join(HERE, "watari-load.py"))
wl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wl)

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
    files = {
        "wel2-m1.bin": image(0x100000, b"M1"),
        "wel2-chain.bin": image(0x100000, b"CHAIN"),
        "m1guest.bin": image(0x20000, b"GUEST"),
        "j293-minimal.dtb": b"\xd0\x0d\xfe\xed" + struct.pack(">I", 64) + b"\0" * 56,
    }
    for n, d in files.items():
        with open(os.path.join(tmp, n), "wb") as f:
            f.write(d)
    dump = os.path.join(tmp, "fdt_m1_dump")           # stub: the real tool is tested by make -C el2 test
    with open(dump, "w") as f:
        f.write("#!/bin/sh\necho 'platform m1 (stub)'\n")
    os.chmod(dump, 0o755)
    with open(os.path.join(tmp, "SHA256SUMS"), "w") as f:
        for n in sorted(files):
            f.write("%s  %s\n" % (hashlib.sha256(files[n]).hexdigest(), n))
    return files


def run(argv, target, tmp):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        rc = wl.main(["--bundle", tmp, "--out", os.path.join(tmp, "out")] + argv, target=target)
    return rc, out.getvalue()


def names(t):
    return [c[0] for c in t.calls]


def idx(t, name, nth=0):
    return [i for i, c in enumerate(t.calls) if c[0] == name][nth]


class Fake(wl.DryTarget):
    """DryTarget that is quiet and lets the test read back the 'prepared' FDT."""
    def __init__(self):
        super().__init__(echo=False)


with tempfile.TemporaryDirectory() as tmp:
    files = make_bundle(tmp)

    # --- L3 full order ---------------------------------------------------------------------------------------------
    t = Fake()
    rc, out = run(["--rung", "L3", "--cpuif", "trap-notc", "--go"], t, tmp)
    n = names(t)
    check(rc == 0, "L3 exits 0 after the hand-over")
    check(n[0] == "malloc" and n[1] == "writemem", "DTB is allocated and written first")
    check(n.count("memalign") == 2, "two memalign blocks (layer, guest)")
    last_alloc = max(i for i, c in enumerate(t.calls) if c[0] in ("memalign", "writemem"))
    first_clean = idx(t, "dc_cvau")
    check(last_alloc < first_clean, "all allocations and copies precede the cache maintenance")
    check(idx(t, "ic_ivau", 1) < idx(t, "kboot_set_chosen"), "cache maintenance precedes the boot arguments")
    check(idx(t, "kboot_set_chosen") < idx(t, "kboot_prepare_dt"), "boot arguments precede kboot_prepare_dt")
    check(idx(t, "kboot_prepare_dt") < idx(t, "msr_daif") < idx(t, "kboot_boot"), "prepare_dt -> DAIF -> boot")
    check(n.index("kboot_boot") == len(n) - 1, "kboot_boot is the last call")
    check("smp_start_secondaries" not in n, "no secondaries at L3")
    allocs = [c for c in t.calls if c[0] == "memalign"]
    check(all(c[1] == wl.BLOCK and c[2] == wl.BLOCK for c in allocs), "blocks are 32 MB aligned and 32 MB long")
    layer_addr = [c for c in t.calls if c[0] == "kboot_boot"][0][1]
    guest_addr = [c for c in t.calls if c[0] == "writemem"][2][1]
    check(layer_addr % wl.BLOCK == 0 and guest_addr % wl.BLOCK == 0 and layer_addr != guest_addr, "distinct aligned addresses")
    args = [c for c in t.calls if c[0] == "kboot_set_chosen"][0]
    check(args[1] == "bootargs" and args[2] == "wel2.step=3 wel2.cpuif=trap-notc wel2.guest=0x%x" % guest_addr,
          "boot arguments carry step, cpuif and the guest address: %r" % args[2])
    w = {c[1]: c[2] for c in t.calls if c[0] == "writemem"}
    check(w[layer_addr] == len(files["wel2-chain.bin"]) and w[guest_addr] == len(files["m1guest.bin"]), "chain layer and guest written")

    # --- L0: no jump, no bootargs, reads back --------------------------------------------------------------------
    t = Fake()
    rc, out = run(["--rung", "L0", "--fdt-dump", os.path.join(tmp, "fdt_m1_dump")], t, tmp)
    n = names(t)
    check(rc == 0, "L0 exits 0")
    check("kboot_boot" not in n and "msr_daif" not in n, "L0 never jumps")
    check("kboot_set_chosen" not in n and "memalign" not in n, "L0 sets no wel2 arguments and allocates no blocks")
    check(n.count("readmem") == 2 and idx(t, "readmem") > idx(t, "kboot_prepare_dt"), "L0 reads the FDT back after prepare_dt")
    check(os.path.isfile(os.path.join(tmp, "out", "prepared-fdt.dtb")), "L0 saves the prepared FDT locally")
    check("platform m1 (stub)" in out, "L0 runs fdt_m1_dump on the blob")

    # --- L5: secondaries between set_chosen and prepare_dt ---------------------------------------------------------
    t = Fake()
    rc, _ = run(["--rung", "L5", "--go"], t, tmp)
    check(rc == 0 and idx(t, "kboot_set_chosen") < idx(t, "smp_start_secondaries") < idx(t, "kboot_prepare_dt"),
          "L5: set_chosen -> smp_start_secondaries -> prepare_dt")
    t = Fake()
    run(["--rung", "L5", "--no-smp", "--go"], t, tmp)
    check("smp_start_secondaries" not in names(t), "--no-smp skips the secondaries")

    # --- L1: non-chain image, no guest ----------------------------------------------------------------------------
    t = Fake()
    rc, _ = run(["--rung", "L1", "--go"], t, tmp)
    a = [c for c in t.calls if c[0] == "kboot_set_chosen"][0][2]
    check(rc == 0 and a == "wel2.step=1" and names(t).count("memalign") == 1, "L1: wel2.step=1 only, one block")

    # --- kboot_boot reply lost -------------------------------------------------------------------------------------
    t = Fake()
    t.boot_error = OSError("serial gone")
    rc, out = run(["--rung", "L2", "--go"], t, tmp)
    check(rc == 0 and "handed over" in out, "a transport error after the request was sent counts as handed over")
    t = Fake()
    t.boot_error = OSError("never sent")
    t.boot_error_before_send = True
    rc, out = run(["--rung", "L2", "--go"], t, tmp)
    check(rc == 1 and "NOT sent" in out, "a transport error before the request was sent is an error")
    t = Fake()
    t.boot_error = ValueError("reply error")
    rc, _ = run(["--rung", "L2", "--go"], t, tmp)
    check(rc == 1, "a non-transport error is not a hand-over")

    # --- prepare_dt failure ----------------------------------------------------------------------------------------
    t = Fake()
    t.fail_prepare = -1
    rc, _ = run(["--rung", "L3", "--go"], t, tmp)
    check(rc == 1 and "kboot_boot" not in names(t), "kboot_prepare_dt failure stops before the jump")

    # --- manual --fdt-addr when the console line is missing --------------------------------------------------------
    class NoConsole(Fake):
        def console_text(self):
            return ""
    t = NoConsole()
    rc, _ = run(["--rung", "L0"], t, tmp)
    check(rc == 3, "L0 without a captured address exits 3")
    t = NoConsole()
    rc, _ = run(["--rung", "L0", "--fdt-addr", "0x840000000", "--fdt-dump", os.path.join(tmp, "fdt_m1_dump")], t, tmp)
    check(rc == 0, "L0 with --fdt-addr works")

    # --- jump rungs need --go on a real device; refused before any m1n1 import ----------------------------------------
    rc, out = run(["--rung", "L1", "--m1n1", "/nonexistent"], None, tmp)
    check(rc == 2 and "--go" in out, "L1 without --go is refused")

    # --- bundle integrity ------------------------------------------------------------------------------------------
    with open(os.path.join(tmp, "m1guest.bin"), "ab") as f:
        f.write(b"x")
    t = Fake()
    rc, out = run(["--rung", "L3", "--go"], t, tmp)
    check(rc == 2 and t.calls == [] and "mismatch" in out, "a damaged bundle is refused before any proxy call")

# --- m1n1.setup is never imported -----------------------------------------------------------------------------------
check("m1n1.setup" not in sys.modules and "m1n1" not in sys.modules, "m1n1 (and so m1n1.setup) not imported by the test run")
tree = ast.parse(open(os.path.join(HERE, "watari-load.py")).read())
bad = []
for node in ast.walk(tree):
    if isinstance(node, ast.Import):
        bad += [a.name for a in node.names if a.name.startswith("m1n1.setup")]
    elif isinstance(node, ast.ImportFrom):
        mod = node.module or ""
        if mod.startswith("m1n1.setup") or (mod == "m1n1" and any(a.name == "setup" for a in node.names)):
            bad.append(mod)
        if mod.startswith("m1n1") and mod not in ("m1n1.proxy", "m1n1.proxyutils", "m1n1.sysreg"):
            bad.append(mod)
check(not bad, "the loader imports only m1n1.proxy, m1n1.proxyutils, m1n1.sysreg: %s" % (bad or "ok"))

print("\n%s" % ("ALL PASSED" if not FAILED else "%d FAILED" % len(FAILED)))
sys.exit(1 if FAILED else 0)
