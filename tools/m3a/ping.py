# SPDX-License-Identifier: MIT
# Read-only: is m1n1 answering? (no setup, no writes)
from m1n1.proxy import UartInterface, M1N1Proxy
iface = UartInterface(); p = M1N1Proxy(iface, debug=False)
print("base", hex(p.get_base()))
