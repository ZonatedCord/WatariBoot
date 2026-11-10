# SPDX-License-Identifier: MIT
# #60 E2H stuck-bit test on ONE P core (cpu7). WRITES HCR_EL2 (try E2H=0), reads back, restores
# the original value in the same instruction block. Volatile CPU state only.
import sys
from m1n1.proxy import UartInterface, M1N1Proxy, REGION_RX_EL1
from m1n1.proxyutils import ProxyUtils, bootstrap_port
from m1n1.sysreg import HCR_EL2
log = open(sys.argv[1], "w")
def say(*a):
    s = " ".join(str(x) for x in a); print(s, flush=True); log.write(s + "\n"); log.flush()
iface = UartInterface(); p = M1N1Proxy(iface, debug=False); bootstrap_port(iface, p); u = ProxyUtils(p)
def mrs_i(enc, rt):
    op0, op1, crn, crm, op2 = enc
    return 0xd5200000 | (op0 << 19) | (op1 << 16) | (crn << 12) | (crm << 8) | (op2 << 5) | rt
def msr_i(enc, rt):
    return mrs_i(enc, rt) & ~(1 << 21)
H = (3, 4, 1, 1, 0)                      # HCR_EL2
assert mrs_i((3, 0, 0, 0, 0), 0) == 0xd5380000   # mrs x0, midr_el1 sanity check
ISB = 0xd5033fdf
blob = (mrs_i(H, 1),                     # mrs x1, hcr_el2
        0xd2c00083,                      # movz x3, #4, lsl #32   (1 << 34 = E2H)
        0x8a230022,                      # bic x2, x1, x3
        msr_i(H, 2), ISB,                # msr hcr_el2, x2 ; isb
        mrs_i(H, 0),                     # mrs x0, hcr_el2  (read back)
        msr_i(H, 1), ISB)                # msr hcr_el2, x1 ; isb  (restore)
CPU = 7
if not p.smp_is_alive(CPU):
    p.smp_start_secondaries()
say(f"cpu{CPU} alive: {p.smp_is_alive(CPU)}")
call = lambda addr, *a: p.smp_call_sync(CPU, addr & ~REGION_RX_EL1, *a)
before = u.mrs(HCR_EL2, call=call)
say(f"HCR_EL2 before        = 0x{before:016x}  E2H={(before >> 34) & 1}")
back = u.exec(blob, call=call)
say(f"HCR_EL2 after E2H:=0   = 0x{back:016x}  E2H={(back >> 34) & 1}")
after = u.mrs(HCR_EL2, call=call)
say(f"HCR_EL2 after restore = 0x{after:016x}  E2H={(after >> 34) & 1}")
say("RESULT: E2H " + ("STUCK at 1 (write of 0 ignored)" if (back >> 34) & 1 else "is WRITABLE (read back 0)"))
