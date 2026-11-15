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
check(a("D4a") == "--rung D2 --edk2 WATARI_M1_EFI.fd --fb band --go --cpuif trap-notc --pmlog --args wel2.hold=1" and "--smp" not in a("D4a"), "D4a is trap-notc without --smp")
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
    check(all(g[0].startswith("confirm:") or g[0] in mo.RUNGS or g[0] in mo.VERDICT_OWNER or g[0] == "rate" for g in r["gate"]), "%s: gate ids are known" % rid)

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
    ("first memory range too small", re.sub(r"reg = <0x8 0x44100000 0x1 0xbbf00000>", "reg = <0x8 0x44100000 0x0 0x4000000>", base), "first /memory range holds the EDK2 block"),
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
for wr, want in ((7.0, "CONTINUE"), (2.5, "CONTINUE"), (2.0, "ASK"), (0.9, "ASK"), (0.8, "STOP"), (0.4, "STOP"), (0.0, "STOP")):
    check(mo.rate_decision(wr, SIZE)[0] == want, "%.1f MB/s -> %s" % (wr, want))
for wr, want in ((3.1, "CONTINUE"), (1.3, "ASK"), (0.6, "STOP"), (0.3, "STOP")):
    t = re.sub(r"^RATE TEST: .*$", "RATE TEST: write %.2f MB/s, read 9.00 MB/s, read-back identical" % wr, fixture("W1a"), flags=re.M)
    ov, res, f = run("W1a", t, dry=False)
    check(ov == "PASS" and f["rate_decision"] == want, "W1a on a device at %.1f MB/s: decision %s" % (wr, want))
t = re.sub(r"^RATE TEST: .*$", "RATE TEST: FAIL - read-back differs in the slice at offset 0x1000000", fixture("W1a"), flags=re.M)
check(run("W1a", t, dry=False)[0] == "FAIL", "a read-back difference FAILs the rate test")
t = re.sub(r"^RATE TEST: .*$", "RATE TEST: write 2.00 MB/s, read 9.00 MB/s, read-back not compared (dry run)", fixture("W1a"), flags=re.M)
check(run("W1a", t, dry=False)[0] == "FAIL", "'not compared' is not accepted on a device")
check(run("W1a2", fixture("W1a-nocsum"))[0] == "PASS", "--no-data-csums output is accepted (the 'data checksums disabled' line)")
check(run("W1a2", fixture("W1a"))[0] == "FAIL", "--no-data-csums needs its 'data checksums disabled' line")

print("== --pmlog on the EDK2 jump rungs, PM0, the post-mortem reader (T16j) ==")
jumps = [x for x in mo.ORDER if mo.RUNGS[x]["jump"]]
for x in jumps:
    has = "--pmlog" in mo.RUNGS[x]["args"].split()
    want = "--edk2" in mo.RUNGS[x]["args"] or "--rung D2" in mo.RUNGS[x]["args"]
    check(has == want, "%s: --pmlog %s (every EDK2 jump rung, not D1's test-guest L5)" % (x, "present" if has else "absent"))
check(all("--pmlog --args wel2.hold=1" in mo.RUNGS[x]["args"] for x in ("D2", "D4", "D5", "W1c", "W2", "W2g", "W2-e4", "W2-e1")), "the ring flag sits right before --args wel2.hold=1 (as in the procedures)")
check("--pmlog" not in a("D0") and "--pmlog" not in a("W1b") and "--pmlog" not in a("D1"), "previews and D1 do not set up a ring")
check(mo.RUNGS["PM0"]["kind"] == "postmortem" and not mo.RUNGS["PM0"]["jump"] and mo.RUNGS["PM0"]["args"] == "--postmortem", "PM0 is a read-only [check] rung (loader --postmortem)")
check(mo.rung_command_text(mo.RUNGS["PM0"]) in docs[3], "PM0 command appears in the day-3 procedure: %s" % mo.rung_command_text(mo.RUNGS["PM0"]))
d0, d2 = fixture("D0"), fixture("D2")
d0_addr = {k: v for k, v in re.findall(r"^BLOCK  (layer|edk2) .* at (0x[0-9a-f]+)", d0, re.M)}
d2_addr = {"layer": re.search(r"^LAYER  .* at (0x[0-9a-f]+)", d2, re.M).group(1), "edk2": re.search(r"^EDK2   .* at (0x[0-9a-f]+)", d2, re.M).group(1)}
check(d0_addr == d2_addr and len(d0_addr) == 2, "D2 --pmlog keeps D0's layer and EDK2 block addresses %s (the ring takes no block of its own)" % d0_addr)
pmm = re.search(r"^PMLOG  region 0x([0-9a-f]+)\.\.0x([0-9a-f]+) \(2048 KiB, zeroed, start of the spacer block", d2, re.M)
spc = re.search(r"^SPACER 32 MB aligned block at 0x([0-9a-f]+)", d2, re.M)
edk = re.search(r"^EDK2   \S+ +\d+ bytes at 0x([0-9a-f]+)", d2, re.M)
d0g = re.search(r"^BLOCK  guest .* at 0x([0-9a-f]+)", d0, re.M)
check(bool(pmm and spc and edk and d0g) and int(pmm.group(1), 16) == int(spc.group(1), 16) == int(d0g.group(1), 16) and int(spc.group(1), 16) + (32 << 20) == int(edk.group(1), 16),
      "D2 --pmlog: the ring is at the start of the spacer block (= D0's 'guest' block), right below EDK2: the entry's range stays whole (the 64 MB placement rule; a ring above EDK2 is refused, code 5)")
