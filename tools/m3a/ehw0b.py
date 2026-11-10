# SPDX-License-Identifier: MIT
# E-HW0 part 2, READ-ONLY: row 7 (ADT ranges, no reads there) + AIC reads (rows 1, 2, 14). Never 0x2004.
import sys, traceback
from m1n1.proxy import UartInterface, M1N1Proxy
from m1n1.proxyutils import ProxyUtils, bootstrap_port
log = open(sys.argv[1], "w")
def say(*a):
    s = " ".join(str(x) for x in a); print(s, flush=True); log.write(s + "\n"); log.flush()
iface = UartInterface(); p = M1N1Proxy(iface, debug=False); bootstrap_port(iface, p); u = ProxyUtils(p)
adt = u.adt
say("== row 7: ADT ranges overlapping [0xFFFFC000, 0x100000000) (addresses NOT read) ==")
LO, HI = 0xFFFFC000, 0x100000000
hits = 0
for n in adt.walk_tree():
    try:
        regs = n._properties.get("reg", None)
        if not regs or not hasattr(regs, "__len__"):
            continue
        for i in range(len(regs)):
            try:
                a, s = n.get_reg(i)
            except Exception:
                continue
            if isinstance(a, int) and a < HI and a + s > LO:
                hits += 1; say(f"  {n._path} reg[{i}] 0x{a:x}+0x{s:x}")
    except Exception:
        say("  node error: " + traceback.format_exc().splitlines()[-1])
say(f"  overlaps found: {hits}")
try:
    say("  /chosen/memory-map keys: " + ", ".join(adt["/chosen/memory-map"]._properties.keys()))
except Exception as e:
    say(f"  /chosen/memory-map: {e}")
say("== AIC reads; never offset 0x2004 ==")
aic = adt["/arm-io/aic"].get_reg(0)[0]
say(f"AIC base 0x{aic:x}")
for off, nm in ((0x0004, "INFO"), (0x2000, "WHOAMI"), (0x4200, "HW_IRQ_MON"),
                (0x4100, "MASK_SET[0]"), (0x3000, "TARGET_CPU[0]"), (0x3004, "TARGET_CPU[1]")):
    assert off != 0x2004
    try:
        v = p.read32(aic + off)
        say(f"AIC+0x{off:04x} {nm} = 0x{v:08x}" + (f"  NR_IRQ={v & 0xffff} (bits 31:16 = 0x{v >> 16:x})" if off == 4 else ""))
    except Exception as e:
        say(f"AIC+0x{off:04x} {nm} = EXCEPTION {type(e).__name__}: {e}")
say("done")
