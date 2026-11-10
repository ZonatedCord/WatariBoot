# SPDX-License-Identifier: MIT
# E-HW0 part 3 (rows 2 and 9 on every core). NOT pure read-only: starts the secondary cores in
# m1n1 (volatile, gone at reboot), then on each core runs `mrs` and one `ldr w0,[x0]` of AIC WHOAMI.
# No register or memory writes. Never AIC EVENT (0x2004).
import sys
from m1n1.proxy import UartInterface, M1N1Proxy, REGION_RX_EL1
from m1n1.proxyutils import ProxyUtils, bootstrap_port
from m1n1.sysreg import *
log = open(sys.argv[1], "w")
def say(*a):
    s = " ".join(str(x) for x in a); print(s, flush=True); log.write(s + "\n"); log.flush()
iface = UartInterface(); p = M1N1Proxy(iface, debug=False); bootstrap_port(iface, p); u = ProxyUtils(p)
aic = u.adt["/arm-io/aic"].get_reg(0)[0]
WHOAMI = aic + 0x2000
assert WHOAMI - aic != 0x2004
LDR_W0_X0 = 0xb9400000  # ldr w0, [x0]
p.smp_start_secondaries()
for cpu in range(8):
    try:
        alive = cpu == 0 or p.smp_is_alive(cpu)
        if not alive:
            say(f"cpu{cpu}: not alive"); continue
        if cpu == 0:
            call = None
        else:
            call = (lambda c: (lambda addr, *a: p.smp_call_sync(c, addr & ~REGION_RX_EL1, *a)))(cpu)
        mpidr = u.mrs(MPIDR_EL1, call=call)
        midr = u.mrs(MIDR_EL1, call=call)
        who = u.exec((LDR_W0_X0,), WHOAMI, call=call) & 0xffffffff
        say(f"cpu{cpu}: MPIDR=0x{mpidr:x} (Aff1={(mpidr>>8)&0xff} Aff0={mpidr&0xff})  MIDR=0x{midr:x} (part 0x{(midr>>4)&0xfff:x} rev {midr&0xf} var {(midr>>20)&0xf})  WHOAMI={who}")
    except Exception as e:
        say(f"cpu{cpu}: EXCEPTION {type(e).__name__}: {e}")
say("done")
