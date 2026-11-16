#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""HACR0: does HACR_EL2[56] stick on the real M1, and (optionally) does it route a PMUv3 access to EL2? Over the m1n1 proxy.

Why: the M1 has no PMUv3 (ID_AA64DFR0_EL1 = 0x10305f09, PMUVer 0xF), MDCR_EL2.TPM / TPMCR are RES0, so Windows' PMU accesses (bootmgr:
PMCCNTR_EL0, PMCR_EL0, ...) are UNDEFINED at EL1 and the layer never sees them. The layer sets bit 56 of the IMPLEMENTATION DEFINED
HACR_EL2 on every core so that they trap to EL2 [hypothesis until this probe / the first layer run says so]; see
docs/design/watari-el2/hacr56-pmu.md. HACR_EL2 is architectural: S3_4_C1_C1_7 [verified: llvm-mc]; the bit meaning is Apple's.

What it does, on the BOOT CORE only (m1n1 runs at EL2 there):
  1. CurrentEL must be 2, else it stops before any write.
  2. reads HACR_EL2, ESR_EL2, AFSR1_EL2 (under the proxy exception guard; a faulting read is reported, nothing is written).
  3. HACR_EL2 |= bit 56, reads it back, reports which bits stuck. This is the DEFAULT and the only mode of the monday.py rung HACR0.
  4. only with --el1: runs `mrs xN, PMCCNTR_EL0` AT EL1 through the proxy's el1_call (u.mrs(enc, call="el1"), as tools/m3a/el1_ctrl.py does)
     under the exception guard, then reads ESR_EL2 / AFSR1_EL2 again and reports: it arrived at EL2 (EC 0x18 or 0x3F, with AFSR1), it ran
     (the value), or it did not arrive (EL1 UNDEF or a proxy error). [hypothesis] that the guard counts and skips an exception taken from
     EL1 exactly as for EL2 code; that is why this step is opt-in: an UNDEF at EL1 that m1n1 does not catch may hang the proxy (power-cycle).
  5. ALWAYS restores HACR_EL2 to the value read in step 2 (also after an error) and reads it back.
What it never does: no memory write, no m1n1.setup import (it writes the PMU), no SMP start, no chainload, no jump. Secondary cores are
not touched (the layer programs them itself). The only register written is HACR_EL2, and it is put back.

Usage: python3 hacr_probe.py [--out FILE] [--el1]      (FILE default hacr-out.txt; raw values go to FILE minus extension + .json)
       python3 hacr_probe.py --dry-run                  (plan only, no proxy at all)
