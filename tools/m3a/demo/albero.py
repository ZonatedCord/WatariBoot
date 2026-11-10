# SPDX-License-Identifier: MIT
# Draws a Christmas tree on the target's screen through m1n1's framebuffer calls (fb_clear/fb_fill).
# Writes only the framebuffer in RAM (volatile, gone at reboot). No register, NVRAM or disk write.
import random
from m1n1.proxy import UartInterface, M1N1Proxy
from m1n1.proxyutils import ProxyUtils, bootstrap_port
iface = UartInterface(); p = M1N1Proxy(iface, debug=False); bootstrap_port(iface, p); u = ProxyUtils(p)
W, H = u.ba.video.width, u.ba.video.height
print(f"screen {W}x{H}")
p.fb_clear(0x0a1430)                       # night blue
random.seed(25)
for _ in range(160):                       # snow
    s = random.choice((4, 6, 8))
    p.fb_fill(random.randrange(W - s), random.randrange(H - s), s, s, 0xffffff)
cx, top, base, step = W // 2, 260, 1250, 10
greens = (0x0b6b2e, 0x128a3c, 0x0f7a35)
for y in range(top, base, step):           # tree: three stacked triangles
    t = y - top
    tier = min(2, t * 3 // (base - top))
    tier_top = top + tier * (base - top) // 3 - (60 if tier else 0)
    half = 80 + (y - tier_top) * 0.9 + tier * 70
    half = int(min(half, 520))
    p.fb_fill(cx - half, y, 2 * half, step, greens[tier])
p.fb_fill(cx - 55, base, 110, 170, 0x6b3a1e)  # trunk
p.fb_fill(cx - 200, base + 170, 400, 40, 0xb22222)  # pot
for (dx, dy, c) in ((-150, 520, 0xff2a2a), (120, 600, 0xffd700), (-60, 760, 0x2a8cff), (210, 900, 0xff2a2a),
                    (-260, 980, 0xffd700), (40, 1050, 0xff6ad5), (-120, 1150, 0x2a8cff), (300, 1170, 0xffd700),
                    (60, 420, 0xff6ad5), (-330, 1190, 0xff2a2a), (180, 780, 0xffffff), (-20, 960, 0xffd700)):
    p.fb_fill(cx + dx - 18, dy - 18, 36, 36, c)
for i in range(5):                          # star
    p.fb_fill(cx - 12 - i * 14, 200 + i * 10, 24 + i * 28, 14, 0xffd700)
for i in range(5):
    p.fb_fill(cx - 12 - (4 - i) * 14, 250 + i * 10, 24 + (4 - i) * 28, 14, 0xffd700)
for x0 in range(0, W, 60):                  # ground
    p.fb_fill(x0, H - 120, 60, 120, 0xe8eef8)
print("tree drawn")
