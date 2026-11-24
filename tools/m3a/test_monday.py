#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Tests of monday.py (T16i). Run: python3 -I tools/m3a/test_monday.py
No device, no m1n1, no network. Uses a fake loader that replays real loader dry-run outputs (tools/m3a/testdata/monday/*.txt,
made by testdata/monday/gen-fixtures.sh from the T16e rehearsal bundles) and mutates them into hardware-shaped and failing outputs."""
import contextlib
import importlib.util
import io
import os
import re
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
FIX = os.path.join(HERE, "testdata", "monday")
spec = importlib.util.spec_from_file_location("monday", os.path.join(HERE, "monday.py"))
mo = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mo)

FAILED = []


def check(cond, msg):
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        FAILED.append(msg)


def fixture(name):
    return open(os.path.join(FIX, name + ".txt")).read().rsplit("exit=", 1)[0]


SIZE, SHA16 = 747634688, "09e0e9c6be125a55"


def run(rid, text, exit_code=0, dry=True, **kw):
    if rid in ("W1a", "W1a2", "W1b", "W1c", "W2"):
        kw.setdefault("ramdisk_size", SIZE)
        kw.setdefault("ramdisk_sha16", SHA16)
    return mo.run_checks(mo.RUNGS[rid], text, exit_code, dry, **kw)


def failing(res):
    return [n for s, n, d in res if s == "FAIL"]


def hardware(text, cores=1, fb="band"):
    """Turn a dry-run output into what a real device prints (panel plan, geometry, cpu count)."""
    t = "\n".join(l for l in text.splitlines() if not l.startswith("DRY RUN"))
    if fb == "band":
        t = t.replace("mode none, band rows 0, guest rows 0, guest base 0x0, guest bytes 0x0",
                      "mode band, band rows 360, guest rows 1240, guest base 0x8ff000000, guest bytes 0x2f4000")
        t = t.replace('fb-mode = "none"', 'fb-mode = "band"')
        t = t.replace("  psci:", '  framebuffer@8ff000000:\n    width = <0xa00>;\n    height = <0x4d8>;\n    stride = <0x2800>;\n  psci:', 1)
    else:
        t = t.replace("mode none, band rows 0, guest rows 0, guest base 0x0, guest bytes 0x0",
                      "mode guest, band rows 0, guest rows 1600, guest base 0x8ff000000, guest bytes 0x3e8000")
    t = t.replace("// framebuffer plan", "// framebuffer of the prepared FDT: 2560x1600 stride=10240 (equals 4 x width) format=x2r10g10b10\n// framebuffer plan", 1)
    t = re.sub(r"8 core\(s\)", "%d core(s)" % cores, t)
    t = t.replace("0xf 0x10000000 0x0 0x100000>", "0xf 0x10000000 0x0 0x%x>" % (0x20000 * cores))      # F-40 (2): one GICR frame of 0x20000 per core
    return t + "\n"


# ---------------------------------------------------------------------------------------------------------------------
print("== the command table against the procedures ==")
docs = {3: open(os.path.join(REPO, mo.PROC3)).read(), 4: open(os.path.join(REPO, mo.PROC4)).read()}
for rid in mo.ORDER:
    r = mo.RUNGS[rid]
    text = docs[r["day"]]
    if r["verbatim"]:
        want = r["args"] if r["kind"] == "ehw2" else mo.rung_command_text(r)
        check(want in text, "%s: command appears verbatim in %s: %s" % (rid, r["doc"], want))
    else:
        check(bool(r["note"]), "%s: derived command has a note: %s" % (rid, r["note"][:70]))
a = lambda x: mo.RUNGS[x]["args"]
check(a("D3b") + " --args wel2.hold=1" == a("D2"), "D3b is D2 minus the hold")
check(a("D4a") == "--rung D2 --edk2 WATARI_M1_EFI.fd --fb band --go --cpuif trap-notc --args wel2.hold=1" and "--smp" not in a("D4a"), "D4a is trap-notc without --smp")
check("--smp" in a("D4b") and "trap-notc" not in a("D4b"), "D4b is --smp with trap")
check(a("D5b") == a("D5") and mo.RUNGS["D5b"]["bundle"] == "bgr8", "D5b is D5 from the BGR8 bundle")
check(a("W2g") == a("W2").replace("--fb band", "--fb guest"), "W2g is W2 with --fb guest")
check(a("W2-e4").replace("WATARI_M1_EFI-e4.fd", "WATARI_M1_EFI.fd") == a("W2"), "W2-e4 is W2 with the e4 firmware file (T16g names)")
check(a("W2-e1").replace("WATARI_M1_EFI-e1.fd", "WATARI_M1_EFI.fd").replace("--no-smp", "--smp") == a("W2"), "W2-e1 is W2 with the e1 firmware file and --no-smp")
check(all(mo.RUNGS[x]["verbatim"] for x in ("W2-e4", "W2-e1")), "fallback commands are verbatim (checked against day-4 section 6.3)")
check("{FW}" not in open(os.path.join(HERE, "monday.py")).read().split("RUNGS = {}")[1] and "provisional" not in open(os.path.join(HERE, "monday.py")).read(),
      "no provisional mechanism left in monday.py")
inproc = set()
for day, t in docs.items():
    for m in re.finditer(r"\bwl ([\w-]+) (--[^`\n]*?)(?=\s*(#|`|\n|$))", t):
        inproc.add("wl %s %s" % (m.group(1), m.group(2).strip()))
table = {mo.rung_command_text(r) for r in mo.RUNGS.values() if r["kind"] == "loader"}
extra = sorted(c for c in inproc if c not in table and "<" not in c)
check(True, "commands in the procedures but not in the table (information, add them if they are rungs): %s" % (extra or "none"))
for rid, r in mo.RUNGS.items():
    check(r["photos"] in mo.PHOTOS and all(len(x) == 2 for x in mo.PHOTOS[r["photos"]]), "%s: photo checklist exists" % rid)
    check(all(g[0].startswith("confirm:") or g[0] in mo.RUNGS or g[0] in mo.VERDICT_OWNER or g[0] in ("rate", "e-gate") for g in r["gate"]), "%s: gate ids are known" % rid)

print("== quoted screen strings exist in the layer/EDK2 sources ==")
paths = []
for base in ("el2", "edk2-apple"):
    for root, _, files in os.walk(os.path.join(REPO, base)):
        for f in files:
            if f.endswith((".c", ".h")):
                paths.append(os.path.join(root, f))
for s, frag in mo.SOURCE_STRINGS.items():
    ok = any(p.endswith(frag) and s in open(p, errors="replace").read() for p in paths)
    check(ok, "%r is in a source file ending %s" % (s, frag))

print("== automatic checks on the real dry-run outputs ==")
for fx, rid in (("D0", "D0"), ("D1", "D1"), ("D2", "D2"), ("D4-0", "D4-0"), ("D4", "D4"), ("D5-0", "D5-0"), ("D5", "D5"),
                ("W1a", "W1a"), ("W1a-nocsum", "W1a2"), ("W1b", "W1b"), ("W1c", "W1c"), ("W2", "W2")):
    ov, res, facts = run(rid, fixture(fx))
    check(ov == "PASS" and not failing(res), "%s: dry-run output PASSes (%d checks) %s" % (rid, len(res), failing(res)))
ov, res, f = run("W1c", fixture("W1c"))
check(f.get("image_bytes") == SIZE and f.get("image_sha16") == SHA16 and f.get("image_crc32") == "0xe3eb84c7" and f.get("ramdisk_arg") == "0x842000000,0x2ca00000",
      "W1c facts: image size, sha, crc32 and wel2.ramdisk=0x842000000,0x2ca00000 extracted")
ov, res, f = run("W2", fixture("W2"))
check(f["boot_arguments"].startswith("wel2.cpuif=trap-notc ") and f["cores"] == 8, "W2: trap-notc and 8 cores")

print("== hardware-shaped outputs ==")
for fx, rid, cores, fb in (("D0", "D0", 1, "band"), ("D2", "D2", 1, "band"), ("D4-0", "D4-0", 8, "band"), ("D4", "D4", 8, "band"),
                           ("D5-0", "D5-0", 1, "guest"), ("D5", "D5", 1, "guest"), ("W1b", "W1b", 1, "band"), ("W1c", "W1c", 1, "band"), ("W2", "W2", 8, "band")):
    t = hardware(fixture(fx), cores, fb)
    ov, res, facts = run(rid, t, dry=False)
    check(ov == "PASS", "%s on a device: PASS %s" % (rid, failing(res)))
    ov2, res2, _ = run(rid, fixture(fx), dry=False)
    check(ov2 == "FAIL" and "framebuffer plan" in failing(res2), "%s: a dry-run style plan (mode none) FAILs on a device" % rid)
base = hardware(fixture("D2"))
for what, t, expect in (
    ("stride is not 4 x width", base.replace("stride=10240 (equals 4 x width)", "stride=10304 (NOT 4 x width)"), "panel geometry"),
    ("band rows 352", base.replace("band rows 360", "band rows 352"), "framebuffer plan"),
    ("BLOCK CHECK FAIL", base.replace("BLOCK CHECK: PASS", "BLOCK CHECK: FAIL - the heap blocks"), "BLOCK CHECK"),
    ("a block FAIL line", base.replace("edk2  block 0x846000000..0x848000000 : PASS", "edk2  block 0x846000000..0x848000000 : FAIL"), "BLOCK CHECK"),
    ("GUEST FDT CHECK FAIL", base.replace("GUEST FDT CHECK: PASS", "GUEST FDT CHECK: FAIL"), "GUEST FDT CHECK"),
    ("8 cores with --no-smp", base.replace("1 core(s)", "8 core(s)"), "cpu nodes"),
    ("boot arguments without the hold", base.replace(" wel2.hold=1\"", "\""), "boot arguments"),
    ("wrong cpuif in the boot arguments", base.replace("wel2.cpuif=trap ", "wel2.cpuif=trap-notc "), "boot arguments"),
    ("no kboot_boot line", base.replace("kboot_boot(0x842000000) ...", "kboot_prepared"), "kboot_boot sent"),
    ("identifying text", base + "serial-number = <0x1>\n", "log hygiene"),
    ("GIC address wrong", base.replace("0xf 0x0 0x0 0x10000", "0x0 0x8000000 0x0 0x10000"), "GIC node"),
    ("first memory range too small", re.sub(r"reg = <0x8 0x44000000 0x1 0xbc000000>", "reg = <0x8 0x44000000 0x0 0x4000000>", base), "first /memory range holds the EDK2 block"),
):
    ov, res, _ = run("D2", t, dry=False)
    check(ov == "FAIL" and expect in failing(res), "D2 %s -> FAIL (%s)" % (what, expect))
ov, res, _ = run("D2", base, exit_code=4, dry=False)
check(ov == "FAIL" and "loader exit" in failing(res), "exit 4 -> FAIL with the exit meaning")
ov, res, _ = run("D2", base, exit_code=1, dry=False, interrupted=True)
check(ov == "PASS", "Ctrl-C after kboot_boot is accepted as the end of a jump")
ov, res, _ = run("D2", base.replace("kboot_boot(0x842000000) ...", "x"), exit_code=1, dry=False, interrupted=True)
check(ov == "FAIL", "Ctrl-C before the jump is not accepted")
w = hardware(fixture("W1c"))
for what, t, expect in (
    ("sidecar mismatch", w.replace(SHA16, "0000000000000000"), "RAMDISK PLAN numbers"),
    ("crc mismatch plan/written", w.replace("(crc32 0xe3eb84c7)", "(crc32 0xdeadbeef)"), "RAMDISK written"),
    ("wel2.ramdisk size wrong", w.replace("0x2ca00000", "0x2c000000"), "wel2.ramdisk equals the block"),
    ("ramdisk base not 32 MB aligned", w.replace("wel2.ramdisk=0x842000000", "wel2.ramdisk=0x841000000"), "wel2.ramdisk base 32 MB aligned"),
    ("two checksum errors", w + "ramdisk slice 3/45: checksum error twice (x): aborting; nothing was handed over\n", "slice checksums"),
    ("two plain retries", w + "  ramdisk slice 3/45: checksum error, retrying this slice once\n" * 2, "slice checksums"),
    ("old gfdt_dump", w + "NOTE: this gfdt_dump has no --ramdisk option (T21a adds it)\n", "gfdt_dump knows --ramdisk"),
    ("ramdisk block size", w.replace("748683264 bytes (714.0 MiB", "747634688 bytes (714.0 MiB"), "ramdisk block size"),
):
    ov, res, _ = run("W1c", t, dry=False)
    check(ov == "FAIL" and expect in failing(res), "W1c %s -> FAIL (%s)" % (what, expect))
ov, res, f = run("W1c", w + "  ramdisk slice 3/45: checksum error, retrying this slice once\n", dry=False)
check(ov == "PASS" and f["slice_retries"] == 1, "one slice retry is tolerated and recorded")

print("== the rate test and its decision ==")
for wr, want in ((7.0, "CONTINUE"), (2.5, "CONTINUE"), (2.0, "PREAPPROVED"), (0.9, "PREAPPROVED"), (0.8, "STOP"), (0.4, "STOP"), (0.0, "STOP")):
    check(mo.rate_decision(wr, SIZE)[0] == want, "%.1f MB/s -> %s" % (wr, want))
for wr, want in ((3.1, "CONTINUE"), (1.3, "PREAPPROVED"), (0.6, "STOP"), (0.3, "STOP")):
    t = re.sub(r"^RATE TEST: .*$", "RATE TEST: write %.2f MB/s, read 9.00 MB/s, read-back identical" % wr, fixture("W1a"), flags=re.M)
    ov, res, f = run("W1a", t, dry=False)
    check(ov == "PASS" and f["rate_decision"] == want, "W1a on a device at %.1f MB/s: decision %s" % (wr, want))
t = re.sub(r"^RATE TEST: .*$", "RATE TEST: FAIL - read-back differs in the slice at offset 0x1000000", fixture("W1a"), flags=re.M)
check(run("W1a", t, dry=False)[0] == "FAIL", "a read-back difference FAILs the rate test")
t = re.sub(r"^RATE TEST: .*$", "RATE TEST: write 2.00 MB/s, read 9.00 MB/s, read-back not compared (dry run)", fixture("W1a"), flags=re.M)
check(run("W1a", t, dry=False)[0] == "FAIL", "'not compared' is not accepted on a device")
check(run("W1a2", fixture("W1a-nocsum"))[0] == "PASS", "--no-data-csums output is accepted (the 'data checksums disabled' line)")
check(run("W1a2", fixture("W1a"))[0] == "FAIL", "--no-data-csums needs its 'data checksums disabled' line")

print("== block addresses of the EDK2 jump rungs (no ring: the spacer block stays allocated and unwritten) ==")
d0, d2 = fixture("D0"), fixture("D2")
d0_addr = {k: v for k, v in re.findall(r"^BLOCK  (layer|edk2) .* at (0x[0-9a-f]+)", d0, re.M)}
d2_addr = {"layer": re.search(r"^LAYER  .* at (0x[0-9a-f]+)", d2, re.M).group(1), "edk2": re.search(r"^EDK2   .* at (0x[0-9a-f]+)", d2, re.M).group(1)}
check(d0_addr == d2_addr and len(d0_addr) == 2, "D2 keeps D0's layer and EDK2 block addresses %s" % d0_addr)
spc = re.search(r"^SPACER 32 MB aligned block at 0x([0-9a-f]+)", d2, re.M)
edk = re.search(r"^EDK2   \S+ +\d+ bytes at 0x([0-9a-f]+)", d2, re.M)
d0g = re.search(r"^BLOCK  guest .* at 0x([0-9a-f]+)", d0, re.M)
check(bool(spc and edk and d0g) and int(spc.group(1), 16) == int(d0g.group(1), 16) and int(spc.group(1), 16) + (32 << 20) == int(edk.group(1), 16),
      "D2: the unwritten SPACER block is D0's 'guest' block, right below EDK2 (the addresses are D0's)")
check("GUEST FDT CHECK: PASS" in d2 and "pmlog" not in d2.lower() and "PMLOG" not in d2 and "POSTMORTEM" not in d2, "D2 dry run: GUEST FDT CHECK PASS, no ring anywhere in the output")
w1c = fixture("W1c")
check("wel2.pmlog" not in w1c and re.search(r"wel2\.ramdisk=0x[0-9a-f]+,0x[0-9a-f]+ wel2\.abi=fdt", w1c), "W1c: wel2.ramdisk is followed directly by wel2.abi (no wel2.pmlog)")
for fx, rid in (("D2", "D2"), ("D4", "D4"), ("D5", "D5"), ("W1c", "W1c"), ("W2", "W2")):
    ov, res, facts = run(rid, fixture(fx))
    check(ov == "PASS" and "pmlog_base" not in facts and "pmlog_arg" not in facts, "%s: dry-run output PASSes with no ring facts" % rid)
base2 = hardware(fixture("D2"))
ov, res, _ = run("D2", re.sub(r'(wel2\.guest=0x[0-9a-f]+)', r'\1 wel2.pmlog=0x844000000,0x200000', base2, count=1), dry=False)
check(ov == "FAIL" and "boot arguments" in failing(res), "a stray wel2.pmlog in the boot arguments of a rung that does not ask for it -> FAIL (boot arguments)")

print("== results file and gate logic ==")
fake = os.path.join(tempfile.mkdtemp(), "fake_loader.py")
open(fake, "w").write('import os, sys\nsys.stdout.write(open(os.environ["FAKE_FIXTURE"]).read().rsplit("exit=", 1)[0])\nsys.exit(int(os.environ.get("FAKE_EXIT", "0")))\n')
tmpd = tempfile.mkdtemp()


def mon(bundle, out, *args, fixture_name=None, exit_code=0):
    if fixture_name:
        os.environ["FAKE_FIXTURE"] = os.path.join(FIX, fixture_name + ".txt")
        os.environ["FAKE_EXIT"] = str(exit_code)
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        try:
            code = mo.main(["--bundle", bundle, "--out", out] + list(args))
        except SystemExit as e:
            code = e.code
    return code, buf.getvalue()


b3 = tempfile.mkdtemp()
b4 = tempfile.mkdtemp()
os.makedirs(os.path.join(b4, "winram"))
with open(os.path.join(b4, "winram", "winram.img"), "wb") as f:      # a sparse file of the real size: only its size is read
    f.truncate(SIZE)
open(os.path.join(b4, "winram", "winram.img.sha256"), "w").write(SHA16 + "0" * 48 + "  winram.img\n")
out = tempfile.mkdtemp()
R = lambda rid, fx, *extra, **kw: mon(b3, out, "rung", rid, "--dry-run", "--quiet", "--loader", fake, "--force-bundle", *extra, fixture_name=fx, **kw)
V = lambda vid, res, b=b3: mon(b, out, "verdict", vid, res, "--note", "test", "--dry-run")[0]
C = lambda key, b=b3: mon(b, out, "confirm", key, "--by", "Marco", "--dry-run")[0]

code, txt = R("D0", "D0")
check(code == mo.EX_GATE and "not bypassable" in txt, "no backup confirmation: D0 refused")
code, txt = R("D0", "D0", "--force-gate", "because")
check(code == mo.EX_GATE, "--force-gate never bypasses the backup confirmation")
check(C("backup") == 0, "confirm backup recorded")
code, txt = R("D0", "D0")
check(code == 0 and "RESULT D0: PASS" in txt and "ESEGUO" in txt and "PHOTO CHECKLIST D0" in txt, "D0 runs through the fake loader and passes")
rec = open(os.path.join(out, "results-dryrun.md")).read()
check("### D0 auto" in rec and "- result: PASS" in rec and "- command: wl D0 --rung D0" in rec and "BLOCK CHECK" in rec, "the record holds result, command and checks")
check(not os.path.exists(os.path.join(out, "results.md")), "dry-run records go to results-dryrun.md only")
logs = [f for f in os.listdir(out) if f.endswith("-D0.log")]
check(len(logs) == 1 and "BLOCK CHECK: PASS" in open(os.path.join(out, logs[0])).read(), "the full output is tee'd to <timestamp>-D0.log")
import shutil
shutil.copy(os.path.join(HERE, "idregs.py"), os.path.join(b3, "idregs.py"))
idr = mo.RUNGS["ID0"]
check(idr["kind"] == "idregs" and not idr["jump"] and idr["minutes"] == "2 min" and idr["gate"] == [mo.G_BACKUP] and "ID0" in mo.ORDER,
      "ID0 is a read-only rung: no jump, 2 min, gated only on the backup confirmation")
check("sola lettura" in idr["intro"] and "senza saltare" in idr["intro"], "ID0 intro (Italian) says read-only and no jump")
code, txt = R("ID0", "D0")
check(code == 0 and "RESULT ID0: PASS" in txt and "idregs.py --dry-run" in txt, "ID0 dry-run runs idregs.py --dry-run from the bundle and passes")
check("PHOTO CHECKLIST ID0" in txt and "--ramdisk-reuse" not in txt, "ID0 prints its checklist and never adds --ramdisk-reuse")
check(mo.run_checks(idr, "Traceback (most recent call last)", 0, False)[0] == "FAIL", "ID0: a traceback fails the automatic check")
check(mo.run_checks(idr, "== x\nno verdict here", 0, False)[0] == "FAIL", "ID0 (real run): output without the verdict block fails")
check(mo.run_checks(idr, "== Windows-relevant verdict ==\n", 0, False)[0] == "PASS", "ID0 (real run): output with the verdict block passes")
shutil.copy(os.path.join(HERE, "hacr_probe.py"), os.path.join(b3, "hacr_probe.py"))
hr = mo.RUNGS["HACR0"]
check(hr["kind"] == "hacr" and not hr["jump"] and hr["minutes"] == "2 min" and hr["gate"] == [mo.G_BACKUP] and "HACR0" in mo.ORDER and "--el1" not in hr["args"],
      "HACR0 is a no-jump rung gated only on the backup confirmation, readback only (no --el1)")
code, txt = R("HACR0", "D0")
check(code == 0 and "RESULT HACR0: PASS" in txt and "hacr_probe.py --dry-run" in txt, "HACR0 dry-run runs hacr_probe.py --dry-run from the bundle and passes")
check(mo.run_checks(hr, "Traceback (most recent call last)", 0, False)[0] == "FAIL", "HACR0: a traceback fails the automatic check")
check(mo.run_checks(hr, "== HACR_EL2[56] probe (mode: readback-only), boot core only\nnot at EL2 -> stop, nothing written\n", 0, False)[0] == "FAIL",
      "HACR0 (real run): the header and an early stop (no STICKS line) fail")
check(mo.run_checks(hr, "HACR_EL2[56] DOES NOT STICK (reads back 0)\n", 0, False)[0] == "PASS", "HACR0 (real run): the does-not-stick line passes (the probe ran to the end)")
check(mo.run_checks(hr, "HACR_EL2[56] STICKS (reads back 1)\n", 0, False)[0] == "PASS", "HACR0 (real run): the sticks line passes")
check(mo.run_checks(hr, "something else\n", 0, False)[0] == "FAIL", "HACR0 (real run): output without the sticks line fails")
code, txt = R("D2", "D2")
check(code == mo.EX_GATE and "D1 is NONE" in txt, "D2 refused before D1")
code, txt = R("D1", "D1")
check(code == 0 and "RESULT D1: PASS" in txt and "verdict D1" in txt, "D1 (jump) passes automatically and asks for the photos")
code, txt = R("D2", "D2")
check(code == mo.EX_GATE and "D1 is PENDING" in txt, "an automatic PASS of a jump rung is only PENDING: D2 still refused")
check(V("D1", "UNREADABLE") == 0, "verdict UNREADABLE recorded")
code, txt = R("D2", "D2")
check(code == mo.EX_GATE and "D1 is UNREADABLE" in txt, "UNREADABLE does not open the gate")
check(V("D1", "PASS") == 0, "verdict D1 PASS")
code, txt = R("D2", "D2", "--force-gate", "x")
check(code == 0, "D2 runs once D1 is PASS")
code, txt = mon(b3, out, "next", "--dry-run")
check("record the verdict" in txt or "read the photos" in txt, "next: D2 waits for the photo verdicts")
check(V("D2", "PASS") == 0, "verdict D2 PASS")
code, txt = mon(b3, out, "next", "--dry-run")
check("verdict D3" in txt, "next: D3 verdict still missing after D2 PASS")
check(V("D3", "PASS") == 0, "verdict D3 PASS")
code, txt = mon(b3, out, "next", "--dry-run")
check("RUN:  python3 monday.py rung D4 --marco-go" in txt and "Italiano" in txt and "PM0" not in txt and "postmortem" not in txt, "next: D4 comes right after the D2/D3 verdicts (jump, so --marco-go); no post-mortem step")
code, txt = R("D4", "D4")
check(code == 0, "D4 runs")
check(V("D4", "ABORT") == 0, "verdict D4 ABORT")
code, txt = mon(b3, out, "next", "--dry-run")
check("STOP" in txt and "D4" in txt and "RUN:" not in txt, "next after an ABORT: stop and ask Marco, no rung offered")
check("postmortem" not in txt and "post-mortem" not in txt, "next after an ABORT: no post-mortem step (RAM is volatile)")
code, txt = R("D5", "D5")
check(code == mo.EX_GATE and "Marco has not said to proceed" in txt, "an unresolved ABORT blocks the next jump rung")
code, txt = R("D4a", "D4", "--force-gate", "x")
check(code == 0, "the split D4a (same group) may run after a D4 abort")
check(C("proceed:D4") == 0, "confirm proceed:D4")
code, txt = R("D5", "D5")
check(code == 0, "after 'proceed' D5 runs")
check(V("D4", "PASS") == 0 and V("D5", "FAIL") == 0, "verdicts D4 PASS, D5 FAIL")
code, txt = mon(b3, out, "next", "--dry-run")
check("D5" in txt and "STOP" in txt, "next: D5 FAIL stops")
check(C("proceed:D5") == 0, "confirm proceed:D5")
# rung E is MANDATORY before any W rung (Marco, 2026-11-15)
code, txt = mon(b3, out, "next", "--dry-run")
check("RUN:  python3 monday.py rung E" in txt and "MANDATORY" in txt and "day-4 bundle" not in txt, "next: D0-D4 PASS, then rung E (mandatory) before day 4")
mo.append_record(os.path.join(out, "results-dryrun.md"), "E", "auto", [("result", "PASS")])
code, txt = mon(b3, out, "next", "--dry-run")
check("verdict E" in txt and "RUN:" not in txt, "next: E ran, the report must be read and a verdict recorded (PENDING)")
check(V("E", "FAIL") == 0, "verdict E FAIL (overlap found)")
code, txt = mon(b3, out, "next", "--dry-run")
check("STOP" in txt and "BLOCKS day 4" in txt and "proceed:E --by Marco" in txt and "RUN:" not in txt, "next after an E FAIL: stop, only Marco's proceed:E goes on")
code, txt = mon(b4, out, "rung", "W1a", "--dry-run", "--quiet", "--loader", fake, "--force-bundle", "--force-gate", "x", fixture_name="W1a")
check(code == mo.EX_GATE and "e-gate is FAIL" in txt and "not bypassable" in txt, "an E FAIL closes every W rung, also with --force-gate")
code, txt = mon(b3, out, "confirm", "proceed:E", "--by", "Ciani", "--dry-run")
check(code == mo.EX_USAGE, "proceed:E can only be recorded as Marco's")
check(C("proceed:E") == 0, "confirm proceed:E --by Marco")
check(mo.e_state(mo.parse_results(os.path.join(out, "results-dryrun.md"))) == "PROCEEDED", "E FAIL + proceed:E = PROCEEDED (opens the gate)")
check(V("E", "PASS") == 0, "verdict E PASS (re-run, clean)")
check(mo.e_state(mo.parse_results(os.path.join(out, "results-dryrun.md"))) == "PASS", "E PASS opens the gate")
code, txt = mon(b3, out, "next", "--dry-run")
check("day-4 bundle" in txt or "switch to the day-4" in txt.lower() or "Switch to the day-4" in txt, "next: D0-D4 and E PASS, tells to switch to the day-4 bundle")
# day 4 (shares the results folder)
code, txt = mon(b4, out, "next", "--dry-run")
check("image-ok" in txt and "Microsoft" in txt, "day 4: the image OK of Marco is required first (WG-2)")
code, txt = mon(b4, out, "rung", "W1a", "--dry-run", "--quiet", "--loader", fake, "--force-bundle", fixture_name="W1a")
check(code == mo.EX_GATE and "confirm:image-ok" in txt and "not bypassable" in txt, "W1a refused without the image confirmation, also with --force-gate")
check(C("image-ok", b4) == 0, "confirm image-ok")
code, txt = mon(b4, out, "next", "--dry-run")
check("rung W1a" in txt, "next: W1a")
code, txt = mon(b4, out, "rung", "W1a", "--dry-run", "--quiet", "--loader", fake, fixture_name="W1a")
check(code == 0 and "RESULT W1a: PASS" in txt and "W1a DECISION: DRY" in txt, "W1a runs from the day-4 bundle kind (winram present)")
code, txt = mon(b4, out, "rung", "D0", "--dry-run", "--quiet", "--loader", fake, fixture_name="D0")
check(code == mo.EX_PRECOND and "BUNDLE:" in txt, "a day-3 rung from the day-4 bundle is refused")
code, txt = mon(b3, out, "rung", "W1a", "--dry-run", "--quiet", "--loader", fake, fixture_name="W1a")
check(code == mo.EX_PRECOND and "BUNDLE:" in txt, "a day-4 rung from the day-3 bundle is refused")
code, txt = mon(b4, out, "next", "--dry-run")
check("rung W2 --marco-go" in txt and "ONE transfer" in txt, "next after W1a: the combined W2 run (one transfer)")
code, txt = mon(b4, out, "rung", "W2", "--dry-run", "--quiet", "--loader", fake, fixture_name="W2")
check(code == 0 and "RESULT W2: PASS" in txt and "PHOTO CHECKLIST W2" in txt and "Attendere" in txt, "W2 runs; checklist quotes the Setup text")
check(V("W2", "PASS", b4) == 0, "verdict W2 PASS")
code, txt = mon(b4, out, "next", "--dry-run")
check("W3 =" in txt or "W3" in txt, "next: W3 verdict pending")
code, txt = mon(b4, out, "rung", "W2-e4", "--dry-run", "--quiet", "--loader", fake, "--force-bundle", fixture_name="W2")
check(code == mo.EX_GATE and "confirm:fallback-e4" in txt and "not bypassable" in txt, "the fallback is refused without Marco's 'go e4' (also with --force-gate)")
# next logic on records: no PM step anywhere
rp = tempfile.mkdtemp()
RP = os.path.join(rp, "results.md")
mo.append_record(RP, "backup", "confirm", [("result", "CONFIRMED"), ("by", "Marco")])
for fid in ("D0", "D1"):
    mo.append_record(RP, fid, "verdict", [("result", "PASS")])
mo.append_record(RP, "D2", "auto", [("result", "PASS")])
mo.append_record(RP, "D2", "verdict", [("result", "PASS")])
mo.append_record(RP, "D3", "verdict", [("result", "PASS")])
rid, lines, _ = mo.next_step(mo.parse_results(RP), "day3")
check(rid == "D4" and "post-mortem" not in " ".join(lines), "next: after D2/D3 PASS the next rung is D4 (no PM0)")
mo.append_record(RP, "D4", "auto", [("result", "PASS")])
mo.append_record(RP, "D4", "verdict", [("result", "FAIL"), ("note", "frozen")])
rid, lines, _ = mo.next_step(mo.parse_results(RP), "day3")
check(rid is None and "post-mortem" not in " ".join(lines) and "postmortem" not in " ".join(lines) and "FAIL/ABORT" in " ".join(lines), "next after a D4 FAIL: stop and ask Marco, no post-mortem step")
check(mon(b3, tempfile.mkdtemp(), "confirm", "skip-pm0", "--by", "Marco", "--dry-run")[0] == mo.EX_USAGE, "confirm skip-pm0 is no longer a known key")

# rate gating from records (device case)
rd = tempfile.mkdtemp()
mo.append_record(os.path.join(rd, "results.md"), "confirm-dummy", "note", [("x", "1")])
for fid, key in (("backup", None), ("image-ok", None)):
    mo.append_record(os.path.join(rd, "results.md"), fid, "confirm", [("result", "CONFIRMED"), ("by", "Marco")])
for fid in ("D0", "D1", "D2", "D3", "D4"):
    mo.append_record(os.path.join(rd, "results.md"), fid, "verdict", [("result", "PASS")])
mo.append_record(os.path.join(rd, "results.md"), "E", "auto", [("result", "PASS")])
mo.append_record(os.path.join(rd, "results.md"), "E", "verdict", [("result", "PASS")])
mo.append_record(os.path.join(rd, "results.md"), "W1a", "auto", [("result", "PASS"), ("decision", "STOP"), ("rate-write", "0.4"), ("projected-seconds", "1900")])
recs = mo.parse_results(os.path.join(rd, "results.md"))
check(mo.gate_check(recs, mo.RUNGS["W2"])[0] is False and "rate is WAIT" in " ".join(mo.gate_check(recs, mo.RUNGS["W2"])[2]), "rate STOP without Marco's rate-ok: W2 gate closed")
rid, lines, _ = mo.next_step(recs, "day4")
check(rid is None and "ask Marco" in " ".join(lines), "next asks Marco about the slow rate")
mo.append_record(os.path.join(rd, "results.md"), "rate-ok", "confirm", [("result", "CONFIRMED"), ("by", "Marco")])
recs = mo.parse_results(os.path.join(rd, "results.md"))
check(mo.gate_check(recs, mo.RUNGS["W2"])[0] is True, "rate-ok opens the W2 gate")
mo.append_record(os.path.join(rd, "results.md"), "W2", "auto", [("result", "PASS")])
mo.append_record(os.path.join(rd, "results.md"), "W2", "verdict", [("result", "FAIL"), ("note", "bugcheck")])
recs = mo.parse_results(os.path.join(rd, "results.md"))
ge4 = mo.gate_check(recs, mo.RUNGS["W2-e4"])
check(ge4[0] is False and any("fallback-e4" in m for m in ge4[1]), "after a W2 FAIL the e4 gate still needs Marco's go (hard, not bypassable)")
mo.append_record(os.path.join(rd, "results.md"), "fallback-e4", "confirm", [("result", "CONFIRMED"), ("by", "Marco")])
recs = mo.parse_results(os.path.join(rd, "results.md"))
check(mo.gate_check(recs, mo.RUNGS["W2-e4"])[0] is True, "W2 FAIL + 'go e4' opens the e4 gate")
ge1 = mo.gate_check(recs, mo.RUNGS["W2-e1"])
check(ge1[0] is False and any("W2-e4" in m for m in ge1[2]) and any("fallback-e1" in m for m in ge1[1]), "e1 needs e4 failed first and its own 'go e1'")
rid, lines, _ = mo.next_step(recs, "day4")
check(rid is None and "FAIL/ABORT" in " ".join(lines), "next after W2 FAIL: stop and ask Marco")

print("== pre-approvals (decisions 2 and 5) and the E gate without E ==")
noe = tempfile.mkdtemp()
for fid in ("backup", "image-ok"):
    mo.append_record(os.path.join(noe, "results.md"), fid, "confirm", [("result", "CONFIRMED"), ("by", "Marco")])
for fid in ("D0", "D1", "D2", "D3", "D4"):
    mo.append_record(os.path.join(noe, "results.md"), fid, "verdict", [("result", "PASS")])
mo.append_record(os.path.join(noe, "results.md"), "W1a", "auto", [("result", "PASS"), ("decision", "CONTINUE")])
gw = mo.gate_check(mo.parse_results(os.path.join(noe, "results.md")), mo.RUNGS["W2"])
check(gw[0] is False and any("e-gate is NONE" in m for m in gw[1]), "no recorded E: W2 gate closed (hard)")
check(all(any(g[0] == "e-gate" for g in mo.RUNGS[x]["gate"]) for x in ("W1a", "W1a2", "W1b", "W1c", "W2", "W2g", "W2-e4", "W2-e1")), "every W rung carries the E gate")
pa = tempfile.mkdtemp()
note = "pre-approved 2026-11-15, docs/decisions/2026-11-15-marco-monday.md"
for key in ("image-ok", "fallback-e4", "fallback-e1"):
    code, txt = mon(b3, pa, "confirm", key, "--by", "Marco", "--note", note, "--dry-run")
    check(code == 0 and "PRE-APPROVED by Marco on 2026-11-15" in txt, "%s accepted with the pre-approval note and printed as such" % key)
code, txt = mon(b3, pa, "confirm", "image-ok", "--by", "Marco", "--dry-run")
check(code == 0 and "pre-approved %s on 2026-11-15" % "image-ok" in txt and note in txt, "image-ok without the note: the exact note is printed")
code, txt = mon(b3, pa, "confirm", "backup", "--by", "Marco", "--note", note, "--dry-run")
check(code == mo.EX_USAGE and "NOT pre-approved" in txt, "the backup is never pre-approved")
check(mo.confirm_state(mo.parse_results(os.path.join(pa, "results-dryrun.md")), "backup") == "NONE", "a refused pre-approved backup is not recorded")
check(mo.rate_decision(0.8, SIZE)[0] == "STOP" and mo.rate_decision(0.9, SIZE)[0] == "PREAPPROVED", "the 15 min limit (900 s): above it STOP, up to it pre-approved")

print("== hardening: stale backup, verdict without a run, fallback manifest ==")
sd = tempfile.mkdtemp()
with open(os.path.join(sd, "results.md"), "w") as f:
    f.write("### backup confirm 2020-01-01T10:00:00\n- result: CONFIRMED\n- by: Marco\n\n")
check(mo.confirm_state(mo.parse_results(os.path.join(sd, "results.md")), "backup") == "STALE", "a backup confirmation of an earlier day does not count")
code, txt = mon(tempfile.mkdtemp(), sd, "verdict", "D2", "PASS", "--note", "x", "--dry-run")
check(code == mo.EX_PRECOND and "no recorded run" in txt, "a verdict without a recorded run is refused")
fb = tempfile.mkdtemp()
def wf(name, data): open(os.path.join(fb, name), "wb").write(data)
import hashlib as _h
wf("WATARI_M1_EFI.fd", b"main"); wf("WATARI_M1_EFI.fd.build.txt", b"model=j293\nbench=0\nramdisk_standin=0\nbgr8=1\nbuild_tag=\nbuild_pcd=\ndirty=0\n")
wf("WATARI_M1_EFI-e4.fd", b"e4fw")
def man(model, tag, dirty="0", bench="0", sha=None):
    return ("model=%s\nbench=%s\nramdisk_standin=0\nbgr8=1\nbuild_tag=%s\nbuild_pcd=\ndirty=%s\nfd_sha256=%s\n" % (model, bench, tag, dirty, sha or _h.sha256(b"e4fw").hexdigest())).encode()
check(mo.check_fallback_fw(fb, "e1") is not None, "missing e1 file: refused")
wf("WATARI_M1_EFI-e4.fd.build.txt", man("j293-e4", "e4")); check(mo.check_fallback_fw(fb, "e4") is None, "a clean e4 fallback passes")
for label, m in (("wrong model", man("j293", "e4")), ("bench", man("j293-e4", "e4", bench="1")), ("other tag", man("j293-e4", "t16be4")),
                 ("dirty", man("j293-e4", "e4", dirty="1")), ("sha mismatch", man("j293-e4", "e4", sha="0" * 64))):
    wf("WATARI_M1_EFI-e4.fd.build.txt", m); check(mo.check_fallback_fw(fb, "e4") is not None, "fallback refused: %s" % label)

print("== F-40: hardware-shaped checks (one core, minute-long transfer, property names) ==")
hw1 = fixture("W1c-hw1core")
ov, res, f = run("W1c", hw1, dry=False)
check(ov == "PASS" and not failing(res), "W1c hardware fixture (one core, GICR 0x20000, 'in 1 min 07 s'): PASS %s" % failing(res))
check(f.get("transfer_seconds") == 67 and f.get("cores") == 1, "'in 1 min 07 s' is recorded as transfer-seconds 67: %r" % f.get("transfer_seconds"))
check(any(n == "GIC node" and "0x20000" in d for s, n, d in res), "the GIC check names the expected GICR size for one core")
ov, res, f = run("W1c", fixture("W1c"))
check(ov == "PASS" and f.get("transfer_seconds") == 0, "the short form 'in 0 s' is still accepted")
for what, line, secs in (("in 45 s", "in 45 s", 45), ("in 12 min 28 s", "in 12 min 28 s", 748), ("in 1 min 00 s", "in 1 min 00 s", 60)):
    ov, res, f = run("W1c", re.sub(r"bytes in [^(]+\(", "bytes in " + line.split("in ", 1)[1] + " (", hw1), dry=False)
    check(ov == "PASS" and f.get("transfer_seconds") == secs, "RAMDISK written %s -> transfer-seconds %d" % (what, secs))
ov, res, f = run("W1c", hw1.replace("bytes in 1 min 07 s (", "bytes in about a minute ("), dry=False)
check(ov == "FAIL" and "RAMDISK written" in failing(res), "an unreadable transfer time still FAILs (no silent pass)")
ov, res, f = run("W1c", hw1.replace("(crc32 0xe3eb84c7)", "(crc32 0xdeadbeef)", 1).replace("crc32 0xe3eb84c7, no", "x"), dry=False)
check(ov == "FAIL", "a crc mismatch on the minute-long form FAILs as before")
# the GIC node follows the core count
check(failing(run("W1c", hw1.replace("0xf 0x10000000 0x0 0x20000>", "0xf 0x10000000 0x0 0x100000>", 1), dry=False)[1]) == ["GIC node"], "one core with the eight-core GICR size (0x100000) -> only 'GIC node' FAILs")
w8 = hardware(fixture("W2"), cores=8)
check(run("W2", w8, dry=False)[0] == "PASS" and "0x0 0x100000>" in w8, "eight cores: GICR 0x100000 still passes")
check(failing(run("W2", w8.replace("0xf 0x10000000 0x0 0x100000>", "0xf 0x10000000 0x0 0x20000>", 1), dry=False)[1]) == ["GIC node"], "eight cores with the one-core GICR size -> only 'GIC node' FAILs")
check(failing(run("W2", w8.replace("8 core(s)", "4 core(s)"), dry=False)[1]) != [], "a core count that is neither expected nor consistent with the GICR still FAILs")
# log hygiene: names are fine, values are not (all values below are invented)
E_HW = fixture("E-hw")
check("mac-address-bluetooth0" in E_HW and "serial-number[17]" in E_HW, "E fixture carries the property NAMES line (invented lengths)")
ov, res, _ = mo.run_checks(mo.RUNGS["E"], E_HW, 0, False)
check(ov == "PASS" and "log hygiene" not in failing(res), "rung E: property NAMES in the names-only line do not fail log hygiene %s" % failing(res))
for what, t in (("a dumped mac-address value", E_HW + "  mac-address = [02 00 5e 10 00 01]\n"),
                ("a dumped serial-number value", E_HW + '  serial-number = "FAKE000SN"\n'),
                ("a local-mac-address line", E_HW + "local-mac-address = <0x0 0x0>\n"),
                ("a MAC-shaped value elsewhere", E_HW + "  adapter 02:00:5e:10:00:01 up\n"),
                ("a MAC-shaped value with dashes", E_HW + "  adapter 02-00-5E-10-00-01 up\n"),
                ("a MAC value inside the names-only line", E_HW.replace("fake-chosen[3]", "fake-chosen[3], 02:00:5e:10:00:01")),
                ("a value glued to a name in the names-only line", E_HW.replace("serial-number[17]", "serial-number=FAKE000SN")),
                ("a second serial-number name outside the names-only line", E_HW + "serial-number\n")):
    ov, res, _ = mo.run_checks(mo.RUNGS["E"], t, 0, False)
    check(ov == "FAIL" and "log hygiene" in failing(res), "rung E log hygiene still FAILs on %s" % what)
check(mo.hygiene_hit("  /chosen property names (values NOT printed): a[1], b[int]") is None and mo.hygiene_hit("clean line\n") is None, "hygiene_hit: clean text is None")
check(mo.hygiene_hit("a 12:34:56 clock and 1:2:3:4:5 and 00:11:22:33:44") is None, "hygiene_hit: clock times and short colon lists are not MACs")

print("== W1c-e1 (W1c with the j293-e1 firmware) ==")
r1 = mo.RUNGS["W1c-e1"]
check(a("W1c-e1") == a("W1c").replace("WATARI_M1_EFI.fd", "WATARI_M1_EFI-e1.fd") and a("W1c-e1").count("WATARI_M1_EFI-e1.fd") == 1,
      "W1c-e1 is W1c with the firmware file as the ONLY change: %s" % a("W1c-e1"))
check("--cpuif" not in a("W1c-e1") and "--smp" not in a("W1c-e1") and "--args wel2.hold=1" in a("W1c-e1") and "--pmlog" not in a("W1c-e1") and "--ramdisk winram/winram.img" in a("W1c-e1"),
      "W1c-e1: cpuif trap, no smp, same wel2.hold=1, RAM disk")
check(r1["jump"] and r1["fallback"] == "e1" and r1["verdicts"] == ["W1c-e1"] and r1["bundle"] == "day4" and "W1c-e1" in mo.ORDER and mo.VERDICT_OWNER["W1c-e1"] == "W1c-e1"
      and mo.GROUP["W1c-e1"] == "W1c" and not r1["verbatim"] and r1["note"], "W1c-e1 is wired: jump, fallback e1, verdict owner, group, ORDER, derived-with-note")
check(r1["photos"] in mo.PHOTOS and r1["fresh"], "W1c-e1 has photos and a fresh m1n1 boot")
gids = [g[0] for g in r1["gate"]]
check(gids == ["confirm:backup", "confirm:image-ok", "e-gate", "W1a", "rate", "confirm:fallback-e1"], "W1c-e1 gates: backup, image-ok, E, W1a, rate, confirm:fallback-e1 (no D-gate, no W2/W2-e4 failure): %s" % gids)
check("D3" not in gids and "D4" not in gids and "W2" not in gids and "W2-e4" not in gids and "W1c" not in gids, "W1c-e1 needs no D rung, no W1c and no recorded W2/W2-e4 failure")
check(all(mo.RUNGS[x]["fallback"] == x[3:] for x in ("W2-e4", "W2-e1")) and all(mo.RUNGS[x]["fallback"] is None for x in ("W1c", "W2", "D2")), "only the fallback rungs carry a fallback tag")
# gate logic on records of a NEW folder (no D0-D4 recorded)
nf = tempfile.mkdtemp()
NF = os.path.join(nf, "results.md")
for fid in ("backup", "image-ok"):
    mo.append_record(NF, fid, "confirm", [("result", "CONFIRMED"), ("by", "Marco")])
mo.append_record(NF, "E", "auto", [("result", "PASS")]); mo.append_record(NF, "E", "verdict", [("result", "PASS")])
mo.append_record(NF, "W1a", "auto", [("result", "PASS"), ("decision", "CONTINUE")])
g = mo.gate_check(mo.parse_results(NF), r1)
check(g[0] is False and g[1] == ["confirm:fallback-e1 is NONE (needs CONFIRMED)"] and g[2] == [], "new folder, no D0-D4: the only thing missing is Marco's go e1 (hard)")
mo.append_record(NF, "fallback-e1", "confirm", [("result", "CONFIRMED"), ("by", "Marco")])
check(mo.gate_check(mo.parse_results(NF), r1)[0] is True, "go e1 opens W1c-e1 without any D0-D4, W2 or W2-e4 record")
ngd = tempfile.mkdtemp()
mo.append_record(os.path.join(ngd, "results.md"), "backup", "confirm", [("result", "CONFIRMED"), ("by", "Marco")])
gw = mo.gate_check(mo.parse_results(os.path.join(ngd, "results.md")), mo.RUNGS["W1a"])
check(gw[0] is False and any(m.startswith("D3 is NONE") for m in gw[2]) and not any("D3" in m for m in gw[1]),
      "W1a keeps its D3 gate SOFT: in a fresh folder it needs --force-gate (recorded), unchanged by this change")
for miss in ("backup", "image-ok", "E", "W1a", "rate"):
    f2 = tempfile.mkdtemp(); F2 = os.path.join(f2, "results.md")
    if miss != "backup": mo.append_record(F2, "backup", "confirm", [("result", "CONFIRMED"), ("by", "Marco")])
    if miss != "image-ok": mo.append_record(F2, "image-ok", "confirm", [("result", "CONFIRMED"), ("by", "Marco")])
    if miss != "E": mo.append_record(F2, "E", "auto", [("result", "PASS")]); mo.append_record(F2, "E", "verdict", [("result", "PASS")])
    if miss != "W1a": mo.append_record(F2, "W1a", "auto", [("result", "PASS"), ("decision", "CONTINUE" if miss != "rate" else "STOP")])
    mo.append_record(F2, "fallback-e1", "confirm", [("result", "CONFIRMED"), ("by", "Marco")])
    gg = mo.gate_check(mo.parse_results(F2), r1)
    check(gg[0] is False, "W1c-e1 gate closed when %s is missing" % miss)
    if miss in ("backup", "image-ok", "E"):
        check(len(gg[1]) >= 1, "... and the %s gate is HARD (not bypassable)" % miss)
    else:
        check(len(gg[2]) >= 1 and not gg[1], "... and the %s gate is SOFT (--force-gate with a reason)" % miss)
# a go from an earlier day does not count
sf = tempfile.mkdtemp()
with open(os.path.join(sf, "results.md"), "w") as fh:
    fh.write("### fallback-e1 confirm 2020-01-01T10:00:00\n- result: CONFIRMED\n- by: Marco\n\n")
check(mo.confirm_state(mo.parse_results(os.path.join(sf, "results.md")), "fallback-e1") == "STALE", "a 'go e1' of an earlier day (a reused folder) is STALE, not a go")
# another group's unresolved FAIL stays a soft stop for W1c-e1; a W1c failure does not block it (same family)
mo.append_record(NF, "D4", "auto", [("result", "PASS")]); mo.append_record(NF, "D4", "verdict", [("result", "FAIL")])
gg = mo.gate_check(mo.parse_results(NF), r1)
check(gg[0] is False and not gg[1] and any("D4 ended FAIL/ABORT" in m for m in gg[2]), "an unresolved D4 FAIL is a soft stop for W1c-e1 (confirm proceed:D4 or --force-gate)")
mo.append_record(NF, "proceed:D4", "confirm", [("result", "CONFIRMED"), ("by", "Marco")])
mo.append_record(NF, "W1c", "auto", [("result", "PASS")]); mo.append_record(NF, "W1c", "verdict", [("result", "FAIL"), ("note", "0xA0")])
check(mo.gate_check(mo.parse_results(NF), r1)[0] is True, "W1c FAIL (0xA0, the reason for W1c-e1) does not block W1c-e1")
check(mo.gate_check(mo.parse_results(NF), mo.RUNGS["W2"])[0] is False, "...but it still blocks W2 until Marco says proceed:W1c")
# end to end through the fake loader, dry run
b4e = tempfile.mkdtemp()
os.makedirs(os.path.join(b4e, "winram"))
with open(os.path.join(b4e, "winram", "winram.img"), "wb") as fh:
    fh.truncate(SIZE)
open(os.path.join(b4e, "winram", "winram.img.sha256"), "w").write(SHA16 + "0" * 48 + "  winram.img\n")
import hashlib as _h2
open(os.path.join(b4e, "WATARI_M1_EFI.fd"), "wb").write(b"main")
open(os.path.join(b4e, "WATARI_M1_EFI.fd.build.txt"), "wb").write(b"model=j293\nbench=0\nramdisk_standin=0\nbgr8=1\nbuild_tag=\nbuild_pcd=\ndirty=0\n")
open(os.path.join(b4e, "WATARI_M1_EFI-e1.fd"), "wb").write(b"e1fw")
open(os.path.join(b4e, "WATARI_M1_EFI-e1.fd.build.txt"), "wb").write(("model=j293-e1\nbench=0\nramdisk_standin=0\nbgr8=1\nbuild_tag=e1\nbuild_pcd=\ndirty=0\nfd_sha256=%s\n" % _h2.sha256(b"e1fw").hexdigest()).encode())
o2 = tempfile.mkdtemp()
O2 = os.path.join(o2, "results-dryrun.md")
RE1 = lambda *x, **kw: mon(b4e, o2, "rung", "W1c-e1", "--dry-run", "--quiet", "--loader", fake, *x, fixture_name="W1c", **kw)
code, txt = RE1()
check(code == mo.EX_GATE and "not bypassable" in txt, "W1c-e1 in a fresh folder: refused (backup)")
mo.append_record(O2, "backup", "confirm", [("result", "CONFIRMED"), ("by", "Marco")])
mo.append_record(O2, "image-ok", "confirm", [("result", "CONFIRMED"), ("by", "Marco")])
mo.append_record(O2, "E", "auto", [("result", "PASS")]); mo.append_record(O2, "E", "verdict", [("result", "PASS")])
mo.append_record(O2, "W1a", "auto", [("result", "PASS"), ("decision", "CONTINUE")])
code, txt = RE1("--force-gate", "x")
check(code == mo.EX_GATE and "confirm:fallback-e1" in txt and "not bypassable" in txt, "W1c-e1 refused without 'go e1', also with --force-gate")
check(mon(b4e, o2, "confirm", "fallback-e1", "--by", "Marco", "--note", "go e1 for W1c-e1", "--dry-run")[0] == 0, "confirm fallback-e1 recorded")
code, txt = RE1()
check(code == 0 and "RESULT W1c-e1: PASS" in txt and "WATARI_M1_EFI-e1.fd" in txt and "PHOTO CHECKLIST W1c-e1" in txt and "GATE" not in txt,
      "W1c-e1 runs through the fake loader with no D0-D4 and no force: PASS, firmware e1 in the command, photo checklist printed")
rec = open(O2).read()
check("### W1c-e1 auto" in rec and "WATARI_M1_EFI-e1.fd" in rec, "the run is recorded as W1c-e1 with the e1 firmware in its command")
rc = mo.parse_results(O2)
check(mo.effective(rc, "W1c-e1") == "PENDING" and mo.effective(rc, "W1c") == "NONE", "automatic PASS is PENDING until the photo verdict; W1c itself is untouched")
rid, lines, _ = mo.next_step(rc, "day4")
check(rid is None and "W1c-e1 ran" in " ".join(lines), "next: waits for the W1c-e1 verdict (no other rung offered)")
check(mon(b4e, o2, "verdict", "W1c-e1", "FAIL", "--note", "0xA0 again", "--dry-run")[0] == 0, "verdict W1c-e1 FAIL (note) accepted")
check(mo.effective(mo.parse_results(O2), "W1c-e1") == "FAIL" and mo.unresolved(mo.parse_results(O2)) == ["W1c"], "a W1c-e1 FAIL is an unresolved FAIL of the W1c family (confirm proceed:W1c)")
check(mon(b4e, o2, "verdict", "W1c-e1", "PASS", "--note", "spinner moved for 6 min", "--dry-run")[0] == 0 and mo.effective(mo.parse_results(O2), "W1c-e1") == "PASS", "verdict PASS accepted")
code, txt = mon(b4, o2, "rung", "W1c-e1", "--dry-run", "--quiet", "--loader", fake, "--force-bundle", fixture_name="W1c")
check(code == mo.EX_PRECOND and "FALLBACK FIRMWARE REFUSED" in txt, "a bundle without the e1 firmware: W1c-e1 refused before the loader")
open(os.path.join(b4e, "WATARI_M1_EFI-e1.fd"), "wb").write(b"tampered")
code, txt = RE1()
check(code == mo.EX_PRECOND and "FALLBACK FIRMWARE REFUSED" in txt and "SHA-256" in txt, "a tampered e1 firmware: W1c-e1 refused (check_fallback_fw)")
code, txt = mon(b4e, o2, "preflight", "--quick", "--no-record")
check("declared fallback firmware e1" in txt and "FAIL" in txt, "preflight re-checks the e1 firmware (same check_fallback_fw)")

print("== preflight and the bundle ==")
code, txt = mon(tempfile.mkdtemp(), out, "preflight", "--quick", "--no-record")
check(code == 1 and "missing:" in txt and "PREFLIGHT FAIL" in txt, "preflight on an empty folder fails with the missing files")
for cand in (os.environ.get("MONDAY_TEST_BUNDLE"),):
    if cand and os.path.isdir(cand):
        code, txt = mon(cand, out, "preflight", "--quick", "--no-record")
        check(code == 0 and "PREFLIGHT PASS" in txt, "preflight on %s passes" % cand)
bld = open(os.path.join(HERE, "build-m1-payload.sh")).read()
check('cp "$HERE/monday.py" "$OUT/"' in bld, "build-m1-payload.sh copies monday.py into the bundle (so it is in SHA256SUMS)")
check('monday.py' in open(os.path.join(HERE, "README-m1-payload.md")).read(), "README-m1-payload.md lists monday.py")
check('cp "$HERE/idregs.py" "$OUT/"' in bld and "idregs.py" in open(os.path.join(HERE, "README-m1-payload.md")).read(), "the bundle build copies idregs.py and the README lists it")
check('cp "$HERE/hacr_probe.py" "$OUT/"' in bld and "hacr_probe.py" in open(os.path.join(HERE, "README-m1-payload.md")).read(), "the bundle build copies hacr_probe.py and the README lists it")

print("== the module itself ==")
src = open(os.path.join(HERE, "monday.py")).read()
check(not re.search(r"^(import|from) (requests|serial|construct|m1n1)", src, re.M), "stdlib only: no third-party import")
check("m1n1.setup" not in src.replace("never from m1n1.setup", ""), "monday.py never imports m1n1.setup")
check("shutil.rmtree" not in src and "os.remove" not in src and "unlink" not in src, "monday.py deletes nothing")

print("\n%s: %d check(s) failed" % ("FAILED" if FAILED else "ALL OK", len(FAILED)))
sys.exit(1 if FAILED else 0)
