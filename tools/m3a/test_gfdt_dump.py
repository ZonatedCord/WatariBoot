#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Test of gfdt_dump (PLAN E14): the guest FDT the layer would build from an m1n1-prepared FDT, on the j293 fixture of T6b
(el2/tests/fx_j293.h, which carries a serial number and a MAC address on purpose). Run: python3 -I tools/m3a/test_gfdt_dump.py
Builds the tool and the fixture with the host cc and make into a scratch folder (no network, no downloads, nothing is
written in the repository except el2/build/, which `make -C el2 test` also uses). Asserts: no Apple node, no identifying
property or value in the output, the tree the layer promises (memory, psci, GICv3, timer, 8 cpus, chosen/watari,el2), the
placement rule, and the privacy self-check of the tool itself."""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
EL2 = os.path.join(REPO, "el2")
FAILED = []


def check(cond, msg):
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        FAILED.append(msg)


def sh(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


with tempfile.TemporaryDirectory() as tmp:
    # the fixture: test_fdt_m1 writes build/fx_j293.dtb (cwd = el2)
    r = sh(["make", "-s", "-C", EL2, "build/test_fdt_m1"])
    check(r.returncode == 0, "the T6b test binary builds: %s" % r.stderr[-200:])
    r = sh(["./build/test_fdt_m1"], cwd=EL2)
    fx = os.path.join(EL2, "build", "fx_j293.dtb")
    check(r.returncode == 0 and os.path.isfile(fx), "the j293 fixture is written by test_fdt_m1")
    raw = open(fx, "rb").read()
    check(b"serial-number" in raw and b"WATARI-SECRET-SN" in raw and b"SECRET-MAC" in raw and b"apple,aic" in raw,
          "the fixture carries the identifying properties and Apple nodes the output must not")

    tool = os.path.join(tmp, "gfdt_dump")
    srcs = [os.path.join(HERE, "gfdt_dump.c")] + [os.path.join(EL2, p) for p in (
        "boot/fdt_m1.c", "boot/fdt.c", "vgic/fb.c", "vgic/ram_map.c", "vgic/fdt_build.c", "vgic/maps.c", "vgic/pt.c")]
    r = sh(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-I" + os.path.join(EL2, "include"),
            "-I" + os.path.join(EL2, "boot")] + srcs + ["-o", tool])
    check(r.returncode == 0, "gfdt_dump builds warning-free: %s" % r.stderr[-300:])

    # the fixture: usable[] = 0x800004000..0x807ff0000 (rounds to 64 MB), a 32 MB piece, 0x812100000..0x820000000 (192 MB)
    out_dtb = os.path.join(tmp, "g.dtb")
    r = sh([tool, fx, "--layer", "0x802000000", "--entry", "0x814000000", "--image-size", "0x800000", "--fb", "guest", "-o", out_dtb])
    out = r.stdout
    check(r.returncode == 0, "gfdt_dump exits 0 on the j293 fixture: %s" % r.stderr[-300:])
    for pat, what in ((r"^  memory@814000000:", "the entry's range is the first /memory node"),
                      (r"^  psci:", "psci"), (r"^  intc@f00000000:", "the GICv3 node at 0xF00000000"),
                      (r"reg = <0xf 0x0 0x0 0x10000 0xf 0x10000000 0x0 0x100000>", "GICD and eight GICR frames"),
                      (r"^  timer:", "timer"), (r"^  cpus:", "cpus"), (r"^    framebuffer@9e2570000:", "framebuffer node (wel2.fb=guest)"),
                      (r'format = "x2r10g10b10"', "framebuffer format"), (r"stride = <0x2800>", "stride 10240"),
                      (r"^    watari,el2:", "chosen/watari,el2")):
        check(re.search(pat, out, re.M) is not None, "output has %s" % what)
    check(len(re.findall(r"^    cpu@", out, re.M)) == 8, "eight cpu nodes")
    first_mem = re.findall(r"^  memory@([0-9a-f]+):", out, re.M)
    check(first_mem and first_mem[0] == "814000000" and len(first_mem) == 3, "three /memory nodes, the EDK2 block's range first: %s" % first_mem)
    # the layer's 32 MB block (0x802000000) is cut out of the first usable range
    check("memory@802000000" not in out and "memory@804000000" in out, "the layer's block is cut out of the guest RAM")
    check("// guest image at 0x814000000" in out, "the placement line is printed")

    # privacy: no Apple node, no identifying property, no value from the fixture, in the text and in the blob
    low = out.lower()
    for bad in ("apple", "serial", "mac-address", "local-mac", "secret", "seed", "spin-table", "release", "aic", "wdt", "uart"):
        check(bad not in low, "the output has no %r" % bad)
    blob = open(out_dtb, "rb").read()
    for bad in (b"apple", b"serial", b"mac", b"SECRET", b"seed", b"aic"):
        check(bad not in blob.lower() if bad.islower() else bad.lower() not in blob.lower(), "the guest FDT blob has no %r" % bad)
    if shutil.which("dtc"):
        d = sh(["dtc", "-q", "-I", "dtb", "-O", "dts", out_dtb])
        check(d.returncode == 0 and "memory@814000000" in d.stdout and "apple" not in d.stdout.lower(), "dtc reads the guest FDT back")

    # the other framebuffer modes and the no-framebuffer default
    r = sh([tool, fx, "--layer", "0x802000000", "--entry", "0x814000000", "--image-size", "0x800000"])
    check(r.returncode == 0 and "framebuffer@" not in r.stdout and 'fb-mode = "none"' in r.stdout, "default --fb layer: no framebuffer node, fb-mode none")
    r = sh([tool, fx, "--layer", "0x802000000", "--entry", "0x814000000", "--image-size", "0x800000", "--fb", "band"])
    o = r.stdout
    check(r.returncode == 0 and 'fb-mode = "band"' in o, "--fb band: fb-mode band")
    check("mode band, band rows 360, guest rows 1240, guest base 0x9e28f4000, guest bytes 0xc20000" .replace("0xc20000", "0x%x" % (1240 * 10240)) in o,
          "--fb band on 2560x1600 stride 10240 (16 KB granule): band 360 rows, guest 1240 rows, as wel2_fb_plan")
    check(re.search(r"^    framebuffer@9e28f4000:", o, re.M) and "height = <0x4d8>" in o and "width = <0xa00>" in o and
          "reg = <0x9 0xe28f4000 0x0 0x%x>" % (1240 * 10240) in o, "the band node starts below the band: base, height 1240, size")
    r = sh([tool, fx, "--layer", "0x802000000", "--entry", "0x814000000", "--image-size", "0x800000", "--fb", "band", "--gran", "4k"])
    check("band rows 360, guest rows 1240" in r.stdout, "--gran 4k: the same 360 rows for stride 10240")
    r = sh([tool, fx, "--layer", "0x802000000", "--entry", "0x814000000", "--image-size", "0x800000", "--fb", "guest"])
    check("mode guest, band rows 0, guest rows 1600" in r.stdout, "--fb guest: the whole panel")

    # the layer's fallback: a plan that fails (no framebuffer in the tree) keeps the panel with the layer, no node
    if shutil.which("dtc"):
        mdtb = os.path.join(tmp, "min.dtb")
        sh(["dtc", "-q", "-I", "dts", "-O", "dtb", "-o", mdtb, os.path.join(HERE, "j293-minimal.dts")])
        r = sh([tool, mdtb, "--fb", "band"])
        check(r.returncode == 0 and "plan fails" in r.stdout and 'fb-mode = "none"' in r.stdout and "framebuffer@" not in r.stdout,
              "wel2.fb=band without a usable framebuffer falls back to layer: no node, fb-mode none")

    # privacy self-check really fires: a build whose platform string says Apple (stands for any leak) exits 3
    bad = os.path.join(tmp, "gfdt_dump_leak")
    r = sh(["cc", "-std=c11", "-O1", "-DGFDT_PLATFORM=\"AppleT8103\"", "-I" + os.path.join(EL2, "include"), "-I" + os.path.join(EL2, "boot")]
           + srcs + ["-o", bad])
    check(r.returncode == 0, "the leak variant builds")
    r = sh([bad, fx, "--layer", "0x802000000", "--entry", "0x814000000", "--image-size", "0x800000"])
    check(r.returncode == 3 and "PRIVACY" in r.stderr, "an 'apple' value (any case, raw byte scan) makes the tool exit 3")

    # the layer's placement rule is applied: an entry in the 64 MB piece (below 128 MB) is refused, as is one near the top
    r = sh([tool, fx, "--layer", "0x8c0000000", "--entry", "0x804000000", "--image-size", "0x800000"])
    check(r.returncode == 1 and "placement refused" in r.stderr, "an entry in a range below 128 MB is refused (exit 1)")
    r = sh([tool, fx, "--layer", "0x8c0000000", "--entry", "0x81c000000", "--image-size", "0x800000"])
    check(r.returncode == 1 and "placement refused" in r.stderr, "an image within 64 MB of the top of its range is refused (UEFI region)")

    # input errors
    r = sh([tool, os.path.join(tmp, "nonexistent.dtb")])
    check(r.returncode == 2, "a missing file exits 2")
    trunc = os.path.join(tmp, "t.dtb")
    open(trunc, "wb").write(raw[:100])
    r = sh([tool, trunc])
    check(r.returncode == 1, "a truncated blob exits 1")
    r = sh([tool, fx, "--fb", "bogus"])
    check(r.returncode == 2, "an unknown --fb mode is a usage error")

print("\n%s" % ("ALL PASSED" if not FAILED else "%d FAILED" % len(FAILED)))
sys.exit(1 if FAILED else 0)
