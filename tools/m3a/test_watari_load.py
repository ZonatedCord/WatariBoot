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
        "WATARI_M1_EFI.fd": image(0x300000, b"EDK2"),
        "WATARI_M1_EFI-e4.fd": image(0x300000, b"EDK2-E4"),       # T16g declared fallbacks (build-m1-payload.sh --fallback-efi)
        "WATARI_M1_EFI-e1.fd": image(0x300000, b"EDK2-E1"),
    }
    for n, d in files.items():
        with open(os.path.join(tmp, n), "wb") as f:
            f.write(d)
    dump = os.path.join(tmp, "fdt_m1_dump")           # stub: the real tool is tested by make -C el2 test
    with open(dump, "w") as f:
        f.write("#!/bin/sh\necho 'platform m1 (stub)'\ncat \"$(dirname \"$0\")/usable.txt\"\n")
    with open(os.path.join(tmp, "usable.txt"), "w") as f:
        f.write("usable 0 base=0x800000000 size=0x200000000 end=0xa00000000\n")
    os.chmod(dump, 0o755)
    gdump = os.path.join(tmp, "gfdt_dump")            # stub: records its arguments, exit code from gfdt-rc.txt
    with open(gdump, "w") as f:
        f.write("#!/bin/sh\necho \"$@\" >> \"$(dirname \"$0\")/gfdt-args.txt\"\necho 'guest tree (stub)'\n"
                "exit $(cat \"$(dirname \"$0\")/gfdt-rc.txt\")\n")
    with open(os.path.join(tmp, "gfdt-rc.txt"), "w") as f:
        f.write("0\n")
    os.chmod(gdump, 0o755)
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
    dry_run = False                    # behaves like a device for the checks that a real dry run only reports

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
    check(n.count("dc_cvac") == 2 and n.count("dc_cvau") == 2, "dc_cvau and dc_cvac over both images")
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
    check(idx(t, "cpufreq_init") < idx(t, "smp_start_secondaries") < idx(t, "kboot_prepare_dt"),
          "L0 starts the secondaries (cpufreq_init first) before prepare_dt so the FDT keeps 8 cpus")
    check("kboot_set_chosen" not in n, "L0 sets no wel2 arguments and allocates no blocks")
    check(n.count("memalign") == 2 and n.count("writemem") == 1, "L0 memaligns two blocks and writes only the DTB")
    check("BLOCK CHECK: PASS" in out, "L0 block check passes when the blocks are inside usable RAM")
    check(n.count("readmem") == 2 and idx(t, "readmem") > idx(t, "kboot_prepare_dt"), "L0 reads the FDT back after prepare_dt")
    check(os.path.isfile(os.path.join(tmp, "out", "prepared-fdt.dtb")), "L0 saves the prepared FDT locally")
    check("platform m1 (stub)" in out, "L0 runs fdt_m1_dump on the blob")

    # --- L0 block check FAIL: heap blocks outside / straddling usable RAM -------------------------------------------
    usable_path = os.path.join(tmp, "usable.txt")
    good = open(usable_path).read()
    for label, text in (("a reserved hole over the guest block", "usable 0 base=0x800000000 size=0x44000000 end=0x844000000\n"
                         "usable 1 base=0x846000000 size=0x1ba000000 end=0xa00000000\n"),
                        ("a block straddling the end of a range", "usable 0 base=0x800000000 size=0x43000000 end=0x843000000\n")):
        open(usable_path, "w").write(text)
        t = Fake()
        rc, out = run(["--rung", "L0", "--fdt-dump", os.path.join(tmp, "fdt_m1_dump")], t, tmp)
        check(rc == 4 and "BLOCK CHECK: FAIL" in out and "FAIL (not wholly" in out, "L0 block check FAILs with %s" % label)
    open(usable_path, "w").write(good)
    ok, _ = wl.check_blocks(good, {"x": 0x840000000})
    bad, _ = wl.check_blocks("usable 0 base=0x800000000 size=0x1000 end=0x800001000\n", {"x": 0x800000000})
    check(ok and not bad, "check_blocks: inside passes, bigger than the range fails")
    ok2, _ = wl.check_blocks(good, {"x": 0x840100000})
    check(not ok2, "check_blocks: an unaligned block is refused")

    # --- guest image must leave room for 4 KB + the info block in its block ----------------------------------------
    try:
        wl.check_image("g", image(wl.BLOCK - 4096), guest=True)
        check(False, "a guest image_size leaving no room for the info block is refused")
    except wl.LoadError:
        check(True, "a guest image_size leaving no room for the info block is refused")
    try:
        wl.check_image("g", image(wl.BLOCK - 4096 - wl.GUEST_INFO_SIZE), guest=True)
        check(True, "the largest guest image that still leaves room is accepted")
    except wl.LoadError:
        check(False, "the largest guest image that still leaves room is accepted")

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

    # --- E14: --edk2, the third block, wel2.abi=fdt / wel2.gfdt=build / wel2.fb ----------------------------------------
    FD = os.path.join(tmp, "fdt_m1_dump")
    GD = os.path.join(tmp, "gfdt_dump")
    gargs = os.path.join(tmp, "gfdt-args.txt")
    edk2_size = len(files["WATARI_M1_EFI.fd"])

    def gfdt_calls():
        return open(gargs).read().splitlines() if os.path.exists(gargs) else []

    if os.path.exists(gargs):
        os.remove(gargs)
    t = Fake()
    rc, out = run(["--rung", "D2", "--edk2", "WATARI_M1_EFI.fd", "--fb", "band", "--cpuif", "trap-notc", "--go",
                   "--fdt-dump", FD, "--gfdt-dump", GD], t, tmp)
    n = names(t)
    check(rc == 0, "D2 exits 0 after the hand-over")
    check(n[0] == "malloc" and n[1] == "writemem", "D2: the DTB is allocated and written first")
    check(n.count("memalign") == 3 and n.count("writemem") == 3, "D2: three 32 MB blocks (layer, spacer, edk2), three writes (DTB, layer, edk2)")
    allocs = [c for c in t.calls if c[0] == "memalign"]
    check(all(c[1] == wl.BLOCK and c[2] == wl.BLOCK for c in allocs), "D2: blocks are 32 MB aligned and 32 MB long")
    wr = [c for c in t.calls if c[0] == "writemem"]
    edk2_addr = wr[2][1]
    layer_addr = wr[1][1]
    check(wr[1][2] == len(files["wel2-chain.bin"]) and wr[2][2] == edk2_size, "D2: the chain layer and the EDK2 image are the two blocks written")
    check(layer_addr % wl.BLOCK == 0 and edk2_addr % wl.BLOCK == 0 and layer_addr != edk2_addr, "D2: distinct aligned addresses")
    last_alloc = max(i for i, c in enumerate(t.calls) if c[0] in ("memalign", "writemem"))
    check(last_alloc < idx(t, "dc_cvau"), "D2: all allocations and copies precede the cache maintenance")
    check(n.count("dc_cvau") == 2 and n.count("ic_ivau") == 2, "D2: cache maintenance over the layer and EDK2 only")
    check(idx(t, "ic_ivau", 1) < idx(t, "kboot_set_chosen") < idx(t, "kboot_prepare_dt"), "D2: cache maintenance -> boot arguments -> prepare_dt")
    want = "wel2.cpuif=trap-notc wel2.guest=0x%x wel2.abi=fdt wel2.gfdt=build wel2.fb=band" % edk2_addr
    got = [c for c in t.calls if c[0] == "kboot_set_chosen"][0][2]
    check(got == want, "D2 boot arguments: %r" % got)
    check("wel2.step" not in got, "D2 passes no wel2.step")
    check(idx(t, "kboot_prepare_dt") < idx(t, "readmem") < idx(t, "msr_daif") < idx(t, "kboot_boot"),
          "D2: prepare_dt -> read-back of the FDT -> DAIF -> boot")
    check(n.index("kboot_boot") == len(n) - 1 and [c for c in t.calls if c[0] == "kboot_boot"][0][1] == layer_addr,
          "D2: kboot_boot(layer) is the last call")

    # --- T16g: the declared fallback firmware of the bundle is selected by name, nothing else changes -----------------------
    saved_g = open(gargs).read() if os.path.exists(gargs) else None
    for fb_name in ("WATARI_M1_EFI-e4.fd", "WATARI_M1_EFI-e1.fd"):
        t = Fake()
        rc, out = run(["--rung", "D2", "--edk2", fb_name, "--fb", "band", "--cpuif", "trap-notc", "--go", "--no-smp",
                       "--fdt-dump", FD, "--gfdt-dump", GD], t, tmp)
        wr = [c for c in t.calls if c[0] == "writemem"]
        check(rc == 0 and wr[2][2] == len(files[fb_name]), "D2 --edk2 %s: accepted as a bundle file, the fallback image is the third block" % fb_name)
        check(rc == 0 and ("edk2=%s" % fb_name) in out, "D2 --edk2 %s: the rung line names the fallback image" % fb_name)
        check(not [c for c in t.calls if c[0] == "smp_start_secondaries"], "D2 --edk2 %s --no-smp: no secondary is started by m1n1" % fb_name)
    if os.path.exists(gargs):
        os.remove(gargs)                       # the gfdt_dump call counts below are for the default run
    if saved_g is not None:
        open(gargs, "w").write(saved_g)
    check("smp_start_secondaries" not in n, "D2 starts no secondaries unless --smp")
    g = gfdt_calls()
    check(len(g) == 1 and ("--layer 0x%x" % layer_addr) in g[0] and ("--entry 0x%x" % edk2_addr) in g[0] and
          "--image-size 0x300000" in g[0] and "--fb band" in g[0], "D2 runs gfdt_dump with the layer, entry, image size and fb: %s" % g)
    check("BLOCK CHECK: PASS" in out and "GUEST FDT CHECK: PASS" in out, "D2: block and guest FDT checks pass before the jump")

    # wel2.gran=4k|16k in --args is passed on to gfdt_dump as --gran (the layer picks the granule the same way)
    for extra, want_gran in (("wel2.gran=4k", "--gran 4k"), ("wel2.gran=16k", "--gran 16k"), ("wel2.hold=1", None)):
        t3 = Fake()
        open(gargs, "w").close()
        rc, _ = run(["--rung", "D0", "--edk2", "WATARI_M1_EFI.fd", "--args", extra, "--fdt-dump", FD, "--gfdt-dump", GD], t3, tmp)
        g = gfdt_calls()
        check(rc == 0 and len(g) == 1 and (want_gran in g[0] if want_gran else "--gran" not in g[0]),
              "D0 --args %s -> gfdt_dump %s: %s" % (extra, want_gran or "no --gran", g))

    # D2 default --fb is layer and is passed as such
    t = Fake()
    rc, _ = run(["--rung", "D2", "--edk2", "WATARI_M1_EFI.fd", "--go", "--fdt-dump", FD, "--gfdt-dump", GD], t, tmp)
    a = [c for c in t.calls if c[0] == "kboot_set_chosen"][0][2]
    check(rc == 0 and a.endswith("wel2.abi=fdt wel2.gfdt=build wel2.fb=layer") and "wel2.cpuif=trap " in a, "D2 default: wel2.fb=layer is passed: %r" % a)
    t = Fake()
    rc, _ = run(["--rung", "D2", "--edk2", "WATARI_M1_EFI.fd", "--go", "--smp", "--fdt-dump", FD, "--gfdt-dump", GD], t, tmp)
    check(rc == 0 and idx(t, "kboot_set_chosen") < idx(t, "smp_start_secondaries") < idx(t, "kboot_prepare_dt"),
          "D2 --smp: set_chosen -> smp_start_secondaries -> prepare_dt")

    # D0: three blocks, nothing written but the DTB, never jumps, no boot arguments
    os.remove(gargs)
    t = Fake()
    rc, out = run(["--rung", "D0", "--edk2", "WATARI_M1_EFI.fd", "--fb", "guest", "--fdt-dump", FD, "--gfdt-dump", GD], t, tmp)
    n = names(t)
    check(rc == 0 and "kboot_boot" not in n and "msr_daif" not in n, "D0 exits 0 and never jumps")
    check(n.count("memalign") == 3 and n.count("writemem") == 1, "D0 memaligns three blocks and writes only the DTB")
    check("kboot_set_chosen" not in n, "D0 sets no wel2 arguments")
    check(all(("%-5s block" % k) in out for k in ("layer", "guest", "edk2")) and "BLOCK CHECK: PASS" in out,
          "D0 block check covers three blocks and passes")
    g = gfdt_calls()
    d0_blocks = [c[1] for c in t.calls if c[0] == "memalign"]
    check(len(g) == 1 and "--fb guest" in g[0] and "--image-size 0x300000" in g[0], "D0 runs gfdt_dump: %s" % g)
    check(os.path.isfile(os.path.join(tmp, "out", "prepared-fdt.dtb")), "D0 saves the prepared FDT locally")
    # the addresses D0 checked are the ones D2 gets (same allocation sequence)
    t2 = Fake()
    run(["--rung", "D2", "--edk2", "WATARI_M1_EFI.fd", "--go", "--fdt-dump", FD, "--gfdt-dump", GD], t2, tmp)
    check([c[1] for c in t.calls if c[0] == "writemem"][0] == [c[1] for c in t2.calls if c[0] == "writemem"][0], "D0 and D2 allocate the DTB at the same place")
    check(len(d0_blocks) == 3, "D0 allocated three blocks")

    # a heap block (the third) outside usable RAM: block check FAIL, exit 4, no jump (also D2, which reads the FDT back)
    open(usable_path, "w").write("usable 0 base=0x840000000 size=0x6000000 end=0x846000000\n")
    t = Fake()
    rc, out = run(["--rung", "D0", "--edk2", "WATARI_M1_EFI.fd", "--fdt-dump", FD, "--gfdt-dump", GD], t, tmp)
    check(rc == 4 and "BLOCK CHECK: FAIL" in out and "edk2  block" in out, "D0: the third (edk2) block outside usable RAM fails the block check")
    t = Fake()
    rc, out = run(["--rung", "D2", "--edk2", "WATARI_M1_EFI.fd", "--go", "--fdt-dump", FD, "--gfdt-dump", GD], t, tmp)
    check(rc == 4 and "kboot_boot" not in names(t), "D2: the same failure stops before the jump")
    open(usable_path, "w").write(good)

    # the layer's placement rule (gfdt_dump exit 1) stops D2; a privacy hit (exit 3) stops it too
    for code, want_rc, label in ((1, 4, "a placement refusal"), (3, 1, "a privacy self-check hit")):
        open(os.path.join(tmp, "gfdt-rc.txt"), "w").write("%d\n" % code)
        t = Fake()
        rc, out = run(["--rung", "D2", "--edk2", "WATARI_M1_EFI.fd", "--go", "--fdt-dump", FD, "--gfdt-dump", GD], t, tmp)
        check(rc == want_rc and "kboot_boot" not in names(t), "D2 stops before the jump on %s (exit %d)" % (label, rc))
    open(os.path.join(tmp, "gfdt-rc.txt"), "w").write("0\n")

    # gfdt_dump exit 2 (usage / malformed input) or a crash is not a placement verdict: stop, exit 1, say so
    for code, label in ((2, "exit 2"), (139, "a crash-like exit 139")):
        open(os.path.join(tmp, "gfdt-rc.txt"), "w").write("%d\n" % code)
        t = Fake()
        rc, out = run(["--rung", "D2", "--edk2", "WATARI_M1_EFI.fd", "--go", "--fdt-dump", FD, "--gfdt-dump", GD], t, tmp)
        check(rc == 1 and "kboot_boot" not in names(t) and "not a verdict" in out, "D2 stops before the jump on gfdt_dump %s, not called a placement refusal" % label)
    open(os.path.join(tmp, "gfdt-rc.txt"), "w").write("1\n")
    t = Fake()
    rc, out = run(["--rung", "D2", "--edk2", "WATARI_M1_EFI.fd", "--go", "--fdt-dump", FD, "--gfdt-dump", GD], t, tmp)
    check(rc == 4 and "placement rule" in out and "kboot_boot" not in names(t), "gfdt_dump exit 1 is the placement refusal (exit 4)")
    open(os.path.join(tmp, "gfdt-rc.txt"), "w").write("0\n")
    # a tool that cannot be started (OSError from subprocess) stops before the jump with a clear message
    broken = os.path.join(tmp, "broken_dump")
    open(broken, "w").write("#!/nonexistent/interpreter\n")
    os.chmod(broken, 0o755)
    for opts, label in ((["--fdt-dump", broken, "--gfdt-dump", GD], "fdt_m1_dump"), (["--fdt-dump", FD, "--gfdt-dump", broken], "gfdt_dump")):
        t = Fake()
        rc, out = run(["--rung", "D2", "--edk2", "WATARI_M1_EFI.fd", "--go"] + opts, t, tmp)
        check(rc == 1 and "NOT jumping" in out and "kboot_boot" not in names(t), "a %s that cannot start stops before the jump" % label)

    # D2 whose FDT cannot be read back must NOT jump (exit 3, hint --fdt-addr); the same for a bad magic; --fdt-addr fixes it
    t = NoConsole()
    rc, out = run(["--rung", "D2", "--edk2", "WATARI_M1_EFI.fd", "--go", "--gfdt-dump", GD], t, tmp)
    check(rc == 3 and "--fdt-addr" in out and "kboot_boot" not in names(t) and "msr_daif" not in names(t),
          "D2 without a captured FDT address does not jump (exit 3, --fdt-addr hint)")

    class BadMagic(Fake):
        def read(self, addr, n):
            super().read(addr, n)
            return b"\0" * n
    t = BadMagic()
    rc, out = run(["--rung", "D2", "--edk2", "WATARI_M1_EFI.fd", "--go", "--fdt-dump", FD, "--gfdt-dump", GD], t, tmp)
    check(rc == 3 and "no FDT magic" in out and "kboot_boot" not in names(t), "D2 with no FDT magic at the address does not jump (exit 3)")
    t = NoConsole()
    rc, _ = run(["--rung", "D2", "--edk2", "WATARI_M1_EFI.fd", "--go", "--fdt-addr", "0x840000000", "--fdt-dump", FD, "--gfdt-dump", GD], t, tmp)
    check(rc == 0 and names(t)[-1] == "kboot_boot", "D2 with --fdt-addr reads back, checks and jumps")
    t = NoConsole()
    rc, _ = run(["--rung", "D0", "--edk2", "WATARI_M1_EFI.fd"], t, tmp)
    check(rc == 3, "D0 without a captured FDT address exits 3")

    # --dry-run makes no proxy call: the real connection function is never reached, every call is only recorded
    connected = []
    real_connect = wl.connect
    wl.connect = lambda *a, **k: connected.append(a) or (_ for _ in ()).throw(AssertionError("connect called"))
    try:
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = wl.main(["--bundle", tmp, "--out", os.path.join(tmp, "out"), "--rung", "D0", "--edk2", "WATARI_M1_EFI.fd",
                          "--dry-run", "--fdt-dump", FD, "--gfdt-dump", GD])
        check(rc == 0 and not connected and "DRY RUN" in out.getvalue(), "D0 --dry-run opens no device (connect never called)")
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = wl.main(["--bundle", tmp, "--out", os.path.join(tmp, "out"), "--rung", "D2", "--edk2", "WATARI_M1_EFI.fd",
                          "--dry-run", "--fdt-dump", FD, "--gfdt-dump", GD])
        check(rc == 0 and not connected and "[proxy] kboot_boot" in out.getvalue(), "D2 --dry-run opens no device and only prints the proxy calls")
    finally:
        wl.connect = real_connect

    # refusals: no proxy call is made
    for argv, label in ((["--rung", "D2", "--go"], "D2 without --edk2"),
                        (["--rung", "D0"], "D0 without --edk2"),
                        (["--rung", "L3", "--go", "--edk2", "WATARI_M1_EFI.fd"], "--edk2 with L3"),
                        (["--rung", "D2", "--go", "--edk2", "/etc/hosts"], "--edk2 outside the bundle"),
                        (["--rung", "D2", "--go", "--edk2", "../WATARI_M1_EFI.fd"], "--edk2 with ..")):
        t = Fake()
        rc, out = run(argv, t, tmp)
        check(rc == 2 and t.calls == [], "%s is refused (exit 2) before any proxy call" % label)

    # an EDK2 image that is not listed in SHA256SUMS, or that was changed, is refused before any proxy call
    sums = open(os.path.join(tmp, "SHA256SUMS")).read()
    open(os.path.join(tmp, "SHA256SUMS"), "w").write("".join(l for l in sums.splitlines(True) if "WATARI_M1_EFI.fd" not in l))
    t = Fake()
    rc, out = run(["--rung", "D2", "--go", "--edk2", "WATARI_M1_EFI.fd"], t, tmp)
    check(rc == 2 and t.calls == [] and "not listed" in out, "an unlisted EDK2 image is refused")
    open(os.path.join(tmp, "SHA256SUMS"), "w").write(sums)
    efi = os.path.join(tmp, "WATARI_M1_EFI.fd")
    orig_efi = open(efi, "rb").read()
    open(efi, "ab").write(b"x")
    t = Fake()
    rc, out = run(["--rung", "D2", "--go", "--edk2", "WATARI_M1_EFI.fd"], t, tmp)
    check(rc == 2 and t.calls == [] and "mismatch" in out, "a modified EDK2 image is refused (SHA-256)")
    open(efi, "wb").write(orig_efi)
    open(efi, "wb").write(image(wl.BLOCK + 16))
    t = Fake()
    sums_save = open(os.path.join(tmp, "SHA256SUMS")).read()
    open(os.path.join(tmp, "SHA256SUMS"), "w").write("".join(
        (hashlib.sha256(open(efi, "rb").read()).hexdigest() + "  WATARI_M1_EFI.fd\n") if "WATARI_M1_EFI.fd" in l else l
        for l in sums_save.splitlines(True)))
    rc, out = run(["--rung", "D2", "--go", "--edk2", "WATARI_M1_EFI.fd"], t, tmp)
    check(rc == 1 and t.calls == [] and "32 MB block" in out, "an EDK2 image larger than its 32 MB block is refused")
    open(efi, "wb").write(orig_efi)
    open(os.path.join(tmp, "SHA256SUMS"), "w").write(sums)
    t = Fake()
    rc, _ = run(["--rung", "D2", "--go", "--edk2", "WATARI_M1_EFI.fd", "--fdt-dump", FD, "--gfdt-dump", GD], t, tmp)
    check(rc == 0, "the bundle is back to a good state")

    # wel2.fb on the non-EDK2 rungs: only when it is not the default
    t = Fake()
    run(["--rung", "L3", "--go", "--fb", "band"], t, tmp)
    a = [c for c in t.calls if c[0] == "kboot_set_chosen"][0][2]
    check(a.endswith(" wel2.fb=band") and "wel2.abi" not in a, "L3 --fb band appends wel2.fb=band only: %r" % a)
    check(wl.build_bootargs(3, "trap", 0x840000000, "", edk2=False, fb="layer") == "wel2.step=3 wel2.cpuif=trap wel2.guest=0x840000000",
          "build_bootargs: the default fb adds nothing on the test-guest rungs")

    # --- T23: the fourth block, the Windows RAM-disk image (--ramdisk), the rate test (--ramdisk-test) -----------------------
    MIB = wl.MIB
    RD_BYTES = 40 * MIB + 12345                      # not a multiple of the 16 MiB slice: 16 + 16 + 8 MiB + 12345
    rd_path = os.path.join(tmp, "winram-test.img")
    with open(rd_path, "wb") as f:
        for i in range(0, RD_BYTES, MIB):
            f.write(wl.test_pattern(i // MIB, min(MIB, RD_BYTES - i)))
    rd_data = open(rd_path, "rb").read()
    rd_sha = hashlib.sha256(rd_data).hexdigest()
    rd_crc = zlib.crc32(rd_data) & 0xffffffff

    def write_sidecar(sha):
        with open(rd_path + ".sha256", "w") as f:
            f.write("%s  winram-test.img\n" % sha)
    write_sidecar(rd_sha)
    RD_RANGE = ((RD_BYTES + 4096 + MIB - 1) // MIB) * MIB
    check(wl.ramdisk_range_size(RD_BYTES) == RD_RANGE and RD_RANGE % 16384 == 0 and RD_RANGE - RD_BYTES >= 4096,
          "ramdisk range = image + 4 KiB trailer rounded to 1 MiB (16 KB aligned): %d" % RD_RANGE)

    GD2 = os.path.join(tmp, "gfdt_dump_rd")          # a gfdt_dump that "knows" --ramdisk (the option string is in the file)
    with open(GD2, "w") as f:
        f.write("#!/bin/sh\n# supports --ramdisk <base>,<size>\necho \"$@\" >> \"$(dirname \"$0\")/gfdt-args.txt\"\n"
                "echo 'guest tree (stub)'\nexit 0\n")
    os.chmod(GD2, 0o755)
    open(os.path.join(tmp, "gfdt-rc.txt"), "w").write("0\n")
    if os.path.exists(gargs):
        os.remove(gargs)

    RD_ARGS = ["--edk2", "WATARI_M1_EFI.fd", "--ramdisk", rd_path, "--fdt-dump", FD]
    t = Fake()
    rc, out = run(["--rung", "D2", "--go", "--gfdt-dump", GD2] + RD_ARGS, t, tmp)
    n = names(t)
    check(rc == 0 and n[-1] == "kboot_boot", "D2 --ramdisk: exit 0 and the jump is the last call")
    allocs = [c for c in t.calls if c[0] == "memalign"]
    check(len(allocs) == 4 and allocs[0][1] == wl.BLOCK and allocs[0][2] == RD_RANGE and all(c[1] == wl.BLOCK for c in allocs),
          "D2 --ramdisk: four 32 MB aligned blocks, the FIRST is the ramdisk of %d bytes" % RD_RANGE)
    check(n[0] == "malloc" and idx(t, "memalign", 0) == 3 and t.calls[2][0] == "timeout", "D2 --ramdisk: DTB written, the long timeout set, then the ramdisk block is the first memalign")
    wr = [c for c in t.calls if c[0] == "writemem"]
    rd_base = wr[1][1]                                  # writes: DTB, then the first ramdisk slice
    sl = [c for c in wr if c[1] >= rd_base and c[1] < rd_base + RD_RANGE]
    check([c[2] for c in sl[:3]] == [16 * MIB, 16 * MIB, 8 * MIB + 12345] and [c[1] - rd_base for c in sl[:3]] == [0, 16 * MIB, 32 * MIB],
          "D2 --ramdisk: three slices 16 + 16 + (8 MiB + 12345) at 0 / 16 / 32 MiB (the boundary slice is short)")
    check(len(sl) == 4 and sl[3][1] == rd_base + RD_RANGE - 4096 and sl[3][2] == 4096, "D2 --ramdisk: the 4 KiB trailer is the LAST ramdisk write, at the end of the range")
    tr = t.mem[sl[3][1]]
    check(tr[:8] == b"WRAMDSK1" and struct.unpack_from("<QI", tr, 8) == (RD_BYTES, rd_crc) and tr[20:52] == bytes.fromhex(rd_sha)
          and tr[52:] == b"\0" * (4096 - 52), "trailer: magic, image_bytes, CRC-32 (zlib), SHA-256, zero padding")
    check(b"".join(t.mem[c[1]] for c in sl[:3]) == rd_data, "the three slices carry the image bytes exactly")
    first_other = min(i for i, c in enumerate(t.calls) if c[0] == "memalign" and c is not allocs[0])
    check(max(i for i, c in enumerate(t.calls) if c[0] == "writemem" and c[1] in [x[1] for x in sl]) < first_other,
          "the ramdisk is fully written (trailer last) before the layer block is allocated")
    def covered(op):
        return sorted((c[1], c[2]) for c in t.calls if c[0] == op and rd_base <= c[1] < rd_base + RD_RANGE)
    def contiguous(spans):
        pos = rd_base
        for a, n in spans:
            if a != pos or n > 32 * MIB:
                return False
            pos += n
        return pos == rd_base + RD_RANGE
    check(all(contiguous(covered(op)) for op in ("dc_cvau", "dc_cvac", "ic_ivau")) and len([c for c in t.calls if c[0] == "dc_cvau"]) >= 3,
          "cache maintenance over the WHOLE ramdisk range (and the layer, EDK2)")
    check(idx(t, "ic_ivau", 2) < idx(t, "kboot_set_chosen"), "cache maintenance precedes the boot arguments")
    got = [c for c in t.calls if c[0] == "kboot_set_chosen"][0][2]
    edk2_a = [c for c in wr if c[2] == edk2_size][0][1]
    check("wel2.guest=0x%x wel2.ramdisk=0x%x,0x%x wel2.abi=fdt" % (edk2_a, rd_base, RD_RANGE) in got, "boot arguments carry wel2.ramdisk next to wel2.guest: %r" % got)
    g = gfdt_calls()
    check(len(g) == 1 and ("--ramdisk 0x%x,0x%x" % (rd_base, RD_RANGE)) in g[0], "gfdt_dump gets --ramdisk <base>,<size>: %s" % g)
    check("RAMDISK PLAN" in out and "3 slices of 16 MiB" in out and "ETA" in out and "MB/s" in out and "RAMDISK block 0x%x" % rd_base in out,
          "the plan, per-slice progress lines (MB/s, ETA) and the placement of the block are printed")
    check("ramdisk block 0x%x..0x%x : PASS" % (rd_base, rd_base + RD_RANGE) in out, "block check covers the whole ramdisk range and passes")

    # a gfdt_dump without --ramdisk: the ramdisk part of the preview is skipped with a clear note, the rest still runs
    open(gargs, "w").close()
    t = Fake()
    rc, out = run(["--rung", "D0", "--gfdt-dump", GD] + RD_ARGS, t, tmp)
    g = gfdt_calls()
    check(rc == 0 and "no --ramdisk option" in out and len(g) == 1 and "--ramdisk" not in g[0],
          "gfdt_dump without --ramdisk: skipped with a NOTE, the EDK2 placement is still checked")

    # D0: the image is written (read-back sample), no jump, no boot arguments, four blocks checked
    t = Fake()
    rc, out = run(["--rung", "D0", "--gfdt-dump", GD2] + RD_ARGS, t, tmp)
    n = names(t)
    check(rc == 0 and "kboot_boot" not in n and "kboot_set_chosen" not in n, "D0 --ramdisk: no jump, no boot arguments")
    check(n.count("memalign") == 4 and sum(1 for c in t.calls if c[0] == "writemem") == 1 + 4, "D0 --ramdisk: four blocks, the DTB and the ramdisk (3 slices + trailer) written")
    check(all(("%-5s block" % k) in out for k in ("ramdisk", "layer", "guest", "edk2")) and "BLOCK CHECK: PASS" in out, "D0 --ramdisk: four blocks in the placement check")
    check(sum(1 for c in t.calls if c[0] == "readmem") >= 6 + 2, "D0 --ramdisk: first and last 4 KiB of every slice read back")

    # bad SHA / no sidecar / not a file / --ramdisk on a wrong rung: refused (exit 2) before ANY proxy call
    write_sidecar("0" * 64)
    t = Fake()
    rc, out = run(["--rung", "D2", "--go", "--gfdt-dump", GD2] + RD_ARGS, t, tmp)
    check(rc == 2 and t.calls == [] and "differs from its sidecar" in out, "a sidecar mismatch refuses the image before any proxy call")
    write_sidecar(rd_sha)
    os.rename(rd_path + ".sha256", rd_path + ".sha256.off")
    t = Fake()
    rc, out = run(["--rung", "D2", "--go"] + RD_ARGS, t, tmp)
    check(rc == 2 and t.calls == [] and "no sidecar" in out, "a missing sidecar refuses the image before any proxy call")
    os.rename(rd_path + ".sha256.off", rd_path + ".sha256")
    with open(rd_path, "r+b") as f:
        f.seek(RD_BYTES - 1)
        f.write(b"\xff")
    t = Fake()
    rc, out = run(["--rung", "D2", "--go"] + RD_ARGS, t, tmp)
    check(rc == 2 and t.calls == [], "a flipped last byte of the image fails the SHA-256 (exit 2, no proxy call)")
    with open(rd_path, "wb") as f:
        f.write(rd_data)
    for argv, label in ((["--rung", "D2", "--go", "--ramdisk", rd_path], "--ramdisk without --edk2"),
                        (["--rung", "L3", "--go", "--ramdisk", rd_path], "--ramdisk with L3"),
                        (["--rung", "D2", "--go", "--edk2", "WATARI_M1_EFI.fd", "--ramdisk", rd_path + ".nope"], "--ramdisk of a missing file"),
                        (["--rung", "D2", "--go", "--edk2", "WATARI_M1_EFI.fd", "--ramdisk", rd_path, "--ramdisk-slice", "0"], "--ramdisk-slice 0")):
        t = Fake()
        rc, out = run(argv, t, tmp)
        check(rc == 2 and t.calls == [], "%s is refused (exit 2) before any proxy call" % label)

    # too big: the 896 MiB limit counts the block (image + trailer rounded to 1 MiB); sparse files, no hashing needed to refuse
    ok_n = 896 * MIB - 4096
    check(wl.check_ramdisk_size(ok_n) == 896 * MIB and wl.heap_need(896 * MIB) == wl.HEAP_BYTES, "the largest image (896 MiB - 4 KiB) fits the 1 GiB heap budget exactly")
    for bad_n in (896 * MIB - 4095, 897 * MIB, 2 * 1024 * MIB):
        try:
            wl.check_ramdisk_size(bad_n)
            check(False, "an image of %d bytes is refused" % bad_n)
        except wl.LoadError as e:
            check(e.code == 2, "an image of %.3f MiB is refused (exit 2)" % (bad_n / MIB))
    big = os.path.join(tmp, "big.img")
    with open(big, "wb") as f:
        f.truncate(900 * MIB)
    open(big + ".sha256", "w").write("%s  big.img\n" % ("1" * 64))
    t = Fake()
    rc, out = run(["--rung", "D2", "--go", "--edk2", "WATARI_M1_EFI.fd", "--ramdisk", big], t, tmp)
    check(rc == 2 and t.calls == [] and "larger than the 896 MiB limit" in out, "a 900 MiB image is refused before any proxy call")
    os.remove(big)
    os.remove(big + ".sha256")

    # overlap and range checks over the WHOLE ramdisk range (check_blocks)
    U = "usable 0 base=0x800000000 size=0x100000000 end=0x900000000\n"
    rdb = (0x840000000, 0x2ca00000, wl.RD_ALIGN)
    ok_, ln = wl.check_blocks(U, {"ramdisk": rdb, "layer": 0x870000000}, [("prepared FDT", 0x830000000, 0x830010000)])
    check(ok_, "check_blocks: a ramdisk range clear of the layer, FDT passes")
    ok_, ln = wl.check_blocks(U, {"ramdisk": rdb, "layer": 0x860000000})
    check(not ok_ and "overlaps the layer block" in " ".join(ln), "check_blocks: the layer block inside the ramdisk range is an overlap")
    ok_, ln = wl.check_blocks(U, {"ramdisk": rdb, "edk2": 0x840000000})
    check(not ok_ and "overlaps the ramdisk block" in " ".join(ln), "check_blocks: overlap is reported on both blocks")
    ok_, ln = wl.check_blocks(U, {"ramdisk": rdb}, [("prepared FDT", 0x850000000, 0x850010000)])
    check(not ok_ and "overlaps the prepared FDT" in " ".join(ln), "check_blocks: the prepared FDT inside the range is refused")
    ok_, ln = wl.check_blocks(U, {"ramdisk": rdb}, [("framebuffer", 0x86c000000, 0x872000000)])
    check(not ok_ and "overlaps the framebuffer" in " ".join(ln), "check_blocks: the framebuffer overlapping the end is refused")
    ok_, ln = wl.check_blocks("usable 0 base=0x800000000 size=0x50000000 end=0x850000000\n", {"ramdisk": rdb})
    check(not ok_ and "not wholly inside" in " ".join(ln), "check_blocks: a range that leaves the usable RAM after its first 32 MB fails (whole range, not 32 MB)")
    ok_, ln = wl.check_blocks(U, {"ramdisk": (0x840000000 + 4096, 0x2ca00000, wl.RD_ALIGN)})
    check(not ok_ and "KB aligned" in " ".join(ln), "check_blocks: a ramdisk base that is not 16 KB aligned is refused")
    check(wl.parse_fb_range("framebuffer base=0x10000000 2560x1600 stride=10240 format=x2r10g10b10\n") == (0x10000000, 0x10000000 + 1600 * 10240), "parse_fb_range")
    # at CLI level: a usable range that ends inside the ramdisk
    open(usable_path, "w").write("usable 0 base=0x800000000 size=0x44000000 end=0x844000000\n")
    t = Fake()
    rc, out = run(["--rung", "D2", "--go", "--gfdt-dump", GD2] + RD_ARGS, t, tmp)
    check(rc == 4 and "BLOCK CHECK: FAIL" in out and "kboot_boot" not in names(t) and "msr_daif" not in names(t),
          "D2: a ramdisk that leaves usable RAM fails the block check and does not jump (exit 4)")
    open(usable_path, "w").write(good)

    # slices: a checksum error retries THAT slice once; twice aborts; a read-back mismatch aborts
    class FlakyOnce(Fake):
        fails = 1

        def write(self, addr, data):
            if len(data) == 16 * MIB and addr % (16 * MIB) == 0 and getattr(self, "seen", 0) == 1 and self.fails:
                self.fails -= 1
                self._log("writemem-csumerr", addr, len(data))
                raise wl.ChecksumError("bad checksum")
            if len(data) == 16 * MIB:
                self.seen = getattr(self, "seen", 0) + 1
            super().write(addr, data)
    t = FlakyOnce()
    rc, out = run(["--rung", "D2", "--go", "--gfdt-dump", GD2] + RD_ARGS, t, tmp)
    check(rc == 0 and "retrying this slice once" in out and names(t).count("writemem-csumerr") == 1, "a slice with one checksum error is retried once and the run continues")
    t = FlakyOnce()
    t.fails = 2
    rc, out = run(["--rung", "D2", "--go", "--gfdt-dump", GD2] + RD_ARGS, t, tmp)
    check(rc == 1 and "checksum error twice" in out and "kboot_set_chosen" not in names(t) and "kboot_boot" not in names(t),
          "a slice that fails twice aborts before the boot arguments and the jump")

    class Corrupt(Fake):
        def read(self, addr, n):
            d = super().read(addr, n)
            return b"\x00" + d[1:] if n == 4096 and d[:1] != b"\x00" else d
    t = Corrupt()
    rc, out = run(["--rung", "D2", "--go", "--gfdt-dump", GD2] + RD_ARGS, t, tmp)
    check(rc == 1 and "read-back differs" in out and "kboot_boot" not in names(t), "a read-back mismatch aborts the transfer (no jump)")
    t = Fake()
    rc, out = run(["--rung", "D2", "--go", "--gfdt-dump", GD2, "--ramdisk-verify", "full"] + RD_ARGS, t, tmp)
    check(rc == 0 and sum(1 for c in t.calls if c[0] == "readmem" and c[2] > 4096) == 3, "--ramdisk-verify full reads every slice back whole")
    t = Fake()
    rc, out = run(["--rung", "D2", "--go", "--gfdt-dump", GD2, "--ramdisk-verify", "none"] + RD_ARGS, t, tmp)
    check(rc == 0 and not any(c[0] == "readmem" and c[2] == 16 * MIB for c in t.calls) and
          sum(1 for c in t.calls if c[0] == "readmem" and c[2] == 4096) == 1, "--ramdisk-verify none reads back only the trailer")
    t = Fake()
    rc, out = run(["--rung", "D2", "--go", "--no-data-csums", "--gfdt-dump", GD2] + RD_ARGS, t, tmp)
    check(rc == 0 and idx(t, "disable_data_csums") < idx(t, "memalign"), "--no-data-csums is applied before the first allocation")

    # write_sliced directly: progress text, ETA, slice sizes
    class Clock:
        def __init__(self):
            self.t = 0.0

        def __call__(self):
            self.t += 2.0
            return self.t
    t = Fake()
    lines = []
    el = wl.write_sliced(t, 0x1000000, lambda off, n: bytes(n), 5 * MIB, 2 * MIB, "x", "sample", Clock(), lines.append)
    check([c[2] for c in t.calls if c[0] == "writemem"] == [2 * MIB, 2 * MIB, MIB] and len(lines) == 3 and "slice 3/3" in lines[-1] and "ETA 0 s" in lines[-1],
          "write_sliced: slices 2+2+1 MiB, one progress line each, ETA reaches 0: %s" % lines[-1])
    check("%.2f MB/s" % (2 * MIB / 2.0 / 1e6) in lines[0], "write_sliced: the rate is decimal MB/s over elapsed time: %s" % lines[0])

    # build_bootargs: the ramdisk argument sits after wel2.guest
    check(wl.build_bootargs(None, "trap", 0x850000000, "", edk2=True, fb="band", ramdisk=(0x840000000, 0x2ca00000)) ==
          "wel2.cpuif=trap wel2.guest=0x850000000 wel2.ramdisk=0x840000000,0x2ca00000 wel2.abi=fdt wel2.gfdt=build wel2.fb=band", "build_bootargs with a ramdisk")
    check("ramdisk" not in wl.build_bootargs(None, "trap", 0x850000000, "", edk2=True, fb="band"), "build_bootargs without a ramdisk is unchanged")
    check(wl.build_trailer(rd_data) == wl.build_trailer(wl.inspect_ramdisk(rd_path)) and len(wl.build_trailer(b"abc")) == 4096,
          "build_trailer(bytes) and build_trailer(RamdiskImage) agree, 4096 bytes")

    # --dry-run with a ramdisk: no device, the plan, the arguments, the slices printed
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        rc = wl.main(["--bundle", tmp, "--out", os.path.join(tmp, "out"), "--rung", "D2", "--dry-run", "--gfdt-dump", GD2] + RD_ARGS)
    o = out.getvalue()
    check(rc == 0 and "RAMDISK PLAN" in o and "wel2.ramdisk=0x" in o and o.count("[proxy] writemem") == 1 + 3 + 1 + 1 + 1 - 0 and not connected,
          "D2 --dry-run --ramdisk prints the plan and the arguments without a device")
    check(rc == 0 and "not compared" in o and "3 slices of 16 MiB" in o, "dry run: slices planned, read-back reported as not compared")

    # --ramdisk-test: its own mode: one block, write + read back, MB/s; no DTB, no layer, no jump, no write outside the block
    t = Fake()
    rc, out = run(["--ramdisk-test", "40"], t, tmp)
    n = names(t)
    blk = [c for c in t.calls if c[0] == "memalign"]
    wr = [c for c in t.calls if c[0] == "writemem"]
    check(rc == 0 and "RATE TEST: write" in out and "read-back identical" in out, "rate test: exit 0, MB/s printed, read-back identical")
    check(len(blk) == 1 and n.count("malloc") == 0 and not any(x.startswith("kboot") or x in ("msr_daif", "smp_start_secondaries", "cpufreq_init") for x in n),
          "rate test: one block, no DTB, no kboot_* call, no secondaries")
    check([c[2] for c in wr] == [16 * MIB, 16 * MIB, 8 * MIB], "rate test: 16 + 16 + 8 MiB slices")
    base = min(c[1] for c in wr)
    check(all(base <= c[1] and c[1] + c[2] <= base + 40 * MIB for c in wr) and base % wl.BLOCK == 0, "rate test: no write outside its own 40 MiB block")
    check(sum(1 for c in t.calls if c[0] == "readmem") == 3, "rate test: every slice read back")
    t = Fake()
    rc, out = run(["--ramdisk-test", "20", "--ramdisk-slice", "4", "--no-data-csums"], t, tmp)
    check(rc == 0 and sum(1 for c in t.calls if c[0] == "writemem") == 5 and "disable_data_csums" in names(t), "rate test: --ramdisk-slice and --no-data-csums")

    class Rot(Fake):
        def read(self, addr, n):
            d = super().read(addr, n)
            return d[:-1] + b"\x01" if n > 4096 else d
    t = Rot()
    rc, out = run(["--ramdisk-test", "20"], t, tmp)
    check(rc == 1 and "RATE TEST: FAIL" in out, "rate test: a read-back difference is a failure (exit 1)")
    for argv, label in ((["--ramdisk-test", "0"], "0 MiB"), (["--ramdisk-test", "5000"], "5000 MiB")):
        t = Fake()
        rc, out = run(argv, t, tmp)
        check(rc == 2 and t.calls == [], "rate test of %s is refused before any proxy call" % label)
    for argv, label in ((["--ramdisk-test", "8", "--rung", "L0"], "with --rung"), (["--ramdisk-test", "8", "--ramdisk", rd_path], "with --ramdisk")):
        try:
            with contextlib.redirect_stderr(io.StringIO()):
                wl.main(["--bundle", tmp] + argv, target=Fake())
            check(False, "--ramdisk-test %s is a usage error" % label)
        except SystemExit as e:
            check(e.code == 2, "--ramdisk-test %s is a usage error" % label)
    try:
        with contextlib.redirect_stderr(io.StringIO()):
            wl.main(["--bundle", tmp], target=Fake())
        check(False, "no --rung and no --ramdisk-test is a usage error")
    except SystemExit as e:
        check(e.code == 2, "no --rung and no --ramdisk-test is a usage error")
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        rc = wl.main(["--bundle", tmp, "--ramdisk-test", "20", "--dry-run"])
    check(rc == 0 and "[proxy] memalign" in out.getvalue() and "not compared" in out.getvalue(), "rate test --dry-run opens no device")

    # --- review fixes: the sidecar check precedes the first proxy request (connect() already sends some) -------------------
    order = []
    real_connect2 = wl.connect
    def fake_connect(*a, **k):
        order.append("connect")
        raise wl.LoadError("stop here (test)", 2)
    wl.connect = fake_connect
    try:
        for sha, label in (("0" * 64, "a wrong sidecar"), (rd_sha, "a good sidecar")):
            write_sidecar(sha)
            order.clear()
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                rc = wl.main(["--bundle", tmp, "--out", os.path.join(tmp, "out"), "--rung", "D2", "--go", "--m1n1", "/x"] + RD_ARGS)
            if sha != rd_sha:
                check(rc == 2 and order == [] and "differs from its sidecar" in out.getvalue(), "%s: refused BEFORE connect() opens the proxy" % label)
            else:
                check(order == ["connect"], "%s: the check passes, then (and only then) connect() is called" % label)
        write_sidecar(rd_sha)
        out = io.StringIO()
        order.clear()
        with contextlib.redirect_stdout(out):
            rc = wl.main(["--bundle", tmp, "--ramdisk-test", "5000", "--m1n1", "/x", "--go"])
        check(rc == 2 and order == [], "an out-of-range rate test is refused before connect()")
    finally:
        wl.connect = real_connect2

    # M1n1Target: what v1.6.1 proxy.py raises for a bad write maps onto ChecksumError; other remote errors do not
    class UartRemoteError(RuntimeError):
        pass
    class UartChecksumError(RuntimeError):
        pass
    class Iface:
        def __init__(self):
            self.cmd, self.unkhandler, self.enabled_features, self.exc, self.n = (lambda *a, **k: None), None, 0, None, 0
        def writemem(self, a, d, progress=False):
            self.n += 1
            if self.exc and self.n <= self.fail:
                raise self.exc
        def readmem(self, a, n):
            raise UartChecksumError("Reply data checksum error")
    ifc = Iface()
    tg = wl.M1n1Target(ifc, None, None, (OSError,), None, (UartChecksumError, UartRemoteError), 1)
    ifc.exc, ifc.fail = UartRemoteError("Reply error: Data checksum failed"), 1
    lines = []
    wl.write_sliced(tg, 0x1000, lambda off, n: bytes(n), 8, 8, "x", "none", out=lines.append)
    check(ifc.n == 2 and any("retrying" in l for l in lines), "real target: UartRemoteError 'Data checksum failed' is retried once as a checksum error")
    ifc.n, ifc.exc, ifc.fail = 0, UartRemoteError("Reply error: Invalid argument"), 1
    try:
        wl.write_sliced(tg, 0x1000, lambda off, n: bytes(n), 8, 8, "x", "none", out=lines.append)
        check(False, "an 'Invalid argument' remote error is not retried")
    except UartRemoteError:
        check(ifc.n == 1, "an 'Invalid argument' remote error is not retried")
    try:
        tg.read(0, 4)
        check(False, "read-back checksum error is a LoadError")
    except wl.LoadError:
        check(True, "a read-back checksum error is a LoadError, not a traceback")
    try:
        tg.disable_data_csums()
        check(False, "--no-data-csums refuses when the handshake did not enable the feature")
    except wl.LoadError as e:
        check(e.code == 2, "--no-data-csums refuses (exit 2) when the handshake did not enable the feature")
    ifc.enabled_features = 1
    tg.disable_data_csums()
    check(True, "--no-data-csums passes only when iface.enabled_features already has the flag")

    # the file changing after the sidecar check is caught before the trailer
    rdi = wl.inspect_ramdisk(rd_path)
    rdi.read(0, 100)
    try:
        rdi.check_streamed()
        check(False, "a partly read image fails check_streamed")
    except wl.LoadError:
        check(True, "check_streamed refuses a transfer that did not hash to the sidecar value")
    rdi = wl.inspect_ramdisk(rd_path)
    for off in range(0, RD_BYTES, 16 * MIB):
        rdi.read(off, min(16 * MIB, RD_BYTES - off))
    rdi.check_streamed()
    check(True, "check_streamed accepts a complete sequential read")

    # --- jump rungs need --go on a real device; refused before any m1n1 import ----------------------------------------
    rc, out = run(["--rung", "L1", "--m1n1", "/nonexistent"], None, tmp)
    check(rc == 2 and "--go" in out, "L1 without --go is refused")

    # --- a file the loader reads must be listed in SHA256SUMS ------------------------------------------------------
    sums = open(os.path.join(tmp, "SHA256SUMS")).read()
    open(os.path.join(tmp, "SHA256SUMS"), "w").write("".join(l for l in sums.splitlines(True) if "m1guest.bin" not in l))
    t = Fake()
    rc, out = run(["--rung", "L3", "--go"], t, tmp)
    check(rc == 2 and t.calls == [] and "not listed" in out, "an unlisted file is refused")
    open(os.path.join(tmp, "SHA256SUMS"), "w").write(sums)

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
