#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""ID0: READ-ONLY dump and decode of the M1's feature/ID registers over the m1n1 proxy (rung ID0 of docs/procedures/m3a-monday-runbook.md).

Why: Windows bootmgr took an EL1 UNDEFINED exception on the real M1. The layer must know which features the silicon really has
(PMUv3? RNDR? FGT? ECV?) to decide how to handle the registers bootmgr touches.

What it does: `mrs` of ID_AA64PFR0/1/2, DFR0/1, ISAR0/1/2, MMFR0/1/2/3, ZFR0 (only if PFR0.SVE != 0), SMFR0 (only if PFR1.SME != 0),
MIDR, REVIDR, AIDR on the BOOT CORE, as m1n1 left it. The EL2 registers (HCR_EL2, MDCR_EL2, CNTHCTL_EL2) only if CurrentEL == 2.
PMCR_EL0 and OSLSR_EL1 are probed under the proxy exception guard (GUARD.SKIP|SILENT set before, get_exc_count() after each read:
a count > 0 means the read faulted, exactly as ehw2.py does for memory reads); a faulting register is reported as "absent (faulted)".
What it never does: no msr, no memory write, no import of m1n1.setup (it writes the PMU), no SMP start (ehw0c.py starts the secondary
cores: that is not read-only), no chainload, no jump. SECONDARY CORES ARE NOT READ: boot core only. (On this SoC the ID registers of the
P and E cores differ only in MIDR/REVIDR and in a few ISAR/MMFR fields; the layer will see them per core later [hypothesis].)
[hypothesis] u.mrs() of an unimplemented register under GUARD.SKIP comes back with the fault counted and the instruction skipped; this
is the m1n1 exc_guard behaviour (exception.c) and is the part never run on hardware. --no-fault-probe skips PMCR_EL0/OSLSR_EL1 entirely.

Usage: python3 idregs.py [--out FILE] [--no-fault-probe]   (FILE default idregs-out.txt; the raw values go to FILE minus extension + .json)
       python3 idregs.py --dry-run                         (plan only, no proxy at all)
