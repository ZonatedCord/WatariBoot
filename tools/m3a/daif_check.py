# SPDX-License-Identifier: MIT
# Read-only: DAIF on each core (mrs only).
from m1n1.proxy import UartInterface, M1N1Proxy, REGION_RX_EL1
from m1n1.proxyutils import ProxyUtils, bootstrap_port
iface = UartInterface(); p = M1N1Proxy(iface, debug=False); bootstrap_port(iface, p); u = ProxyUtils(p)
for c in range(8):
    call = None if c == 0 else (lambda cc: (lambda addr, *a: p.smp_call_sync(cc, addr & ~REGION_RX_EL1, *a)))(c)
    try:
        print(f"cpu{c} DAIF=0x{u.mrs((3, 3, 4, 2, 1), call=call):x}")
    except Exception as e:
        print(f"cpu{c} EXC {e}")
