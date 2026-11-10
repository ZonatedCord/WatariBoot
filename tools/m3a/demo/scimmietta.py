# SPDX-License-Identifier: MIT
# Original pixel-art monkey in a nappy, dancing and eating bananas, on the M1 screen.
# Framebuffer RAM writes only (fb_fill/fb_clear), gone at reboot.
import time
from m1n1.proxy import UartInterface, M1N1Proxy
from m1n1.proxyutils import ProxyUtils, bootstrap_port
iface = UartInterface(); p = M1N1Proxy(iface, debug=False); bootstrap_port(iface, p); u = ProxyUtils(p)
W, H = int(u.ba.video.width), int(u.ba.video.height)
BG = 0x2a8c4a   # jungle green
PAL = {'K': 0x1a1a1a, 'F': 0xd9a877, 'E': 0xffffff, 'P': 0x111111, 'M': 0x8a2a2a,
       'W': 0xf4f4f4, 'S': 0xbdbdbd, 'Y': 0xffd400, 'B': 0x6b4a12, 'G': 0x9bd36a}
BODY = [
    "......KKKKKKKK......",
    "....KKKKKKKKKKKK....",
    "..FFKKKKKKKKKKKKFF..",
    ".FFFKKFFFFFFFFKKFFF.",
    ".FFFKFFEEFFEEFFKFFF.",
    "..FFKFFEPFFEPFFKFF..",
    "....KFFFFFFFFFFK....",
    "....KFFFFKKFFFFK....",
    ".....KFFMMMMFFK.....",
    "......KKFFFFKK......",
    ".....KKKKKKKKKK.....",
    "....KKKFFFFFFKKK....",
    "....KKFFFFFFFFKK....",
    "....KKFFFFFFFFKK....",
    "....KWWWWWWWWWWK....",
    "....WWWWWWWWWWWW....",
    "....WWWSWWWWSWWW....",
    ".....WWWWWWWWWW.....",
    ".....KKK....KKK.....",
    ".....KKK....KKK.....",
    "....KKKK....KKKK....",
]
CELL = 22
SW, SH = 20 * CELL, len(BODY) * CELL
cx, cy = W // 2, H // 2 - 80
def runs(rows, x0, y0, c=CELL):
    for r, row in enumerate(rows):
        k = 0
        while k < len(row):
            ch = row[k]
            if ch == '.':
                k += 1; continue
            j = k
            while j < len(row) and row[j] == ch:
                j += 1
            p.fb_fill(x0 + k * c, y0 + r * c, (j - k) * c, c, PAL[ch])
            k = j
def arms(x0, y0, up, left_len):
    # left arm holds the banana to the mouth or up high; right arm waves
    c = CELL
    if up:
        p.fb_fill(x0 + 1 * c, y0 + 4 * c, 3 * c, c, PAL['K']); p.fb_fill(x0 + 1 * c, y0 + 0 * c, c, 5 * c, PAL['K'])
        p.fb_fill(x0 + 16 * c, y0 + 4 * c, 3 * c, c, PAL['K']); p.fb_fill(x0 + 18 * c, y0 + 0 * c, c, 5 * c, PAL['K'])
        by = y0 - 3 * c
        if left_len > 0:
            p.fb_fill(x0 + 0 * c, by, c, left_len * c, PAL['Y']); p.fb_fill(x0 + 0 * c, by + left_len * c, c, c, PAL['B'])
    else:
        p.fb_fill(x0 + 1 * c, y0 + 11 * c, 3 * c, c, PAL['K']); p.fb_fill(x0 + 3 * c, y0 + 8 * c, c, 4 * c, PAL['K'])
        p.fb_fill(x0 + 16 * c, y0 + 11 * c, 3 * c, c, PAL['K']); p.fb_fill(x0 + 18 * c, y0 + 12 * c, c, 4 * c, PAL['K'])
        if left_len > 0:   # banana at the mouth
            p.fb_fill(x0 + 4 * c, y0 + 8 * c, left_len * c // 2 + c, c, PAL['Y'])
p.fb_clear(BG)
p.fb_fill(0, H - 220, W, 220, 0x3f2a14)                       # ground
for i in range(9):                                              # leaves
    p.fb_fill(80 + i * 290, 40 + (i % 3) * 30, 180, 50, PAL['G'])
def text(s, y, px, col):
    F = {'B':"1111010001111101000111110",'A':"0111010001111111000110001",'N':"1000111001101011001110001",
         'G':"0111110000101111000101111",'E':"1111110000111101000011111",'O':"0111010001100011000101110",
         'S':"0111110000011100000111110",' ':"0"*25,'!':"0010000100001000000000100",'M':"1000111011101011000110001",
         'C':"0111110000100001000001111",'I':"0111000100001000010001110",'H':"1000110001111111000110001",'T':"1111100100001000010000100"}
    x0 = (W - len(s) * 6 * px) // 2
    for i, ch in enumerate(s):
        for k, b in enumerate(F.get(ch, F[' '])):
            if b == '1':
                p.fb_fill(x0 + (i * 6 + k % 5) * px, y + (k // 5) * px, px, px, col)
text("BANANE!", 170, 14, 0xffd400)
peels, bananas, bite = 0, 6, 8
t0, f = time.time(), 0
while time.time() - t0 < 40:
    sway = (-2, 0, 2, 0)[f % 4] * CELL
    x0, y0 = cx - SW // 2 + sway, cy - SH // 2 + (CELL if f % 2 else 0)
    p.fb_fill(cx - SW // 2 - 4 * CELL, cy - SH // 2 - 5 * CELL, SW + 8 * CELL, SH + 7 * CELL, BG)
    runs(BODY, x0, y0)
    up = (f % 4) in (1, 2)
    arms(x0, y0, up, bite)
    if not up:
        bite -= 1
        if bite <= 0:
            peels += 1; bananas -= 1; bite = 8
            p.fb_fill(200 + peels * 120, H - 260, 80, 30, PAL['Y']); p.fb_fill(200 + peels * 120 + 10, H - 230, 20, 30, PAL['Y'])
    for i in range(max(bananas, 0)):                            # banana pile
        p.fb_fill(W - 600 + i * 70, H - 270, 60, 26, PAL['Y']); p.fb_fill(W - 600 + i * 70 + 50, H - 278, 12, 12, PAL['B'])
    for i in range(max(bananas, 0), 6):
        p.fb_fill(W - 600 + i * 70, H - 278, 62, 34, 0x3f2a14)
    if bananas <= 0:
        bananas = 6
    f += 1
    time.sleep(0.15)
print(f"monkey done, {f} frames, {peels} bananas eaten")
