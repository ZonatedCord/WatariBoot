# SPDX-License-Identifier: MIT
# E-HW0 day 1, READ-ONLY measurements (docs/procedures/m3a-day1-hardware.md section 5).
# Does NOT import m1n1.setup: setup.py calls PMU(u).reset_panic_counter(), an SPMI/I2C WRITE.
# No p.write*, no u.msr, no AIC EVENT (0x2004) read, no SMP start, no chainload.
import sys, traceback
from m1n1.proxy import UartInterface, M1N1Proxy
from m1n1.proxyutils import ProxyUtils, bootstrap_port
from m1n1.sysreg import *

OUT = sys.argv[1] if len(sys.argv) > 1 else "ehw0-out.txt"
ADTOUT = sys.argv[2] if len(sys.argv) > 2 else "adt-j293.txt"
log = open(OUT, "w")

def say(*a):
    s = " ".join(str(x) for x in a)
    print(s, flush=True)
    log.write(s + "\n"); log.flush()

def bits(v, hi, lo):
    return (v >> lo) & ((1 << (hi - lo + 1)) - 1)

def mrs(name, enc):
    try:
        v = u.mrs(enc)
        say(f"{name} = 0x{v:016x}")
        return v
    except Exception as e:
        say(f"{name} = EXCEPTION {type(e).__name__}: {e}")
        return None

iface = UartInterface()
p = M1N1Proxy(iface, debug=False)
bootstrap_port(iface, p)
u = ProxyUtils(p)
say(f"m1n1 base: 0x{u.base:x}")

say("== sysregs (boot core, as handed over by m1n1) ==")
say(f"CurrentEL = {(u.mrs(CurrentEL) >> 2) & 3}")
mpidr = mrs("MPIDR_EL1 (row 2)", MPIDR_EL1)
midr = mrs("MIDR_EL1 (row 9)", MIDR_EL1)
if midr is not None:
    say(f"  implementer 0x{bits(midr,31,24):x} variant {bits(midr,23,20)} partnum 0x{bits(midr,15,4):x} rev {bits(midr,3,0)}")
vtr = mrs("ICH_VTR_EL2 (row 3)", ICH_VTR_EL2)
if vtr is not None:
    say(f"  ListRegs={bits(vtr,4,0)+1} TDS={bits(vtr,19,19)} SEIS={bits(vtr,22,22)} PREbits={bits(vtr,28,26)+1} PRIbits={bits(vtr,31,29)+1}")
hcr = mrs("HCR_EL2 (row 4)", HCR_EL2)
if hcr is not None:
    say(f"  E2H={bits(hcr,34,34)} TGE={bits(hcr,27,27)}")
m0 = mrs("ID_AA64MMFR0_EL1 (row 5)", ID_AA64MMFR0_EL1)
if m0 is not None:
    say(f"  PARange={bits(m0,3,0)} TGran16={bits(m0,23,20)} TGran64={bits(m0,27,24)} TGran4={bits(m0,31,28)} "
        f"TGran16_2={bits(m0,35,32)} TGran64_2={bits(m0,39,36)} TGran4_2={bits(m0,43,40)}")
m1 = mrs("ID_AA64MMFR1_EL1 (row 5)", ID_AA64MMFR1_EL1)
if m1 is not None:
    say(f"  VH={bits(m1,11,8)}")
mrs("ID_AA64PFR0_EL1 (row 5)", ID_AA64PFR0_EL1)
mrs("CPTR_EL2 (row 6)", CPTR_EL2)
mrs("CNTHCTL_EL2 (row 6)", CNTHCTL_EL2)
f = mrs("CNTFRQ_EL0 (row 8)", CNTFRQ_EL0)
if f is not None:
    say(f"  = {f} Hz")

say("== ADT ==")
adt = u.adt
try:
    with open(ADTOUT, "w") as fa:
        fa.write(str(adt["/"]) if "/" in adt else str(adt._adt))
    say(f"full ADT dump written to {ADTOUT} (stays in the host folder, not committed)")
except Exception:
    say("ADT dump failed:\n" + traceback.format_exc())

