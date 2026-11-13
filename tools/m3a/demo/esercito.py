# SPDX-License-Identifier: MIT
# Easter egg: THE BANANA ARMY. Original pixel-art monkeys in nappies dancing and eating bananas, random
# fart clouds, and a last row of bigger "alpha" monkeys (gold crown, sunglasses). Drawn on the M1 screen
# through m1n1's fb_fill/fb_clear: framebuffer RAM writes only, gone at reboot.
import random, time
from m1n1.proxy import UartInterface, M1N1Proxy
from m1n1.proxyutils import ProxyUtils, bootstrap_port
iface = UartInterface(); p = M1N1Proxy(iface, debug=False); bootstrap_port(iface, p); u = ProxyUtils(p)
W, H = int(u.ba.video.width), int(u.ba.video.height)
BG, GROUND = 0x2a8c4a, 0x3f2a14
PAL = {'K': 0x1a1a1a, 'F': 0xd9a877, 'E': 0xffffff, 'P': 0x111111, 'M': 0x8a2a2a,
       'W': 0xf4f4f4, 'S': 0xbdbdbd, 'Y': 0xffd400, 'B': 0x6b4a12, 'G': 0x9bd36a,
       'O': 0xffc400, 'R': 0xe8322f, 'D': 0x050505}
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

# arms as cell lists (x, y) on the 20 x 21 body grid, symmetric around x = 9.5, from the shoulders (row 11)
def mirror(cells): return [(19 - x, y) for (x, y) in cells]
UP_L   = [(3, 11), (3, 10), (2, 10), (2, 9), (1, 9), (1, 8), (1, 7), (0, 7), (0, 6), (1, 6)]
DOWN_L = [(3, 11), (3, 12), (2, 12), (2, 13), (2, 14), (2, 15), (1, 15), (2, 16), (1, 16)]
EAT_L  = [(3, 11), (4, 11), (5, 10), (5, 9), (6, 9), (6, 8)]           # bent to the mouth
UP_R, DOWN_R = mirror(UP_L), mirror(DOWN_L)
BANANA_EAT = [(7, 8), (8, 9), (9, 9), (10, 9)]                         # across the mouth
BANANA_UP = [(0, 3), (0, 4), (0, 5), (1, 5)]                           # held high
CROWN = [(6, 0), (8, 0), (10, 0), (12, 0), (6, 1), (7, 1), (8, 1), (9, 1), (10, 1), (11, 1), (12, 1), (13, 1)]
GLASSES = [(6, 4), (7, 4), (8, 4), (9, 4), (10, 4), (11, 4), (12, 4), (13, 4), (7, 5), (8, 5), (11, 5), (12, 5)]
def cells_fill(x0, y0, c, cells, col):
    for (x, y) in cells:
        p.fb_fill(x0 + x * c, y0 + y * c, c, c, col)
def body(x0, y0, c, rows=None):
    for r, row in enumerate(BODY):
        if rows is not None and r not in rows:
            continue
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
def extras(x0, y0, c, alpha):
    if alpha:
        cells_fill(x0, y0 - 2 * c, c, CROWN, PAL['O'])
        p.fb_fill(x0 + 9 * c, y0 - 2 * c, 2 * c, c, PAL['R'])          # crown jewel
        cells_fill(x0, y0, c, GLASSES, PAL['D'])
def pose(x0, y0, c, kind, alpha):
    # clear the arm zones, repaint the body rows under them, then draw the pose
    p.fb_fill(x0, y0 + 3 * c, 6 * c, 14 * c, BG); p.fb_fill(x0 + 14 * c, y0 + 3 * c, 6 * c, 14 * c, BG)
    body(x0, y0, c, rows=set(range(3, 17)))
    extras(x0, y0, c, alpha)
    K = PAL['K']
    if kind == 'up':
        cells_fill(x0, y0, c, UP_L, K); cells_fill(x0, y0, c, UP_R, K); cells_fill(x0, y0, c, BANANA_UP, PAL['Y'])
    elif kind == 'eat':
        cells_fill(x0, y0, c, EAT_L, K); cells_fill(x0, y0, c, DOWN_R, K); cells_fill(x0, y0, c, BANANA_EAT, PAL['Y'])
    else:
        cells_fill(x0, y0, c, DOWN_L, K); cells_fill(x0, y0, c, DOWN_R, K)
F = {'T':"1111100100001000010000100",'H':"1000110001111111000110001",'E':"1111110000111101000011111",
     'B':"1111010001111101000111110",'A':"0111010001111111000110001",'N':"1000111001101011001110001",
     'R':"1111010001111101001010001",'M':"1000111011101011000110001",'Y':"1000101010001000010000100",
     'P':"1111010001111101000010000",'L':"1000010000100001000011111",'S':"0111110000011100000111110",
     '!':"0010000100001000000000100",' ':"0"*25}
def text_at(s, x0, y, px, col):
    for i, ch in enumerate(s):
        for k, b in enumerate(F.get(ch, F[' '])):
            if b == '1':
                p.fb_fill(x0 + (i * 6 + k % 5) * px, y + (k // 5) * px, px, px, col)
def text(s, y, px, col):
    text_at(s, (W - len(s) * 6 * px) // 2, y, px, col)
# troops: 3 rows of 8 small monkeys; alphas: 1 row of 6 big ones
C, CA = 9, 12
troops = [(x, y, C, False) for y in (260, 520, 780)
          for x in [(W - 8 * 26 * C) // 2 + 3 * C + i * 26 * C for i in range(8)]]
alphas = [(x, 1090, CA, True) for x in [(W - 6 * 30 * CA) // 2 + 5 * CA + i * 30 * CA for i in range(6)]]
army = troops + alphas
p.fb_clear(BG)
p.fb_fill(0, H - 110, W, 110, GROUND)
text("THE BANANA ARMY!", 70, 14, 0xffd400)
text_at("ALPHA SQUAD", 60, 1035, 5, PAL['O'])
for (x, y, c, a) in army:
    body(x, y, c); extras(x, y, c, a)
GAS = (0x7fd34a, 0xa6e05a, 0x5cb83a)
random.seed(42)
clouds = {}
t0, f = time.time(), 0
while time.time() - t0 < 50:
    for i, (x, y, c, a) in enumerate(army):
        kind = ('up', 'down', 'eat', 'down')[(f + i) % 4]
        pose(x, y, c, kind, a)
    for i in list(clouds):
        st, cx, cy, c = clouds[i]
        age = f - st
        if age > 3:
            p.fb_fill(cx - 7 * c, cy - 7 * c, 9 * c, 14 * c, BG)
            p.fb_fill(army[i][0], army[i][1] - 6 * army[i][2], 20 * army[i][2], 4 * army[i][2], BG)
            del clouds[i]
        else:
            s = (2 + 2 * age) * c
            p.fb_fill(cx - s, cy - s // 2, s, s, GAS[age % 3])
            p.fb_fill(cx - s // 2 - 2 * c, cy - s, s // 2 + c, s, GAS[(age + 1) % 3])
    for _ in range(3):
        i = random.randrange(len(army))
        if i not in clouds:
            x, y, c, a = army[i]
            clouds[i] = (f, x - c, y + 17 * c, c)
            text_at("PRRT!", x + 2 * c, y - 6 * c, max(2, c // 4), 0xa6e05a)
    f += 1
print(f"army done, {f} frames, {len(troops)} troops + {len(alphas)} alphas")
