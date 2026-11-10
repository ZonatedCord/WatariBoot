# SPDX-License-Identifier: MIT
# Animation over the tree drawn by albero.py: falling snow, blinking lights, twinkling star.
# Framebuffer RAM writes only (fb_fill), gone at reboot.
import random, time
from m1n1.proxy import UartInterface, M1N1Proxy
from m1n1.proxyutils import ProxyUtils, bootstrap_port
iface = UartInterface(); p = M1N1Proxy(iface, debug=False); bootstrap_port(iface, p); u = ProxyUtils(p)
W, H = int(u.ba.video.width), int(u.ba.video.height)
BG, SNOW = 0x0a1430, 0xffffff
cx, top, base = W // 2, 260, 1250
def in_tree(x, y, s):  # keep snow off the tree, trunk, pot, star and ground
    return (cx - 560 < x < cx + 560 and 180 < y < base + 230) or y + s > H - 125
lights = [(-150, 520), (120, 600), (-60, 760), (210, 900), (-260, 980), (40, 1050), (-120, 1150),
          (300, 1170), (60, 420), (-330, 1190), (180, 780), (-20, 960)]
pal = [0xff2a2a, 0xffd700, 0x2a8cff, 0xff6ad5, 0x39ff7a, 0xffffff]
random.seed(7)
flakes = [[random.randrange(W), random.randrange(H - 140), random.choice((4, 6, 8)), random.choice((6, 9, 12))] for _ in range(70)]
t0, frame = time.time(), 0
while time.time() - t0 < 90:
    for f in flakes:
        x, y, s, v = f
        if not in_tree(x, y, s):
            p.fb_fill(x, y, s, s, BG)
        y += v; x += random.choice((-2, 0, 2))
        if y > H - 140:
            y = random.randrange(0, 60); x = random.randrange(W)
        f[0], f[1] = max(0, min(W - s, x)), y
        if not in_tree(f[0], f[1], s):
            p.fb_fill(f[0], f[1], s, s, SNOW)
    if frame % 3 == 0:
        for i, (dx, dy) in enumerate(lights):
            p.fb_fill(cx + dx - 18, dy - 18, 36, 36, pal[(i + frame // 3) % len(pal)])
        star = 0xffd700 if (frame // 3) % 2 == 0 else 0xfff6a0
        for i in range(5):
            p.fb_fill(cx - 12 - i * 14, 200 + i * 10, 24 + i * 28, 14, star)
            p.fb_fill(cx - 12 - (4 - i) * 14, 250 + i * 10, 24 + (4 - i) * 28, 14, star)
    frame += 1
print(f"animation done, {frame} frames")