Field positions: Arm ARM DDI0487 (D19 "AArch64 System Register Descriptions"); each table below says [verified: Arm ARM ...] where the
position was checked against the register description, [hypothesis] where it is from memory of a newer revision.
"""
import argparse
import json
import os
import sys

# name -> (op0, op1, CRn, CRm, op2)   [verified: Arm ARM DDI0487 system register encodings]
ENC = {
    "CurrentEL": (3, 0, 4, 2, 2),
    "MIDR_EL1": (3, 0, 0, 0, 0), "REVIDR_EL1": (3, 0, 0, 0, 6), "AIDR_EL1": (3, 1, 0, 0, 7),
    "ID_AA64PFR0_EL1": (3, 0, 0, 4, 0), "ID_AA64PFR1_EL1": (3, 0, 0, 4, 1), "ID_AA64PFR2_EL1": (3, 0, 0, 4, 2),
    "ID_AA64ZFR0_EL1": (3, 0, 0, 4, 4), "ID_AA64SMFR0_EL1": (3, 0, 0, 4, 5),
    "ID_AA64DFR0_EL1": (3, 0, 0, 5, 0), "ID_AA64DFR1_EL1": (3, 0, 0, 5, 1),
    "ID_AA64ISAR0_EL1": (3, 0, 0, 6, 0), "ID_AA64ISAR1_EL1": (3, 0, 0, 6, 1), "ID_AA64ISAR2_EL1": (3, 0, 0, 6, 2),
    "ID_AA64MMFR0_EL1": (3, 0, 0, 7, 0), "ID_AA64MMFR1_EL1": (3, 0, 0, 7, 1), "ID_AA64MMFR2_EL1": (3, 0, 0, 7, 2),
    "ID_AA64MMFR3_EL1": (3, 0, 0, 7, 3),
    "HCR_EL2": (3, 4, 1, 1, 0), "MDCR_EL2": (3, 4, 1, 1, 1), "CNTHCTL_EL2": (3, 4, 14, 1, 0),
    "PMCR_EL0": (3, 3, 9, 12, 0), "OSLSR_EL1": (2, 0, 1, 1, 4),
}

# (always-read ID registers), in print order
ID_REGS = ["MIDR_EL1", "REVIDR_EL1", "AIDR_EL1", "ID_AA64PFR0_EL1", "ID_AA64PFR1_EL1", "ID_AA64PFR2_EL1",
           "ID_AA64DFR0_EL1", "ID_AA64DFR1_EL1", "ID_AA64ISAR0_EL1", "ID_AA64ISAR1_EL1", "ID_AA64ISAR2_EL1",
           "ID_AA64MMFR0_EL1", "ID_AA64MMFR1_EL1", "ID_AA64MMFR2_EL1", "ID_AA64MMFR3_EL1"]
EL2_REGS = ["HCR_EL2", "MDCR_EL2", "CNTHCTL_EL2"]
PROBE_REGS = ["PMCR_EL0", "OSLSR_EL1"]

# register -> [(field, hi, lo)]. Positions: Arm ARM DDI0487 register descriptions.
FIELDS = {
    # [verified: Arm ARM D19.2 MIDR_EL1]
    "MIDR_EL1": [("Implementer", 31, 24), ("Variant", 23, 20), ("Architecture", 19, 16), ("PartNum", 15, 4), ("Revision", 3, 0)],
    # [verified: Arm ARM ID_AA64PFR0_EL1]
    "ID_AA64PFR0_EL1": [("EL0", 3, 0), ("EL1", 7, 4), ("EL2", 11, 8), ("EL3", 15, 12), ("FP", 19, 16), ("AdvSIMD", 23, 20),
                        ("GIC", 27, 24), ("RAS", 31, 28), ("SVE", 35, 32), ("SEL2", 39, 36), ("MPAM", 43, 40), ("AMU", 47, 44),
                        ("DIT", 51, 48), ("RME", 55, 52), ("CSV2", 59, 56), ("CSV3", 63, 60)],
    # [verified: Arm ARM ID_AA64PFR1_EL1 up to NMI; later fields GCS..PFAR from a newer revision: hypothesis]
    "ID_AA64PFR1_EL1": [("BT", 3, 0), ("SSBS", 7, 4), ("MTE", 11, 8), ("RAS_frac", 15, 12), ("MPAM_frac", 19, 16), ("SME", 27, 24),
                        ("RNDR_trap", 31, 28), ("CSV2_frac", 35, 32), ("NMI", 39, 36), ("MTE_frac", 43, 40), ("GCS", 47, 44),
                        ("THE", 51, 48), ("MTEX", 55, 52), ("DF2", 59, 56), ("PFAR", 63, 60)],
    # [hypothesis] ID_AA64PFR2_EL1 is from a recent revision (2024+)
    "ID_AA64PFR2_EL1": [("MTEPERM", 3, 0), ("MTESTOREONLY", 7, 4), ("MTEFAR", 11, 8), ("FPMR", 35, 32)],
    # [verified: Arm ARM ID_AA64DFR0_EL1 up to MTPMU; BRBE, ExtTrcBuff, HPMN0: hypothesis]
    "ID_AA64DFR0_EL1": [("DebugVer", 3, 0), ("TraceVer", 7, 4), ("PMUVer", 11, 8), ("BRPs", 15, 12), ("PMSS", 19, 16), ("WRPs", 23, 20),
                        ("SEBEP", 27, 24), ("CTX_CMPs", 31, 28), ("PMSVer", 35, 32), ("DoubleLock", 39, 36), ("TraceFilt", 43, 40),
                        ("TraceBuffer", 47, 44), ("MTPMU", 51, 48), ("BRBE", 55, 52), ("ExtTrcBuff", 59, 56), ("HPMN0", 63, 60)],
    # [hypothesis]
    "ID_AA64DFR1_EL1": [("DPFZS", 3, 0), ("PMICNTR", 11, 8), ("SPMU", 19, 16)],
    # [verified: Arm ARM ID_AA64ISAR0_EL1]
    "ID_AA64ISAR0_EL1": [("AES", 7, 4), ("SHA1", 11, 8), ("SHA2", 15, 12), ("CRC32", 19, 16), ("Atomic", 23, 20), ("TME", 27, 24),
                         ("RDM", 31, 28), ("SHA3", 35, 32), ("SM3", 39, 36), ("SM4", 43, 40), ("DP", 47, 44), ("FHM", 51, 48),
                         ("TS", 55, 52), ("TLB", 59, 56), ("RNDR", 63, 60)],
    # [verified: Arm ARM ID_AA64ISAR1_EL1]
    "ID_AA64ISAR1_EL1": [("DPB", 3, 0), ("APA", 7, 4), ("API", 11, 8), ("JSCVT", 15, 12), ("FCMA", 19, 16), ("LRCPC", 23, 20),
                         ("GPA", 27, 24), ("GPI", 31, 28), ("FRINTTS", 35, 32), ("SB", 39, 36), ("SPECRES", 43, 40), ("BF16", 47, 44),
                         ("DGH", 51, 48), ("I8MM", 55, 52), ("XS", 59, 56), ("LS64", 63, 60)],
    # [verified: Arm ARM ID_AA64ISAR2_EL1 WFxT..CLRBHB; the rest: hypothesis]
    "ID_AA64ISAR2_EL1": [("WFxT", 3, 0), ("RPRES", 7, 4), ("GPA3", 11, 8), ("APA3", 15, 12), ("MOPS", 19, 16), ("BC", 23, 20),
                         ("PAC_frac", 27, 24), ("CLRBHB", 31, 28), ("PRFMSLC", 43, 40), ("RPRFM", 51, 48), ("CSSC", 55, 52),
                         ("LUT", 59, 56), ("ATS1A", 63, 60)],
    # [verified: Arm ARM ID_AA64MMFR0_EL1]
    "ID_AA64MMFR0_EL1": [("PARange", 3, 0), ("ASIDBits", 7, 4), ("BigEnd", 11, 8), ("SNSMem", 15, 12), ("BigEndEL0", 19, 16),
                         ("TGran16", 23, 20), ("TGran64", 27, 24), ("TGran4", 31, 28), ("TGran16_2", 35, 32), ("TGran64_2", 39, 36),
                         ("TGran4_2", 43, 40), ("ExS", 47, 44), ("FGT", 59, 56), ("ECV", 63, 60)],
    # [verified: Arm ARM ID_AA64MMFR1_EL1]
    "ID_AA64MMFR1_EL1": [("HAFDBS", 3, 0), ("VMIDBits", 7, 4), ("VH", 11, 8), ("HPDS", 15, 12), ("LO", 19, 16), ("PAN", 23, 20),
                         ("SpecSEI", 27, 24), ("XNX", 31, 28), ("TWED", 35, 32), ("ETS", 39, 36), ("HCX", 43, 40), ("AFP", 47, 44),
                         ("nTLBPA", 51, 48), ("TIDCP1", 55, 52), ("CMOW", 59, 56), ("ECBHB", 63, 60)],
    # [verified: Arm ARM ID_AA64MMFR2_EL1]
    "ID_AA64MMFR2_EL1": [("CnP", 3, 0), ("UAO", 7, 4), ("LSM", 11, 8), ("IESB", 15, 12), ("VARange", 19, 16), ("CCIDX", 23, 20),
                         ("NV", 27, 24), ("ST", 31, 28), ("AT", 35, 32), ("IDS", 39, 36), ("FWB", 43, 40), ("TTL", 51, 48),
                         ("BBM", 55, 52), ("EVT", 59, 56), ("E0PD", 63, 60)],
    # [hypothesis] ID_AA64MMFR3_EL1 is from a recent revision
    "ID_AA64MMFR3_EL1": [("TCRX", 3, 0), ("SCTLRX", 7, 4), ("S1PIE", 11, 8), ("S2PIE", 15, 12), ("S1POE", 19, 16), ("S2POE", 23, 20),
                         ("AIE", 27, 24), ("MEC", 31, 28), ("D128", 35, 32), ("SNERR", 43, 40), ("ANERR", 47, 44)],
    # [hypothesis]
    "ID_AA64ZFR0_EL1": [("SVEver", 3, 0), ("AES", 7, 4), ("BitPerm", 19, 16), ("BF16", 23, 20), ("SHA3", 35, 32), ("SM4", 43, 40),
                        ("I8MM", 47, 44), ("F32MM", 55, 52), ("F64MM", 59, 56)],
    # [hypothesis]
    "ID_AA64SMFR0_EL1": [("SMEver", 59, 56), ("F64F64", 48, 48), ("I16I64", 55, 52)],
    # [verified: Arm ARM HCR_EL2 (bit 34 = E2H, 27 = TGE, 31 = RW)]
    "HCR_EL2": [("VM", 0, 0), ("TWI", 13, 13), ("TWE", 14, 14), ("TID3", 18, 18), ("TSC", 19, 19), ("TGE", 27, 27), ("RW", 31, 31),
                ("E2H", 34, 34)],
    # [verified: Arm ARM MDCR_EL2]
    "MDCR_EL2": [("HPMN", 4, 0), ("TPMCR", 5, 5), ("TPM", 6, 6), ("HPME", 7, 7), ("TDE", 8, 8), ("TDA", 9, 9), ("TDOSA", 10, 10),
                 ("TDRA", 11, 11), ("E2PB", 13, 12), ("TPMS", 14, 14), ("HPMD", 17, 17), ("TTRF", 19, 19)],
    # [verified: Arm ARM CNTHCTL_EL2 with HCR_EL2.E2H == 0 for [9:0]; with E2H == 1 the layout of [11:10] differs]
    "CNTHCTL_EL2": [("EL1PCTEN/EL0PCTEN", 0, 0), ("EL1PCEN/EL0VCTEN", 1, 1), ("EVNTEN", 2, 2), ("EVNTDIR", 3, 3), ("EVNTI", 7, 4),
                    ("EL0VTEN", 8, 8), ("EL0PTEN", 9, 9), ("ECV", 12, 12), ("EL1TVT", 13, 13), ("EL1TVCT", 14, 14)],
    # [verified: Arm ARM PMCR_EL0]
    "PMCR_EL0": [("E", 0, 0), ("P", 1, 1), ("C", 2, 2), ("D", 3, 3), ("X", 4, 4), ("DP", 5, 5), ("LC", 6, 6), ("LP", 7, 7),
                 ("N", 15, 11), ("IDCODE", 23, 16), ("IMP", 31, 24)],
    # [verified: Arm ARM OSLSR_EL1: OSLM = bits [3,0], nTT = bit 2, OSLK = bit 1]
    "OSLSR_EL1": [("OSLM_lo", 0, 0), ("OSLK", 1, 1), ("nTT", 2, 2), ("OSLM_hi", 3, 3)],
}


def bits(v, hi, lo):
    return (v >> lo) & ((1 << (hi - lo + 1)) - 1)


def decode(reg, value):
    """Pure: [(field, value)] for a register value, in table order (empty list for registers printed raw)."""
    return [(n, bits(value, hi, lo)) for n, hi, lo in FIELDS.get(reg, [])]


def fields(reg, value):
    return dict(decode(reg, value))


def verdict(vals, el=None, probed=None):
    """Pure: the "Windows-relevant verdict" lines. vals = {reg name: int or None}; probed = {"PMCR_EL0": int|None, ...}.
    Returns [(item, answer, why)]. Answers are 'yes' / 'no' / 'unknown' / text."""
    probed = probed or {}

    def f(reg, name):
        v = vals.get(reg)
        return None if v is None else fields(reg, v).get(name)
    out = []
    pmu = f("ID_AA64DFR0_EL1", "PMUVer")
    if pmu is None:
        ans, why = "unknown", "DFR0 not read"
    elif pmu == 0xf:
        ans, why = "no (IMPLEMENTATION DEFINED PMU only)", "DFR0.PMUVer = 0xf: not architected PMUv3, PMCR_EL0 etc. may UNDEF"
    elif pmu == 0:
        ans, why = "no", "DFR0.PMUVer = 0"
    else:
        ans, why = "yes", "DFR0.PMUVer = %d (PMUv3)" % pmu
    if "PMCR_EL0" in probed:
        why += "; PMCR_EL0 read %s" % ("faulted (UNDEFINED at this EL)" if probed["PMCR_EL0"] is None else "ok = 0x%x" % probed["PMCR_EL0"])
    out.append(("PMUv3", ans, why))

    def present(item, reg, name, why_yes, minimum=1):
        v = f(reg, name)
        if v is None:
            out.append((item, "unknown", "%s not read" % reg))
        else:
            out.append((item, "yes" if v >= minimum else "no", "%s.%s = %d%s" % (reg.replace("ID_AA64", "").replace("_EL1", ""), name, v, ("  " + why_yes) if v >= minimum else "")))
    present("RNDR", "ID_AA64ISAR0_EL1", "RNDR", "(RNDR/RNDRRS readable)")
    present("FGT", "ID_AA64MMFR0_EL1", "FGT", "(HFGRTR/HFGWTR/HDFGRTR... let the layer trap single registers precisely)")
    present("ECV", "ID_AA64MMFR0_EL1", "ECV", "(enhanced counter virtualization, CNTPOFF_EL2)")
    present("WFxT", "ID_AA64ISAR2_EL1", "WFxT", "(WFET/WFIT)", minimum=2)
    present("BTI", "ID_AA64PFR1_EL1", "BT", "(PSTATE.BTYPE, GP pages)")
    present("MTE", "ID_AA64PFR1_EL1", "MTE", "(tags)")
    present("SVE", "ID_AA64PFR0_EL1", "SVE", "(vector registers; CPTR_EL2.TZ matters)")
    present("SME", "ID_AA64PFR1_EL1", "SME", "")
    present("VH (E2H)", "ID_AA64MMFR1_EL1", "VH", "")
    return out


def lines_for(reg, value):
    s = ["%-18s = 0x%016x" % (reg, value)]
    d = decode(reg, value)
    if d:
        row = ""
        for n, v in d:
            item = "%s=%x" % (n, v)
            if len(row) + len(item) + 2 > 100:
                s.append("    " + row.rstrip())
                row = ""
            row += item + "  "
        if row:
            s.append("    " + row.rstrip())
    return s


class Reader:
    """The only object that touches the proxy: u.mrs(enc) plus set_exc_guard / get_exc_count. A read whose get_exc_count() > 0 faulted
    (None). get_exc_count() returns the count and RESETS it (proxy.c), as in ehw2.py."""

    def __init__(self, p, u, enc_lookup=None):
        self.p, self.u = p, u
        self.enc_lookup = enc_lookup or (lambda n: ENC[n])
        self.faults = 0
        self.p.get_exc_count()                 # drain

    def mrs(self, name):
        try:
            v = self.u.mrs(self.enc_lookup(name))
        except Exception as e:                 # host-side exception (proxy error reply)
            self.faults += 1
            return None, "EXCEPTION %s: %s" % (type(e).__name__, e)
        if self.p.get_exc_count() > 0:
            self.faults += 1
            return None, "faulted"
        return v & 0xffffffffffffffff, None


def run(p, u, out, guard_on, guard_off, probe=True, enc_lookup=None):
    """Read everything. Returns the JSON-able record. Only set_exc_guard, get_exc_count and u.mrs are used."""
    rec = {"regs": {}, "skipped": {}, "faulted": {}, "cores_read": "boot core only"}
    rd = None
    guard_set = False
    try:
        p.set_exc_guard(guard_on)               # BEFORE the first read
        guard_set = True
        rd = Reader(p, u, enc_lookup)
        out("== idregs: read-only, boot core only, as m1n1 handed it over; guard SKIP|SILENT set before the first read ==")
        el_raw, err = rd.mrs("CurrentEL")
        el = None if el_raw is None else (el_raw >> 2) & 3
        out("CurrentEL = %s" % ("EL%d" % el if el is not None else "unreadable (%s)" % err))
        rec["CurrentEL"] = el
        vals = {}

        def take(name, why=None):
            v, err = rd.mrs(name)
            if v is None:
                rec["faulted"][name] = err
                out("%-18s = FAULTED (%s)" % (name, err))
            else:
                vals[name] = v
                rec["regs"][name] = "0x%016x" % v
                for ln in lines_for(name, v):
                    out(ln)
            return v
        for n in ID_REGS:
            take(n)
        pf0, pf1 = vals.get("ID_AA64PFR0_EL1"), vals.get("ID_AA64PFR1_EL1")
        if pf0 is not None and bits(pf0, 35, 32) != 0:
            take("ID_AA64ZFR0_EL1")
        else:
            rec["skipped"]["ID_AA64ZFR0_EL1"] = "PFR0.SVE == 0 (or PFR0 unreadable)"
            out("ID_AA64ZFR0_EL1: skipped (PFR0.SVE == 0)")
        if pf1 is not None and bits(pf1, 27, 24) != 0:
            take("ID_AA64SMFR0_EL1")
        else:
            rec["skipped"]["ID_AA64SMFR0_EL1"] = "PFR1.SME == 0 (or PFR1 unreadable)"
            out("ID_AA64SMFR0_EL1: skipped (PFR1.SME == 0)")
        if el == 2:
            for n in EL2_REGS:
                take(n)
        else:
            for n in EL2_REGS:
                rec["skipped"][n] = "CurrentEL is not EL2"
            out("HCR_EL2 / MDCR_EL2 / CNTHCTL_EL2: skipped (CurrentEL is not EL2)")
        probed = {}
        if probe:
            out("-- fault-probed registers (an UNDEFINED read is counted by the proxy guard and skipped) --")
            for n in PROBE_REGS:
                probed[n] = take(n)
        else:
            for n in PROBE_REGS:
                rec["skipped"][n] = "--no-fault-probe"
            out("PMCR_EL0 / OSLSR_EL1: skipped (--no-fault-probe)")
        out("")
        out("== Windows-relevant verdict (from the registers above; decisions for the layer, not for Windows) ==")
        rec["verdict"] = {}
        for item, ans, why in verdict(vals, el, probed):
            out("  %-10s %-12s %s" % (item, ans, why))
            rec["verdict"][item] = ans
        out("")
        out("secondary cores: NOT read (reading them needs smp_start_secondaries, which is not read-only; ehw0c.py does it when wanted)")
    finally:
        if guard_set:
            p.set_exc_guard(guard_off)
    out("faulted reads: %d" % (rd.faults if rd else 0))
    out("proxy calls used: set_exc_guard(SKIP|SILENT) once, u.mrs each followed by get_exc_count, set_exc_guard(OFF) at exit")
    return rec


def dry_run(out, probe=True):
    out("idregs DRY RUN: no proxy call is made")
    out("would read on the boot core: CurrentEL, " + ", ".join(ID_REGS))
    out("then ID_AA64ZFR0_EL1 only if PFR0.SVE != 0, ID_AA64SMFR0_EL1 only if PFR1.SME != 0")
    out("then %s only if CurrentEL == 2" % ", ".join(EL2_REGS))
    out("then %s under the exception guard%s" % (", ".join(PROBE_REGS), "" if probe else " (SKIPPED by --no-fault-probe)"))
    out("never: msr, memory writes, m1n1.setup, SMP start, chainload. Secondary cores are not read.")


def main(argv=None):
    ap = argparse.ArgumentParser(description="ID0 read-only ID register dump over the m1n1 proxy")
    ap.add_argument("--dry-run", action="store_true", help="print the plan, make no proxy call")
    ap.add_argument("--out", default="idregs-out.txt", help="report file; raw values go to the same name with .json")
    ap.add_argument("--no-fault-probe", action="store_true", help="do not read PMCR_EL0 / OSLSR_EL1")
    a = ap.parse_args(argv)
    f = open(a.out, "w") if not a.dry_run else None

    def out(s):
        print(s, flush=True)
        if f:
            print(s, file=f, flush=True)
    if a.dry_run:
        dry_run(out, not a.no_fault_probe)
        return 0
    from m1n1.proxy import UartInterface, M1N1Proxy, GUARD             # never m1n1.setup
    from m1n1.proxyutils import ProxyUtils, bootstrap_port
    import m1n1.sysreg as sysreg
    iface = UartInterface()
    p = M1N1Proxy(iface, debug=False)
    bootstrap_port(iface, p)
    u = ProxyUtils(p)
    rec = run(p, u, out, GUARD.SKIP | GUARD.SILENT, GUARD.OFF, not a.no_fault_probe,
              enc_lookup=lambda n: getattr(sysreg, n, ENC[n]))
    jpath = os.path.splitext(a.out)[0] + ".json"
    with open(jpath, "w") as jf:
        json.dump(rec, jf, indent=1, sort_keys=True)
    out("raw values: %s" % jpath)
    return 0


if __name__ == "__main__":
    sys.exit(main())