def props(node, names):
    out = {}
    for n in names:
        if n in node._properties:
            out[n] = node._properties[n]
    return out

aic_base = None
nodes = list(adt.walk_tree())
for n in nodes:
    try:
        path = n._path
    except Exception:
        path = getattr(n, "name", "?")
    name = getattr(n, "name", "")
    compat = n._properties.get("compatible", None)
    lname = name.lower()
    if lname == "aic" or (compat and any("aic" in str(c).lower() for c in (compat if isinstance(compat, list) else [compat]))):
        say(f"[AIC row 1] {path} compatible={compat}")
        for i in range(len(getattr(n, "reg", []) or [])):
            try:
                a, s = n.get_reg(i)
                say(f"  reg[{i}] = 0x{a:x} size 0x{s:x}")
                if i == 0 and aic_base is None:
                    aic_base = a
            except Exception as e:
                say(f"  reg[{i}] translate failed: {e}")
    if path.startswith("/cpus/") or "/cpus/" in path:
        say(f"[CPU row 2/9] {path} {props(n, ['reg', 'cpu-id', 'cluster-id', 'cluster-type', 'compatible'])}")
    if any(lname.startswith(k) for k in ("ans", "nvme")):
        say(f"[ANS row 10] {path} {props(n, ['interrupts', 'reg', 'compatible'])}")
    if any(k in lname for k in ("usb-drd", "dwc3", "atc-phy")):
        say(f"[USB row 11] {path} {props(n, ['interrupts', 'compatible'])}")
    if lname.startswith("apcie") or lname.startswith("pcie"):
        msi = {k: v for k, v in n._properties.items() if "msi" in k.lower()}
        say(f"[PCIe row 12] {path} {props(n, ['interrupts', 'compatible'])} msi={msi}")
        for i in range(len(getattr(n, "reg", []) or [])):
            try:
                a, s = n.get_reg(i); say(f"  reg[{i}] = 0x{a:x} size 0x{s:x}")
            except Exception:
                pass
    if lname.startswith("uart"):
        say(f"[UART row 13] {path} {props(n, ['interrupts', 'compatible', 'clock-gates'])}")
        for i in range(len(getattr(n, "reg", []) or [])):
            try:
                a, s = n.get_reg(i); say(f"  reg[{i}] = 0x{a:x} size 0x{s:x}")
            except Exception:
                pass

say("== row 7: ADT ranges overlapping [0xFFFFC000, 0x100000000) (addresses NOT read) ==")
LO, HI = 0xFFFFC000, 0x100000000
hits = 0
for n in nodes:
    for i in range(len(getattr(n, "reg", []) or []) if isinstance(getattr(n, "reg", None), list) else []):
        try:
            a, s = n.get_reg(i)
        except Exception:
            continue
        if a < HI and a + s > LO:
            hits += 1
            say(f"  {n._path} reg[{i}] 0x{a:x}+0x{s:x}")
say(f"  overlaps found: {hits}")
try:
    mm = adt["/chosen/memory-map"]
    say("  /chosen/memory-map: " + ", ".join(f"{k}={v}" for k, v in mm._properties.items()))
except Exception as e:
    say(f"  /chosen/memory-map not readable: {e}")

say("== AIC reads (rows 1, 2, 14); never offset 0x2004 ==")
if aic_base is None:
    say("AIC base not found in ADT: skipping all AIC reads")
else:
    for off, nm in ((0x0004, "INFO"), (0x2000, "WHOAMI"), (0x4200, "HW_IRQ_MON"),
                    (0x4100, "MASK_SET[0]"), (0x3000, "TARGET_CPU[0]"), (0x3004, "TARGET_CPU[1]")):
        assert off != 0x2004
        try:
            v = p.read32(aic_base + off)
            extra = f"  NR_IRQ={v & 0xffff}" if off == 4 else ""
            say(f"AIC+0x{off:04x} {nm} = 0x{v:08x}{extra}")
        except Exception as e:
            say(f"AIC+0x{off:04x} {nm} = EXCEPTION {type(e).__name__}: {e}")
say("done")
