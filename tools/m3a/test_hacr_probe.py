#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fake-proxy test of hacr_probe.py. Run: python3 -I tools/m3a/test_hacr_probe.py   (no device, no m1n1 checkout).
Asserts: readback-only never runs anything at EL1; HACR_EL2 is the only register written and is always restored (also after an error);
nothing is written when CurrentEL != 2 or HACR_EL2 is unreadable; the guard is set first and restored; bit 56 sticking / not sticking is
reported; with --el1 an EC 0x3F / EC 0x18 arrival at EL2 and a plain run are told apart; dry-run makes no call; the source never imports
m1n1.setup and has no memory write / smp call."""
import contextlib
import importlib.util
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("hacr_probe", os.path.join(HERE, "hacr_probe.py"))
hp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hp)
FAILED = []


def check(c, m):
    print(("ok   " if c else "FAIL ") + m)
    if not c:
        FAILED.append(m)


NAMES = {v: k for k, v in hp.ENC.items()}
check(hp.ENC["HACR_EL2"] == (3, 4, 1, 1, 7) and hp.ENC["AFSR1_EL2"] == (3, 4, 5, 1, 1) and hp.ENC["ESR_EL2"] == (3, 4, 5, 2, 0),
      "encodings: HACR_EL2 S3_4_C1_C1_7, AFSR1_EL2 S3_4_C5_C1_1, ESR_EL2 S3_4_C5_C2_0")
check(hp.HACR_PMU == 1 << 56, "bit 56")


class FakeProxy:
    """Only set_exc_guard and get_exc_count exist."""

    def __init__(self):
        self.log, self.exc = [], 0

    def __getattr__(self, n):
        raise AssertionError("forbidden proxy call: " + n)

    def set_exc_guard(self, m):
        self.log.append(("guard", m))

    def get_exc_count(self):
        c, self.exc = self.exc, 0
        return c


class FakeU:
    """A HACR_EL2 whose writable bits are `writable`; the EL1 PMCCNTR access behaves as `el1_mode`:
    'ec3f' / 'ec18' (arrives at EL2: ESR_EL2 / AFSR1_EL2 change, the guard counts), 'undef' (counted, ESR_EL2 unchanged), 'ran'."""

    def __init__(self, p, hacr=0x20, writable=~0, el=2, el1_mode="ec3f", fail_read=False, pmcr0=0x1, pmcr1=0x10100, pmc0_step=3000000,
                 pmc0_fault=False):
        self.p, self.hacr, self.writable, self.el, self.mode, self.fail_read = p, hacr, writable, el, el1_mode, fail_read
        self.pmcr0, self.pmcr1, self.pmc0, self.step, self.pmc0_fault, self.cnt = pmcr0, pmcr1, 1000, pmc0_step, pmc0_fault, 5000
        self.esr, self.afsr1 = 0, 0
        self.reads, self.writes, self.el1_calls = [], [], 0

    def __getattr__(self, n):
        raise AssertionError("forbidden u call: " + n)

    def mrs(self, enc, call=None):
        name = NAMES[enc]
        self.reads.append((name, call))
        if call == "el1":
            self.el1_calls += 1
            if self.mode == "ec3f":
                self.esr, self.afsr1 = (0x3f << 26) | (1 << 25), (3 << 20) | (13 << 1) | 1 | (3 << 14) | (9 << 10)
                self.p.exc += 1
            elif self.mode == "ec18":
                self.esr = (0x18 << 26) | (1 << 25) | (3 << 20) | (3 << 14) | (9 << 10) | (13 << 1) | 1
                self.p.exc += 1
            elif self.mode == "undef":
                self.p.exc += 1
            return 0x1234 if self.mode == "ran" else 0
        if name == "CurrentEL":
            return self.el << 2
        if name == "HACR_EL2":
            if self.fail_read:
                self.p.exc += 1
                return 0
            return self.hacr
        if name == "PMCR0":
            return self.pmcr0
        if name == "PMCR1":
            return self.pmcr1
        if name == "CNTFRQ_EL0":
            return 24000000
        if name == "CNTPCT_EL0":
            v, self.cnt = self.cnt, self.cnt + 48000          # 2 ms between the two samples
            return v
        if name == "PMC0":
            if self.pmc0_fault:
                self.p.exc += 1
                return 0
            v = self.pmc0
            self.pmc0 += self.step if (self.pmcr0 & 1) and (self.pmcr1 & 0x10100) else 0
            return v
        if name == "ESR_EL2":
            return self.esr
        if name == "AFSR1_EL2":
            return self.afsr1
        raise AssertionError("unexpected read " + name)

    def msr(self, enc, val):
        name = NAMES[enc]
        self.writes.append((name, val))
        assert name == "HACR_EL2", "the probe may only write HACR_EL2, wrote " + name
        self.hacr = (self.hacr & ~self.writable) | (val & self.writable)


def go(el1=False, **kw):
    p = FakeProxy()
    u = FakeU(p, **kw)
    lines = []
    rec = hp.run(p, u, lines.append, "ON", "OFF", el1, delay=lambda: None)
    return p, u, rec, "\n".join(lines)


p, u, rec, txt = go()
check(p.log[0] == ("guard", "ON") and p.log[-1] == ("guard", "OFF"), "guard set before the first read and restored at exit")
check(u.el1_calls == 0 and rec["mode"] == "readback-only" and "EL1 attempt not requested" in txt, "readback-only never runs anything at EL1")
check(u.writes == [("HACR_EL2", 0x20 | hp.HACR_PMU), ("HACR_EL2", 0x20)] and u.hacr == 0x20, "HACR_EL2 written with bit 56, then restored to the old value")
check(rec["bit56_sticks"] is True and "STICKS" in txt and rec["restored"] is True, "bit 56 sticks: reported, restore verified")

p, u, rec, txt = go(writable=~hp.HACR_PMU & hp.MASK64)
check(rec["bit56_sticks"] is False and "DOES NOT STICK" in txt and u.hacr == 0x20, "bit 56 RES0: reported as not sticking, value restored")
p, u, rec, txt = go(el1=True, writable=~hp.HACR_PMU & hp.MASK64)
check(u.el1_calls == 0 and "EL1 attempt skipped" in txt, "--el1 with a bit that does not stick: no EL1 access")

p, u, rec, txt = go(el=1)
check(u.writes == [] and rec["stopped"] == "not EL2", "CurrentEL != 2: nothing written")
p, u, rec, txt = go(fail_read=True)
check(u.writes == [] and "HACR_EL2" in rec["faulted"] and rec["stopped"] == "HACR_EL2 unreadable", "unreadable HACR_EL2: nothing written")

p, u, rec, txt = go(el1=True, el1_mode="ec3f")
check(u.el1_calls == 1 and "ARRIVED AT EL2 as EC 0x3F" in rec["el1_verdict"] and "AFSR1_EL2=0x" in rec["el1_verdict"], "--el1: EC 0x3F arrival reported with AFSR1")
check(u.hacr == 0x20 and u.writes[-1] == ("HACR_EL2", 0x20), "--el1: HACR_EL2 restored afterwards")
p, u, rec, txt = go(el1=True, el1_mode="ec18")
check("ARRIVED AT EL2 as EC 0x18" in rec["el1_verdict"], "--el1: EC 0x18 arrival reported")
p, u, rec, txt = go(el1=True, el1_mode="undef")
check("EL1 UNDEF or unrelated" in rec["el1_verdict"], "--el1: an exception with no EC 0x18/0x3F in ESR_EL2 is reported as EL1 UNDEF or unrelated")
p, u, rec, txt = go(el1=True, el1_mode="ran")
check("RAN at EL1" in rec["el1_verdict"], "--el1: a plain run is reported")


class BoomU(FakeU):
    def mrs(self, enc, call=None):
        if call == "el1":
            raise RuntimeError("proxy boom")
        return FakeU.mrs(self, enc, call)


p = FakeProxy()
u = BoomU(p)
lines = []
hp.run(p, u, lines.append, "ON", "OFF", True)
check(u.hacr == 0x20 and "proxy error" in "\n".join(lines) and p.log[-1] == ("guard", "OFF"), "a proxy error at the EL1 step: HACR_EL2 restored, guard off, verdict says proxy error")

# the Apple PMU check: read only (the fake asserts that only HACR_EL2 is ever written), verdicts told apart
p, u, rec, txt = go()
check(rec["pmu"]["advances"] and rec["pmu"]["cycles_per_ms"] == 3000000 // 2 and "PMC0 advances" in txt, "PMC0 advances: rate reported (cycles per ms vs CNTPCT)")
check(all(w[0] == "HACR_EL2" for w in u.writes) and ("PMC0", None) in u.reads and ("PMCR0", None) in u.reads and ("PMCR1", None) in u.reads,
      "the PMU is only read (PMCR0, PMCR1, PMC0), never written")
p, u, rec, txt = go(pmcr0=0, pmcr1=0)
check(not rec["pmu"]["advances"] and "not enabled in this context" in txt and "the layer enables them itself" in txt, "PMC0 disabled in the proxy context: reported, no PMU write")
check(all(w[0] == "HACR_EL2" for w in u.writes), "no PMU write when counter 0 is disabled")
p, u, rec, txt = go(pmc0_step=0)
check(not rec["pmu"]["advances"] and "although PMCR0 / PMCR1 enable it" in txt, "PMC0 enabled but stuck: told apart from disabled")
p, u, rec, txt = go(pmc0_fault=True)
check(rec["pmu"]["advances"] is False and "cannot be read" in txt and u.hacr == 0x20, "PMC0 read faults: reported, HACR_EL2 still restored")
p, u, rec, txt = go(el=1)
check("pmu" not in rec and u.writes == [], "not at EL2: the PMU is not read either")

buf = io.StringIO()
with contextlib.redirect_stdout(buf):
    rc = hp.main(["--dry-run"])
check(rc == 0 and "DRY RUN" in buf.getvalue() and "ALWAYS write the old value back" in buf.getvalue(), "dry-run prints the plan and calls no proxy")
src = open(os.path.join(HERE, "hacr_probe.py")).read()
code = re.sub(r'""".*?"""', "", src, count=1, flags=re.S)
check("import m1n1.setup" not in code and "from m1n1.setup" not in code and not re.search(r"\.(write\w*|set32|clear32|mask32|memcpy|smp_\w+)\(", code),
      "source: no m1n1.setup, no memory write / smp call")

print("\n%s: %d check(s) failed" % ("FAILED" if FAILED else "ALL OK", len(FAILED)))
sys.exit(1 if FAILED else 0)