check("GUEST FDT CHECK: PASS" in d2, "D2 --pmlog dry run: GUEST FDT CHECK still PASS (gfdt_dump --pmlog accepted the placement)")
w1c = fixture("W1c")
rdb = re.search(r"wel2\.ramdisk=0x([0-9a-f]+),", w1c)
pmw = re.search(r"wel2\.pmlog=0x([0-9a-f]+),0x200000", w1c)
check(bool(rdb) and bool(pmw) and int(pmw.group(1), 16) % (32 << 20) == 0 and int(pmw.group(1), 16) > int(rdb.group(1), 16), "W1c: wel2.pmlog=<32 MB aligned>,0x200000 after wel2.ramdisk in the boot arguments")
for fx, rid in (("D2", "D2"), ("D4", "D4"), ("D5", "D5"), ("W1c", "W1c"), ("W2", "W2")):
    ov, res, facts = run(rid, fixture(fx))
    check(ov == "PASS" and facts.get("pmlog_base") and facts.get("pmlog_size") == 2 << 20 and facts.get("pmlog_arg"), "%s: PMLOG/POSTMORTEM lines checked, base %s recorded as a fact" % (rid, facts.get("pmlog_base")))
base2 = hardware(fixture("D2"))
for what, t, expect in (
    ("no PMLOG line", re.sub(r"^PMLOG  region.*$", "", base2, flags=re.M), "PMLOG region"),
    ("no POSTMORTEM line", re.sub(r"^POSTMORTEM base.*$", "", base2, flags=re.M), "POSTMORTEM base recorded"),
    ("pmlog argument missing", re.sub(r" wel2\.pmlog=0x[0-9a-f]+,0x[0-9a-f]+", "", base2), "boot arguments"),
    ("pmlog base differs from the region", re.sub(r"wel2\.pmlog=0x[0-9a-f]+,", "wel2.pmlog=0x10000000,", base2), "pmlog base agrees everywhere"),
):
    ov, res, _ = run("D2", t, dry=False)
    check(ov == "FAIL" and expect in failing(res), "D2 %s -> FAIL (%s)" % (what, expect))

