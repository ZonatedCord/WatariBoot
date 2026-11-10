# SPDX-License-Identifier: MIT
"""Watari Boot logo: original pixel-art Jolly Roger (skull and crossbones) in 80s synthwave colours.

One pixel grid feeds both outputs: `python3 tools/logo/watari_logo.py docs/img/logo.svg` writes the
SVG used by the README; `pixels()` is also used to draw the logo on the M1 screen through m1n1.
"""
import sys

G = 25                      # grid size (cells)
BG, RING1, RING2 = "#12062a", "#ff2fd0", "#22e6ff"
BONE, SKULL, HOLE = "#d8d8f0", "#f2f2ff", "#12062a"
TEXT1, TEXT2, SHADOW = "#22e6ff", "#ffe14d", "#ff2fd0"

def pixels():
    """List of (x, y, colour) cells on a G x G grid, drawn in order."""
    out = []
    c = G // 2
    for y in range(G):                              # neon ring
        for x in range(G):
            d2 = (x - c) ** 2 + (y - c) ** 2
            if 11.2 ** 2 <= d2 < 12.3 ** 2:
                out.append((x, y, RING1))
            elif 10.2 ** 2 <= d2 < 11.2 ** 2:
                out.append((x, y, RING2))
    def bone(x0, y0, x1, y1):                       # 2-cell thick bone with knobbed ends
        n = max(abs(x1 - x0), abs(y1 - y0))
        for i in range(n + 1):
            x = round(x0 + (x1 - x0) * i / n); y = round(y0 + (y1 - y0) * i / n)
            out.append((x, y, BONE)); out.append((x, y + 1, BONE))
        for (x, y, sx) in ((x0, y0, 1 if x1 > x0 else -1), (x1, y1, 1 if x0 > x1 else -1)):
            out.extend([(x - sx, y - 1, BONE), (x - sx, y, BONE), (x, y - 1, BONE),
                        (x - sx, y + 2, BONE), (x - sx, y + 1, BONE), (x, y + 2, BONE)])
    bone(5, 12, 19, 20)
    bone(19, 12, 5, 20)
    skull = [
        "....SSSSSSS....",
        "..SSSSSSSSSSS..",
        ".SSSSSSSSSSSSS.",
        ".SSSSSSSSSSSSS.",
        "SSSHHHSSSHHHSSS",
        "SSSHHHSSSHHHSSS",
        "SSSHHHSSSHHHSSS",
        ".SSSSSSHSSSSSS.",
        "..SSSSHHHSSSS..",
        "...SSSSSSSSS...",
        "....S.S.S.S....",
        "....SSSSSSS....",
    ]
    ox, oy = 5, 2
    for r, row in enumerate(skull):
        for k, ch in enumerate(row):
            if ch == "S":
                out.append((ox + k, oy + r, SKULL))
            elif ch == "H":
                out.append((ox + k, oy + r, HOLE))
    return out

FONT = {
 'W': ["10001","10001","10001","10101","10101","11011","10001"],
 'A': ["01110","10001","10001","11111","10001","10001","10001"],
 'T': ["11111","00100","00100","00100","00100","00100","00100"],
 'R': ["11110","10001","10001","11110","10100","10010","10001"],
 'I': ["11111","00100","00100","00100","00100","00100","11111"],
 'B': ["11110","10001","10001","11110","10001","10001","11110"],
 'O': ["01110","10001","10001","10001","10001","10001","01110"],
 ' ': ["00000"] * 7,
}

def text_cells(s):
    """(x, y) cells of a string in the 5x7 font, 1-cell spacing."""
    return [(i * 6 + c, r) for i, ch in enumerate(s) for r, row in enumerate(FONT[ch])
            for c, b in enumerate(row) if b == "1"]

def svg(cell=12):
    pad = 2
    w1, w2 = 6 * len("WATARI") - 1, 6 * len("BOOT") - 1
    gw = max(G, w1) + 2 * pad
    gh = pad + G + 2 + 7 + 2 + 7 + pad
    r = []
    a = r.append
    a(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {gw * cell} {gh * cell}" '
      f'width="{gw * cell // 2}" shape-rendering="crispEdges" role="img" aria-label="Watari Boot logo">')
    a(f'<rect width="100%" height="100%" rx="{cell * 2}" fill="{BG}"/>')
    ox = (gw - G) // 2
    for (x, y, col) in pixels():
        a(f'<rect x="{(ox + x) * cell}" y="{(pad + y) * cell}" width="{cell}" height="{cell}" fill="{col}"/>')
    for s, col, y0 in (("WATARI", TEXT1, pad + G + 2), ("BOOT", TEXT2, pad + G + 11)):
        x0 = (gw - (6 * len(s) - 1)) // 2
        for (x, y) in text_cells(s):
            a(f'<rect x="{(x0 + x) * cell + cell // 3}" y="{(y0 + y) * cell + cell // 3}" width="{cell}" height="{cell}" fill="{SHADOW}"/>')
            a(f'<rect x="{(x0 + x) * cell}" y="{(y0 + y) * cell}" width="{cell}" height="{cell}" fill="{col}"/>')
    a("</svg>")
    return "\n".join(r) + "\n"

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "docs/img/logo.svg"
    open(out, "w").write(svg())
    print("wrote", out)
