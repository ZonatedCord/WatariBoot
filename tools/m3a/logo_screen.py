# SPDX-License-Identifier: MIT
# Draws the Watari Boot logo (tools/logo/watari_logo.py) small and centred on the M1 screen.
# Framebuffer RAM writes only (fb_clear/fb_fill), gone at reboot.
import sys
sys.path.insert(0, sys.argv[1])
import watari_logo as L
from m1n1.proxy import UartInterface, M1N1Proxy
from m1n1.proxyutils import ProxyUtils, bootstrap_port
iface = UartInterface(); p = M1N1Proxy(iface, debug=False); bootstrap_port(iface, p); u = ProxyUtils(p)
W, H = int(u.ba.video.width), int(u.ba.video.height)
hx = lambda c: int(c[1:], 16)
cell = 16
gw, gh = 31, L.G + 2 + 7 + 2 + 7
x0, y0 = (W - gw * cell) // 2, (H - gh * cell) // 2
p.fb_clear(hx(L.BG))
ox = x0 + (gw - L.G) // 2 * cell
for (x, y, col) in L.pixels():
    p.fb_fill(ox + x * cell, y0 + y * cell, cell, cell, hx(col))
for s, col, ty in (("WATARI", L.TEXT1, L.G + 2), ("BOOT", L.TEXT2, L.G + 11)):
    tx = x0 + (gw - (6 * len(s) - 1)) // 2 * cell
    for (x, y) in L.text_cells(s):
        p.fb_fill(tx + x * cell + cell // 3, y0 + (ty + y) * cell + cell // 3, cell, cell, hx(L.SHADOW))
        p.fb_fill(tx + x * cell, y0 + (ty + y) * cell, cell, cell, hx(col))
print("logo drawn")
