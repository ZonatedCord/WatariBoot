# SPDX-License-Identifier: MIT
"""Watari Boot logo: original pixel-art torii gate in black, red and gold.

One pixel grid feeds both outputs: `python3 tools/logo/watari_logo.py docs/img/logo.svg` writes the
SVG used by the README; `pixels()` is also used to draw the logo on the M1 screen through m1n1.
"""
import sys

G = 25                      # grid size (cells)
BG = "#0d0a08"
RED, DRED, GOLD, GOLD2 = "#c8102e", "#7a0a1c", "#d4a017", "#ffcc33"
TEXT1, TEXT2, SHADOW = "#ffcc33", "#d4a017", "#7a0a1c"

TORII = [                   # R red, D dark red, G gold, Y bright gold
    ".........................",
    ".........................",
    "RRR...................RRR",
    "RRRRRRRRRRRRRRRRRRRRRRRRR",
    ".RRRRRRRRRRRRRRRRRRRRRRR.",
    "...DDDDDDDDDDDDDDDDDDD...",
    ".....RRD....GYG....RRD...",
    ".....RRD....GYG....RRD...",
    ".....RRD....GYG....RRD...",
    "...RRRRRRRRRRRRRRRRRRRD..",
    "...DDDDDDDDDDDDDDDDDDDD..",
    ".....RRD...........RRD...",
    ".....RRD...........RRD...",
    ".....RRD...........RRD...",
    ".....RRD...........RRD...",
    ".....RRD...........RRD...",
    ".....RRD...........RRD...",
    ".....RRD...........RRD...",
    ".....RRD...........RRD...",
    ".....RRD...........RRD...",
    ".....RRD...........RRD...",
    "....GGGGG.........GGGGG..",
    "....YYYYY.........YYYYY..",
    ".GGGGGGGGGGGGGGGGGGGGGGG.",
    ".........................",
]
_COL = {"R": RED, "D": DRED, "G": GOLD, "Y": GOLD2}

def pixels():
    """List of (x, y, colour) cells on a G x G grid, drawn in order."""
    return [(x, y, _COL[ch]) for y, row in enumerate(TORII)
            for x, ch in enumerate(row) if ch in _COL]

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