import struct
import zlib
def ring(text, size=0x4000, boots=1, seal=True):
    cap = size - 64
    data = bytearray(size)
    body = text.encode()
    data[64:64 + len(body)] = body
    hdr = struct.pack("<8sIIIIQII20s", b"WEL2PMLG", 1, 64, cap, boots, len(body), len(body) // cap, 0, b"\0" * 20)
    crc = zlib.crc32(hdr) & 0xffffffff
    data[:64] = hdr + struct.pack("<I", crc if seal else crc ^ 1)
    return bytes(data)
PM_TEXT = ("Watari EL2: post-mortem log ring at 0x0000000840000000\n"
           "Watari EL2: checkpoint 7 BOOT_GUEST\n"
           "Watari EL2: PSCI CPU_ON cpu 1 accepted\nWatari EL2: PSCI CPU_ON cpu 2 accepted\nWatari EL2: PSCI CPU_ON cpu 1 accepted\n"
           "Watari EL2: M1 CPU_ON failed: no usable release address or already released, ret = 0xfffffffffffffffd\n"
           "Watari EL2: checkpoint 8 ENTER_GUEST\n"
           "Watari EL2: stats: guest ICC_SGI*R writes trapped = 0x000000000006540\n"
           "Watari EL2: stats: LR injections    = 0x0000000000000011\n"
           "Watari EL2: FATAL: unhandled guest abort, halted\n")
sm = mo.pm_summary(PM_TEXT)
check(sm["last_checkpoint"] == "8 ENTER_GUEST" and sm["cpu_on_ok"] == [1, 2] and len(sm["cpu_on_failed"]) == 1 and len(sm["fatal"]) == 1
      and sm["stats"] == 2 and "ICC_SGI*R" in sm["sgi_line"] and not sm["hold"] and sm["lines"] == 10, "pm_summary: last checkpoint, CPU_ON (unique + failed), FATAL, stats, hold: %s" % {k: sm[k] for k in ("last_checkpoint", "cpu_on_ok", "stats")})
check(mo.pm_summary("")["last_checkpoint"] is None and mo.pm_summary("")["fatal"] == [], "pm_summary of an empty log")
check(all(l for l in mo.pm_summary_lines(sm)) and any("FATAL" in l for l in mo.pm_summary_lines(sm)), "the summary lines print")


print("== results file and gate logic ==")
fake = os.path.join(tempfile.mkdtemp(), "fake_loader.py")
open(fake, "w").write('import os, sys\nsys.stdout.write(open(os.environ["FAKE_FIXTURE"]).read().rsplit("exit=", 1)[0])\nsys.exit(int(os.environ.get("FAKE_EXIT", "0")))\n')
tmpd = tempfile.mkdtemp()
PM_GOOD, PM_TORN, PM_NONE = (os.path.join(tmpd, n) for n in ("good.bin", "torn.bin", "none.bin"))
open(PM_GOOD, "wb").write(ring(PM_TEXT, boots=2))
open(PM_TORN, "wb").write(ring(PM_TEXT, seal=False))
open(PM_NONE, "wb").write(b"\0" * 0x4000)
# a loader for `postmortem`: the REAL watari-load.py run_postmortem on a fake proxy that serves a synthetic ring (FAKE_PM_REGION) at the base it was given
fake_pm = os.path.join(tmpd, "fake_pm_loader.py")
open(fake_pm, "w").write("""import importlib.util, os, sys
spec = importlib.util.spec_from_file_location("wl", %r)
wl = importlib.util.module_from_spec(spec); spec.loader.exec_module(wl)
class T:
    dry_run = False
    def read(self, addr, n):
        i = sys.argv.index("--pmlog-base"); base = int(sys.argv[i + 1], 0)
        data = open(os.environ["FAKE_PM_REGION"], "rb").read()
        off = addr - base
        return (data[off:off + n] + b"\\0" * n)[:n]
sys.exit(wl.main(sys.argv[1:] + ["--bundle", os.getcwd()], T()))
""" % os.path.join(HERE, "watari-load.py"))


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
check("RUN:  python3 monday.py postmortem" in txt and "PM0" in txt and "rung D4" not in txt, "next: PM0 (post-mortem check) comes right after the D2/D3 verdicts, before D4")
os.environ["FAKE_PM_REGION"] = PM_GOOD
code, txt = mon(b3, out, "postmortem", "--dry-run", "--loader", fake_pm)
check(code == 0 and "RESULT PM0: RECOVERED" in txt, "PM0 through the real loader's --postmortem on a synthetic ring: RECOVERED")
code, txt = mon(b3, out, "next", "--dry-run")
check("RUN:  python3 monday.py rung D4 --marco-go" in txt and "Italiano" in txt, "next: D4 is the next rung once PM0 is recorded (jump, so --marco-go)")
code, txt = R("D4", "D4")
check(code == 0, "D4 runs")
check(V("D4", "ABORT") == 0, "verdict D4 ABORT")
code, txt = mon(b3, out, "next", "--dry-run")
check("STOP" in txt and "D4" in txt and "RUN:" not in txt, "next after an ABORT: stop and ask Marco, no rung offered")
check("FIRST the post-mortem log, THEN the photos" in txt and "python3 monday.py postmortem" in txt, "next after an ABORT of a ring-carrying jump: postmortem first, then photos")
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
code, txt = mon(b3, out, "next", "--dry-run")
check("day-4 bundle" in txt or "switch to the day-4" in txt.lower() or "Switch to the day-4" in txt, "next: D0-D4 PASS, tells to switch to the day-4 bundle")
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
# --- postmortem command: no source, NOLOG, UNVERIFIED, RECOVERED, ids, next logic ---
pd = tempfile.mkdtemp()
PMR = lambda *extra: mon(b3, pd, "postmortem", "--dry-run", "--loader", fake_pm, *extra)
code, txt = PMR()
check(code == mo.EX_PRECOND and "no recorded jump run" in txt, "postmortem without a recorded ring-carrying jump run is refused")
mo.append_record(os.path.join(pd, "results-dryrun.md"), "W2", "auto", [("result", "PASS"), ("pmlog-base", "0x844000000")])
os.environ["FAKE_PM_REGION"] = PM_NONE
code, txt = PMR()
rec = open(os.path.join(pd, "results-dryrun.md")).read()
check(code == 0 and "RESULT PM-W2: NOLOG" in txt and "solo foto" in txt and "### PM-W2 auto" in rec and "- dram-kept: no" in rec and "- loader-exit: 5" in rec,
      "exit 5 (no header): NOLOG recorded as PM-W2 (source W2), dram-kept no, 'photos only' said in Italian")
check(not os.path.exists(os.path.join(pd, "pmlog.txt")), "NOLOG leaves no pmlog.txt behind")
os.environ["FAKE_PM_REGION"] = PM_TORN
code, txt = PMR()
check(code == 0 and "RESULT PM-W2: UNVERIFIED" in txt and "FATAL: unhandled guest abort" in txt and "header CRC BAD" in txt, "exit 6 (CRC bad): the text is still shown, flagged UNVERIFIED")
os.environ["FAKE_PM_REGION"] = PM_GOOD
code, txt = PMR()
rec = open(os.path.join(pd, "results-dryrun.md")).read()
check(code == 0 and "RESULT PM-W2: RECOVERED" in txt and "last 40 lines of 10" in txt and "SUMMARY (verified)" in txt and "last checkpoint : 8 ENTER_GUEST" in txt, "RECOVERED: last lines and summary printed")
check("- last-checkpoint: 8 ENTER_GUEST" in rec and "- fatal-lines: 1" in rec and "- cpu-on-accepted: 2 [1, 2]" in rec and "- cpu-on-failed: 1" in rec and "- stats-lines: 2" in rec
      and "- dram-kept: yes" in rec and "- source: W2" in rec and "boot counter 2" in rec and "- pmlog-base: 0x844000000" in rec, "the record: checkpoint, FATAL count, CPU_ON counts, stats lines, boot counter, base")
check(os.path.exists(os.path.join(pd, "pmlog.txt")) and "FATAL" in open(os.path.join(pd, "pmlog.txt")).read()
      and any(f.endswith("-PM-W2-pmlog.txt") for f in os.listdir(pd)) and any(f.endswith("-PM-W2.log") for f in os.listdir(pd)), "out/pmlog.txt, a stamped copy and the loader log are saved next to the rung logs")
os.environ["FAKE_PM_REGION"] = PM_NONE
code, txt = PMR()
check("RESULT PM-W2: NOLOG" in txt and "SUMMARY" not in txt, "a stale pmlog.txt from an earlier run is not re-read after a NOLOG run")
code, txt = mon(b3, pd, "postmortem", "--dry-run", "--loader", fake_pm, "--rung", "D4")
check(code == mo.EX_PRECOND, "--rung D4 without a recorded D4 ring: refused")
# next logic on records
rp = tempfile.mkdtemp()
RP = os.path.join(rp, "results.md")
for fid in ("backup",):
    mo.append_record(RP, fid, "confirm", [("result", "CONFIRMED"), ("by", "Marco")])
for fid in ("D0", "D1"):
    mo.append_record(RP, fid, "verdict", [("result", "PASS")])
mo.append_record(RP, "D2", "auto", [("result", "PASS"), ("pmlog-base", "0x846000000")])
mo.append_record(RP, "D2", "verdict", [("result", "PASS")])
mo.append_record(RP, "D3", "verdict", [("result", "PASS")])
recs = mo.parse_results(RP)
rid, lines, _ = mo.next_step(recs, "day3")
check(rid == "PM0" and "post-mortem" in " ".join(lines) and "monday.py postmortem" in " ".join(lines), "next: after D2/D3 PASS with a ring, PM0 is proposed first")
check(mo.gate_check(recs, mo.RUNGS["D4"])[0] is True, "PM0 is a [check], not a gate: D4 stays open")
mo.append_record(RP, "confirm-skip", "note", [("x", "1")])
mo.append_record(RP, "skip-pm0", "confirm", [("result", "CONFIRMED"), ("by", "Marco")])
rid, lines, _ = mo.next_step(mo.parse_results(RP), "day3")
check(rid == "D4", "confirm skip-pm0 waives PM0: next is D4")
mo.append_record(RP, "PM0", "auto", [("result", "NOLOG"), ("source", "D2"), ("pmlog-base", "0x846000000")])
rid, lines, _ = mo.next_step(mo.parse_results(RP), "day3")
check(rid == "D4", "PM0 recorded (also NOLOG): next is D4")
mo.append_record(RP, "D4", "auto", [("result", "PASS"), ("pmlog-base", "0x846000000")])
mo.append_record(RP, "D4", "verdict", [("result", "FAIL"), ("note", "frozen")])
rid, lines, _ = mo.next_step(mo.parse_results(RP), "day3")
check(rid is None and "FIRST the post-mortem log, THEN the photos" in " ".join(lines) and "FAIL/ABORT" in " ".join(lines), "next after a D4 FAIL with a ring: postmortem first, then the photos")
mo.append_record(RP, "PM-D4", "auto", [("result", "RECOVERED"), ("source", "D4"), ("pmlog-base", "0x846000000")])
rid, lines, _ = mo.next_step(mo.parse_results(RP), "day3")
check("FIRST the post-mortem log" not in " ".join(lines), "after the postmortem of the failed run, next no longer asks for it")
check(mo.pm_pending(mo.parse_results(RP)) is None, "pm_pending is None once read back")
rn = tempfile.mkdtemp()
mo.append_record(os.path.join(rn, "results.md"), "backup", "confirm", [("result", "CONFIRMED")])
mo.append_record(os.path.join(rn, "results.md"), "D0", "verdict", [("result", "PASS")])
mo.append_record(os.path.join(rn, "results.md"), "D1", "verdict", [("result", "PASS")])
mo.append_record(os.path.join(rn, "results.md"), "D2", "auto", [("result", "PASS")])
mo.append_record(os.path.join(rn, "results.md"), "D2", "verdict", [("result", "PASS")])
mo.append_record(os.path.join(rn, "results.md"), "D3", "verdict", [("result", "PASS")])
check(mo.next_step(mo.parse_results(os.path.join(rn, "results.md")), "day3")[0] == "D4", "a D2 run without a ring (older records): no PM0 prompt")
code, txt = mon(b3, pd, "rung", "PM0", "--dry-run", "--loader", fake_pm)
check(code == 0 and "post-mortem log of the W2 run" in txt, "`rung PM0` is an alias of `postmortem`")
check(mon(b3, pd, "confirm", "skip-pm0", "--by", "Marco", "--dry-run")[0] == 0, "confirm skip-pm0 is a known key")

# rate gating from records (device case)
rd = tempfile.mkdtemp()
mo.append_record(os.path.join(rd, "results.md"), "confirm-dummy", "note", [("x", "1")])
for fid, key in (("backup", None), ("image-ok", None)):
    mo.append_record(os.path.join(rd, "results.md"), fid, "confirm", [("result", "CONFIRMED"), ("by", "Marco")])
for fid in ("D0", "D1", "D2", "D3", "D4"):
    mo.append_record(os.path.join(rd, "results.md"), fid, "verdict", [("result", "PASS")])
mo.append_record(os.path.join(rd, "results.md"), "W1a", "auto", [("result", "PASS"), ("decision", "ASK"), ("rate-write", "1.5"), ("projected-seconds", "498")])
recs = mo.parse_results(os.path.join(rd, "results.md"))
check(mo.gate_check(recs, mo.RUNGS["W2"])[0] is False and "rate is WAIT" in " ".join(mo.gate_check(recs, mo.RUNGS["W2"])[2]), "rate ASK without Marco's rate-ok: W2 gate closed")
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

print("== the module itself ==")
src = open(os.path.join(HERE, "monday.py")).read()
check(not re.search(r"^(import|from) (requests|serial|construct|m1n1)", src, re.M), "stdlib only: no third-party import")
check("m1n1.setup" not in src.replace("never from m1n1.setup", ""), "monday.py never imports m1n1.setup")
check("shutil.rmtree" not in src and "os.remove" not in src and "unlink" not in src, "monday.py deletes nothing")

print("\n%s: %d check(s) failed" % ("FAILED" if FAILED else "ALL OK", len(FAILED)))
sys.exit(1 if FAILED else 0)