"""
import argparse
import json
import os
import sys

# name -> (op0, op1, CRn, CRm, op2)   [verified: llvm-mc encodes HACR_EL2 as S3_4_C1_C1_7, AFSR1_EL2 as S3_4_C5_C1_1; Arm ARM D24.2]
ENC = {
    "CurrentEL": (3, 0, 4, 2, 2),
    "HACR_EL2": (3, 4, 1, 1, 7),
    "ESR_EL2": (3, 4, 5, 2, 0),
    "AFSR1_EL2": (3, 4, 5, 1, 1),
    "PMCCNTR_EL0": (3, 3, 9, 13, 0),
}
HACR_PMU = 1 << 56
MASK64 = 0xffffffffffffffff


class Probe:
    """The only object that touches the proxy: u.mrs / u.msr plus set_exc_guard / get_exc_count (get_exc_count returns and RESETS)."""

    def __init__(self, p, u):
        self.p, self.u = p, u
        self.p.get_exc_count()                 # drain

    def mrs(self, name, call=None):
        try:
            v = self.u.mrs(ENC[name], call=call) if call else self.u.mrs(ENC[name])
        except Exception as e:                 # host-side exception (proxy error reply)
            return None, "EXCEPTION %s: %s" % (type(e).__name__, e)
        if self.p.get_exc_count() > 0:
            return None, "faulted"
        return v & MASK64, None

    def msr(self, name, val):
        try:
            self.u.msr(ENC[name], val & MASK64)
        except Exception as e:
            return "EXCEPTION %s: %s" % (type(e).__name__, e)
        if self.p.get_exc_count() > 0:
            return "faulted"
        return None


def ec_of(esr):
    return (esr >> 26) & 0x3f


def classify_el1(value, err, count, esr_before, esr_after, afsr1_after):
    """Where did the EL1 `mrs PMCCNTR_EL0` go? count = exceptions the proxy guard counted; ESR/AFSR1 read at EL2 afterwards."""
    if err is not None and count == 0 and not str(err).startswith("faulted"):
        return "no verdict: proxy error (%s)" % err
    if count > 0:
        ec = ec_of(esr_after) if esr_after is not None else None
        if ec == 0x3f:
            return "ARRIVED AT EL2 as EC 0x3F (IMPLEMENTATION DEFINED): ESR_EL2=0x%x AFSR1_EL2=%s; ISS in AFSR1 low 25 bits = %s" % (
                esr_after, "0x%x" % afsr1_after if afsr1_after is not None else "?",
                "0x%x" % (afsr1_after & 0x1ffffff) if afsr1_after is not None else "?")
        if ec == 0x18:
            return "ARRIVED AT EL2 as EC 0x18 (MSR/MRS trap): ESR_EL2=0x%x (ISS in ESR)" % esr_after
        return "an exception was counted but ESR_EL2 does not show EC 0x18/0x3F (EC %s, ESR_EL2=%s, was %s): EL1 UNDEF or unrelated" % (
            "0x%x" % ec if ec is not None else "?", "0x%x" % esr_after if esr_after is not None else "?",
            "0x%x" % esr_before if esr_before is not None else "?")
    if value is not None:
        return "RAN at EL1 and returned 0x%x: the access is NOT trapped and NOT UNDEFINED (a PMU-like register answered)" % value
    return "no verdict"


def run(p, u, out, guard_on, guard_off, el1=False):
    rec = {"regs": {}, "faulted": {}, "mode": "el1" if el1 else "readback-only", "cores_touched": "boot core only"}
    pr = None
    guard_set = False
    old = None
    wrote = False
    try:
        p.set_exc_guard(guard_on)               # BEFORE the first read
        guard_set = True
        pr = Probe(p, u)
        out("== HACR_EL2[56] probe (mode: %s), boot core only" % rec["mode"])
        el_raw, err = pr.mrs("CurrentEL")
        if err or el_raw is None:
            out("CurrentEL: %s -> stop, nothing written" % err)
            rec["stopped"] = "CurrentEL " + str(err)
            return rec
        el = (el_raw >> 2) & 3
        rec["regs"]["CurrentEL"] = el
        out("CurrentEL = EL%d" % el)
        if el != 2:
            out("not at EL2 -> stop, nothing written")
            rec["stopped"] = "not EL2"
            return rec
        vals = {}
        for n in ("HACR_EL2", "ESR_EL2", "AFSR1_EL2"):
            v, err = pr.mrs(n)
            if v is None:
                rec["faulted"][n] = err
                out("%-10s FAULTED (%s)" % (n, err))
            else:
                vals[n] = v
                rec["regs"][n + "_before"] = "0x%x" % v
                out("%-10s = 0x%016x" % (n, v))
        if "HACR_EL2" not in vals:
            out("HACR_EL2 cannot be read here -> stop, nothing written")
            rec["stopped"] = "HACR_EL2 unreadable"
            return rec
        old = vals["HACR_EL2"]
        wrote = True                            # before the write: a half-done write is restored too
        werr = pr.msr("HACR_EL2", old | HACR_PMU)
        rb, rerr = pr.mrs("HACR_EL2")
        out("write HACR_EL2 = 0x%016x: %s" % (old | HACR_PMU, werr or "no fault"))
        if rb is None:
            out("read back FAULTED (%s)" % rerr)
            rec["faulted"]["HACR_EL2_readback"] = rerr
        else:
            changed = rb ^ old
            out("read back          = 0x%016x" % rb)
            out("bits that changed vs before = 0x%016x" % changed)
            rec["regs"]["HACR_EL2_after"] = "0x%x" % rb
            rec["bit56_sticks"] = bool(rb & HACR_PMU)
            out("HACR_EL2[56] %s" % ("STICKS (reads back 1)" if rb & HACR_PMU else "DOES NOT STICK (reads back 0): the layer's routing cannot work"))
            if rb & ~(old | HACR_PMU) & MASK64:
                out("note: bits that read 1 although nobody set them: 0x%x" % (rb & ~(old | HACR_PMU) & MASK64))
        if el1 and rec.get("bit56_sticks"):
            esr0 = vals.get("ESR_EL2")
            v, err = pr.mrs("PMCCNTR_EL0", call="el1")
            count_err = err
            # pr.mrs already consumed the exception count: faulted == counted
            counted = 1 if (err == "faulted") else 0
            esr1, _ = pr.mrs("ESR_EL2")
            af1, _ = pr.mrs("AFSR1_EL2")
            rec["regs"]["ESR_EL2_after_el1"] = None if esr1 is None else "0x%x" % esr1
            rec["regs"]["AFSR1_EL2_after_el1"] = None if af1 is None else "0x%x" % af1
            rec["el1_verdict"] = classify_el1(v, count_err, counted, esr0, esr1, af1)
            out("EL1 mrs PMCCNTR_EL0 (el1_call, guard on): value=%s err=%s" % ("0x%x" % v if v is not None else None, err))
            out("ESR_EL2 after = %s, AFSR1_EL2 after = %s" % (rec["regs"]["ESR_EL2_after_el1"], rec["regs"]["AFSR1_EL2_after_el1"]))
            out("EL1 verdict: " + rec["el1_verdict"])
        elif el1:
            out("EL1 attempt skipped: bit 56 does not stick, nothing could route")
        else:
            out("EL1 attempt not requested (--el1): readback only")
        return rec
    finally:
        if wrote and old is not None and pr is not None:
            try:
                e = pr.msr("HACR_EL2", old)
                back, _ = pr.mrs("HACR_EL2")
                rec["restored"] = back == old
                out("restore HACR_EL2 = 0x%016x: read back %s (%s)" % (old, "0x%016x" % back if back is not None else "?",
                                                                      "OK" if back == old else "MISMATCH" + (" " + e if e else "")))
            except BaseException as ex:         # also a KeyboardInterrupt: the old value must go back
                out("restore HACR_EL2 FAILED: %s: %s" % (type(ex).__name__, ex))
        if guard_set:
            p.set_exc_guard(guard_off)
        out("proxy calls used: set_exc_guard, u.mrs, u.msr (HACR_EL2 only, restored), get_exc_count")


def dry_run(out, el1=False):
    out("hacr_probe DRY RUN: no proxy call is made")
    out("would, on the boot core (stop before any write unless CurrentEL == 2): read HACR_EL2 (S3_4_C1_C1_7), ESR_EL2, AFSR1_EL2")
    out("then HACR_EL2 |= 1<<56, read it back, report which bits stuck, ALWAYS write the old value back")
    out("then %s" % ("an EL1 `mrs PMCCNTR_EL0` through el1_call under the exception guard, ESR_EL2 / AFSR1_EL2 afterwards" if el1 else
                     "nothing more (--el1 not given: readback only)"))
    out("never: memory writes, m1n1.setup, SMP start, chainload, jump. Secondary cores are not touched.")


def main(argv=None):
    ap = argparse.ArgumentParser(description="HACR0: HACR_EL2[56] readback (and optional EL1 PMU access) over the m1n1 proxy")
    ap.add_argument("--dry-run", action="store_true", help="print the plan, make no proxy call")
    ap.add_argument("--out", default="hacr-out.txt", help="report file; raw values go to the same name with .json")
    ap.add_argument("--el1", action="store_true", help="also run an EL1 mrs PMCCNTR_EL0 via el1_call (may hang the proxy if EL1 UNDEF is not caught)")
    a = ap.parse_args(argv)
    f = open(a.out, "w") if not a.dry_run else None

    def out(s):
        print(s, flush=True)
        if f:
            print(s, file=f, flush=True)
    if a.dry_run:
        dry_run(out, a.el1)
        return 0
    from m1n1.proxy import UartInterface, M1N1Proxy, GUARD             # never m1n1.setup
    from m1n1.proxyutils import ProxyUtils, bootstrap_port
    iface = UartInterface()
    p = M1N1Proxy(iface, debug=False)
    bootstrap_port(iface, p)
    u = ProxyUtils(p)
    rec = run(p, u, out, GUARD.SKIP | GUARD.SILENT, GUARD.OFF, a.el1)
    jpath = os.path.splitext(a.out)[0] + ".json"
    with open(jpath, "w") as jf:
        json.dump(rec, jf, indent=1, sort_keys=True)
    out("raw values: %s" % jpath)
    return 0


if __name__ == "__main__":
    sys.exit(main())
