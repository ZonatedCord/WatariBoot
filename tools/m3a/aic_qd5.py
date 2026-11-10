# SPDX-License-Identifier: MIT
# Day 2 AIC experiments (Q-D5 family) on an IRQ number no ADT device uses. Writes AIC registers only
# (TARGET_CPU, SW_SET/SW_CLR, MASK_SET/MASK_CLR) and reads EVENT on purpose; restores at the end.
import sys
from m1n1.proxy import UartInterface, M1N1Proxy, REGION_RX_EL1
from m1n1.proxyutils import ProxyUtils, bootstrap_port
log = open(sys.argv[1], "w")
def say(*a):
    s = " ".join(str(x) for x in a); print(s, flush=True); log.write(s + "\n"); log.flush()
iface = UartInterface(); p = M1N1Proxy(iface, debug=False); bootstrap_port(iface, p); u = ProxyUtils(p)
A = u.adt["/arm-io/aic"].get_reg(0)[0]
nr = p.read32(A + 4) & 0xffff
used = set()
for node in u.adt.walk_tree():
    ip = node._properties.get("interrupt-parent", None)
    for v in (node._properties.get("interrupts", None) or []):
        if isinstance(v, int):
            used.add(v)
free = [n for n in range(nr - 1, nr - 64, -1) if n not in used]
N = free[0]
say(f"AIC 0x{A:x} NR_IRQ {nr}; ADT interrupt numbers in use: {len(used)}; test IRQ = {N}")
CPU = 7
call7 = lambda addr, *a: p.smp_call_sync(CPU, addr & ~REGION_RX_EL1, *a)
daif = u.mrs((3, 3, 4, 2, 1), call=call7)
say(f"cpu{CPU} DAIF = 0x{daif:x} (I masked: {bool(daif & 0x80)})")
if not daif & 0x80:
    say("ABORT: IRQs not masked on the target core"); sys.exit(1)
w, b = N // 32, 1 << (N % 32)
TGT, SWS, SWC, MS, MC, EV = 0x3000 + 4 * N, 0x4000 + 4 * w, 0x4080 + 4 * w, 0x4100 + 4 * w, 0x4180 + 4 * w, 0x2004
r = lambda o: p.read32(A + o)
def wr(o, v): p.write32(A + o, v)
masked = lambda: bool(r(MS) & b)
swbit = lambda: bool(r(SWS) & b)
def ev():   # EVENT is a per-core view: read it on the target core
    v = u.exec((0xb9400000,), A + EV, call=call7) & 0xffffffff
    return v, (v >> 16) & 0xff, v & 0xffff
orig_tgt, orig_mask = r(TGT), masked()
say(f"initial: TARGET_CPU[{N}]=0x{orig_tgt:x} masked={orig_mask} sw={swbit()}")
drain = [ev() for _ in range(4)]
say(f"pre-drain EVENT reads: {[hex(d[0]) for d in drain]}")
wr(TGT, 1 << CPU)                            # route to the target core only
wr(MS, b)                                    # make sure it is masked
say(f"T1 masked, no SW: EVENT={hex(ev()[0])} (expect 0)")
wr(SWS, b)
say(f"T2 SW_SET while masked: sw={swbit()} masked={masked()} EVENT={hex(ev()[0])} (expect 0: masked source does not deliver)")
wr(MC, b)
v, t, n = ev()
say(f"T3 unmask with SW pending: EVENT=0x{v:x} type={t} num={n} (expect type 1, num {N})")
say(f"T4 after EVENT read: masked={masked()} (auto-mask on read?) sw={swbit()} (does EVENT clear SW_SET?)")
say(f"T5 second EVENT read: {hex(ev()[0])} (expect 0)")
wr(MC, b)
v2 = ev()
say(f"T6 unmask again, SW still set: EVENT=0x{v2[0]:x} (re-delivers if SW is level-like)")
wr(SWC, b)
say(f"T7 after SW_CLR: sw={swbit()}")
wr(MC, b)
say(f"T8 unmask with nothing pending: EVENT={hex(ev()[0])} (spurious? expect 0)")
wr(MS, b)
wr(SWC, b)
wr(TGT, orig_tgt)
if not orig_mask:
    wr(MC, b)
say(f"restored: TARGET_CPU=0x{r(TGT):x} masked={masked()} sw={swbit()}")
say("done")
