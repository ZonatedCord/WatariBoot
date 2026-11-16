#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fake-proxy test of idregs.py. Run: python3 -I tools/m3a/test_idregs.py   (no device, no m1n1 checkout).
Asserts: the decoder against values measured on the M1 (re/m3a-ehw0-measurements.md), the verdict block, that only
set_exc_guard / get_exc_count / u.mrs are ever called (any other proxy attribute raises), the guard is set before the first read and restored,
an unimplemented register is reported as faulted, ZFR0/SMFR0/EL2 registers are skipped when they must be, dry-run makes no call,
and the source has no msr, no write call and never imports m1n1.setup."""
import contextlib
import importlib.util
import io
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("idregs", os.path.join(HERE, "idregs.py"))
ir = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ir)
FAILED = []


def check(c, m):
    print(("ok   " if c else "FAIL ") + m)
    if not c:
        FAILED.append(m)


PFR0 = 0x1101000010110111      # measured on this M1 (re/m3a-ehw0-measurements.md)
MMFR0 = 0x000012120f100001
MMFR1 = 0x0000000011212100

f = ir.fields("ID_AA64PFR0_EL1", PFR0)
check(f["GIC"] == 0 and f["EL3"] == 0 and f["EL2"] == 1 and f["EL1"] == 1 and f["EL0"] == 1, "PFR0 measured: GIC=0, EL3=0, EL0/1/2 = 1")
check(f["SVE"] == 0 and f["AMU"] == 0 and f["MPAM"] == 0 and f["SEL2"] == 0 and f["RAS"] == 1 and f["FP"] == 1 and f["AdvSIMD"] == 1,
      "PFR0 measured: SVE/AMU/MPAM/SEL2 = 0, RAS = 1, FP = AdvSIMD = 1")
check(f["DIT"] == 1 and f["CSV2"] == 1 and f["CSV3"] == 1 and f["RME"] == 0, "PFR0 measured: DIT = CSV2 = CSV3 = 1")
m0 = ir.fields("ID_AA64MMFR0_EL1", MMFR0)
check(m0["PARange"] == 1 and m0["TGran4"] == 0 and m0["TGran16"] == 1 and m0["TGran64"] == 0xf and m0["TGran4_2"] == 2 and m0["TGran64_2"] == 1,
      "MMFR0 measured: PARange 1, TGran4 0, TGran16 1, TGran64 0xf, stage-2 4K=2 64K=1")
check(ir.fields("ID_AA64MMFR1_EL1", MMFR1)["VH"] == 1, "MMFR1 measured: VH = 1")
d = ir.fields("ID_AA64DFR0_EL1", (6 << 8) | 8 | (0xf << 12))
check(d["PMUVer"] == 6 and d["DebugVer"] == 8 and d["BRPs"] == 0xf and d["WRPs"] == 0, "DFR0 field positions: PMUVer [11:8], DebugVer [3:0], BRPs [15:12], WRPs [23:20]")
check(ir.fields("ID_AA64ISAR0_EL1", 1 << 60)["RNDR"] == 1 and ir.fields("ID_AA64ISAR0_EL1", 2 << 52)["TS"] == 2, "ISAR0: RNDR [63:60], TS [55:52]")
check(ir.fields("ID_AA64ISAR2_EL1", 2)["WFxT"] == 2 and ir.fields("ID_AA64MMFR0_EL1", 1 << 56)["FGT"] == 1 and ir.fields("ID_AA64MMFR0_EL1", 2 << 60)["ECV"] == 2,
      "ISAR2.WFxT [3:0], MMFR0.FGT [59:56], MMFR0.ECV [63:60]")
check(ir.fields("ID_AA64PFR1_EL1", 1 << 24)["SME"] == 1 and ir.fields("ID_AA64PFR1_EL1", 1)["BT"] == 1, "PFR1: SME [27:24], BT [3:0]")
check(ir.fields("MIDR_EL1", 0x610f0220)["Implementer"] == 0x61 and ir.fields("MIDR_EL1", 0x610f0220)["PartNum"] == 0x22, "MIDR decode")
check(ir.fields("HCR_EL2", 1 << 34)["E2H"] == 1 and ir.fields("HCR_EL2", 1 << 27)["TGE"] == 1, "HCR_EL2: E2H bit 34, TGE bit 27")


class FakeProxy:
    """Only set_exc_guard and get_exc_count exist. Anything else raises."""

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
    def __init__(self, p, vals):
        self.p, self.vals, self.reads = p, vals, []

    def __getattr__(self, n):
        raise AssertionError("forbidden u call: " + n)

    def mrs(self, enc):
        name = {v: k for k, v in ir.ENC.items()}[enc]
        self.reads.append(name)
        self.p.log.append(("mrs", name))
        if name not in self.vals:
            self.p.exc += 1                  # UNDEFINED: the guard skips and counts
            return 0xabad1dea
        return self.vals[name]


BASE = {"CurrentEL": 2 << 2, "ID_AA64PFR0_EL1": PFR0, "ID_AA64PFR1_EL1": 0x10, "ID_AA64MMFR0_EL1": MMFR0, "ID_AA64MMFR1_EL1": MMFR1,
        "ID_AA64DFR0_EL1": 0x0000000010305408 | (0xf << 8), "ID_AA64ISAR0_EL1": 0x1221111110212120, "MIDR_EL1": 0x610f0220,
        "HCR_EL2": 1 << 34, "MDCR_EL2": 0x100, "CNTHCTL_EL2": 3}
for n in ir.ID_REGS:
    BASE.setdefault(n, 0)


def go(vals, probe=True):
    p = FakeProxy()
    u = FakeU(p, vals)
    lines = []
    rec = ir.run(p, u, lines.append, "ON", "OFF", probe)
    return p, u, rec, "\n".join(lines)


p, u, rec, txt = go(dict(BASE))
check(p.log[0] == ("guard", "ON") and p.log[-1] == ("guard", "OFF"), "guard set before the first read and restored at exit")
check("ID_AA64ZFR0_EL1" not in u.reads and "ID_AA64SMFR0_EL1" not in u.reads, "ZFR0/SMFR0 not read when SVE == 0 and SME == 0")
check("HCR_EL2" in u.reads and "MDCR_EL2" in u.reads and "CNTHCTL_EL2" in u.reads, "EL2 registers read at EL2")
check(rec["faulted"].keys() == {"PMCR_EL0", "OSLSR_EL1"} and "FAULTED" in txt, "an unimplemented register is reported as faulted, not as a value")
check(rec["verdict"]["PMUv3"].startswith("no (IMPLEMENTATION DEFINED") and rec["verdict"]["RNDR"] == "yes" and rec["verdict"]["FGT"] == "no",
      "verdict: PMUVer 0xf = no PMUv3, RNDR yes (ISAR0.RNDR = 1), FGT no")
check("Windows-relevant verdict" in txt and "secondary cores: NOT read" in txt, "verdict block and the secondary-core note are printed")
check(rec["regs"]["ID_AA64PFR0_EL1"] == "0x1101000010110111" and json.dumps(rec), "raw values kept in the JSON-able record")

p, u, rec, txt = go(dict(BASE, PMCR_EL0=0x41013000), probe=True)
check("PMCR_EL0" in rec["regs"] and "PMCR_EL0" not in rec["faulted"], "PMCR_EL0 readable is recorded")
p, u, rec, txt = go(dict(BASE), probe=False)
check("PMCR_EL0" not in u.reads and "OSLSR_EL1" not in u.reads and "PMCR_EL0" in rec["skipped"], "--no-fault-probe never reads PMCR_EL0 / OSLSR_EL1")
p, u, rec, txt = go(dict(BASE, CurrentEL=1 << 2))
check("HCR_EL2" not in u.reads and "HCR_EL2" in rec["skipped"], "EL1: the EL2 registers are skipped")
p, u, rec, txt = go(dict(BASE, ID_AA64PFR0_EL1=PFR0 | (1 << 32), ID_AA64PFR1_EL1=1 << 24, ID_AA64ZFR0_EL1=1, ID_AA64SMFR0_EL1=1))
check("ID_AA64ZFR0_EL1" in u.reads and "ID_AA64SMFR0_EL1" in u.reads, "ZFR0/SMFR0 read when SVE != 0 / SME != 0")
p, u, rec, txt = go(dict(BASE, ID_AA64DFR0_EL1=6 << 8, ID_AA64ISAR2_EL1=2, ID_AA64MMFR0_EL1=(1 << 56) | (2 << 60), ID_AA64PFR1_EL1=1 | (1 << 8)))
v = rec["verdict"]
check(v["PMUv3"] == "yes" and v["WFxT"] == "yes" and v["FGT"] == "yes" and v["ECV"] == "yes" and v["BTI"] == "yes" and v["MTE"] == "yes" and v["SVE"] == "no",
      "verdict yes/no items follow the fields")

buf = io.StringIO()
with contextlib.redirect_stdout(buf):
    rc = ir.main(["--dry-run"])
check(rc == 0 and "DRY RUN" in buf.getvalue() and "Secondary cores are not read" in buf.getvalue(), "dry-run prints the plan and calls no proxy")
src = open(os.path.join(HERE, "idregs.py")).read()
code = re.sub(r'""".*?"""', "", src, count=1, flags=re.S)
check("import m1n1.setup" not in code and "from m1n1.setup" not in code and not re.search(r"\.(write\w*|set32|clear32|mask32|memcpy|smp_\w+|msr)\(", code),
      "source: no m1n1.setup, no write/msr/smp call")

print("\n%s: %d check(s) failed" % ("FAILED" if FAILED else "ALL OK", len(FAILED)))
sys.exit(1 if FAILED else 0)
