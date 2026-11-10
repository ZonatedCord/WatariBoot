# SPDX-License-Identifier: MIT
# Draws the Watari Boot logo (tools/logo/watari_logo.py, screen_cells()) centred on the M1 screen
# through m1n1's framebuffer calls. Framebuffer RAM writes only (fb_clear/fb_fill), gone at reboot.
# Usage: PYTHONPATH=<m1n1>/proxyclient python3 logo_screen.py <repo>/tools/logo
import sys
sys.path.insert(0, sys.argv[1])
import watari_logo as L
from m1n1.proxy import UartInterface, M1N1Proxy
from m1n1.proxyutils import ProxyUtils, bootstrap_port
iface = UartInterface(); p = M1N1Proxy(iface, debug=False); bootstrap_port(iface, p); u = ProxyUtils(p)
W, H = int(u.ba.video.width), int(u.ba.video.height)
hx = lambda c: int(c[1:], 16)
cell = 12
x0, y0 = (W - L.LOGO_W * cell) // 2, (H - L.LOGO_H * cell) // 2
p.fb_clear(hx(L.BG))
for (x, y, col) in L.screen_cells():
    p.fb_fill(x0 + x * cell, y0 + y * cell, cell, cell, hx(col))
print("logo drawn")
