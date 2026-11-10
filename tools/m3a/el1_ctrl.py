# SPDX-License-Identifier: MIT
# Control: can m1n1 run code at EL1 on core 0 (el1_call)? Reads MIDR_EL1 and CurrentEL only.
from m1n1.proxy import UartInterface, M1N1Proxy
from m1n1.proxyutils import ProxyUtils, bootstrap_port
iface = UartInterface(); p = M1N1Proxy(iface, debug=False); bootstrap_port(iface, p); u = ProxyUtils(p)
print("EL1 CurrentEL =", (u.mrs((3, 0, 4, 2, 2), call="el1") >> 2) & 3)
print("EL1 MIDR = 0x%x" % u.mrs((3, 0, 0, 0, 0), call="el1"))
