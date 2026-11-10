# SPDX-License-Identifier: MIT
"""Watari Boot logo: original pixel-art torii gate in black, red and gold.

One pixel grid feeds both outputs: `python3 tools/logo/watari_logo.py docs/img/logo.svg` writes the
SVG used by the README; `pixels()` is also used to draw the logo on the M1 screen through m1n1.
"""
import sys

LOGO_W, LOGO_H = 51, 28     # whole logo, gate and text on one grid; W is odd, axis = column 25
G = LOGO_W
BG = "#0d0a08"
RED, DRED, GOLD, GOLD2 = "#c8102e", "#7a0a1c", "#d4a017", "#ffcc33"
TEXT1, TEXT2, SHADOW = "#ffcc33", "#d4a017", "#7a0a1c"
AXIS = LOGO_W // 2

def _gate():
    """Gate cells, built for the left half (x <= AXIS) and mirrored, so it is exactly symmetric."""
    half = {}
    def put(x0, x1, y, col):
        for x in range(x0, x1 + 1):
            half[(x, y)] = col
    put(0, 3, 0, RED)                       # kasagi (top beam), upturned tips
    put(0, AXIS, 1, RED)
    put(1, AXIS, 2, RED)
    put(4, AXIS, 3, DRED)
    for y in range(4, 25):                  # pillars
        put(4, 5, y, RED); put(6, 6, y, DRED)
    put(2, AXIS, 13, RED); put(2, AXIS, 14, RED)    # nuki (second beam)
    put(2, AXIS, 15, DRED)
    put(2, 8, 25, GOLD); put(2, 8, 26, GOLD2)       # stone bases
    put(1, AXIS, 27, GOLD)                          # ground line
    out = {}
    for (x, y), col in half.items():
        out[(x, y)] = col
        out[(LOGO_W - 1 - x, y)] = col
    return out

def _text(s, y0, col):
    w = 6 * len(s) - 1
    x0 = (LOGO_W - w) // 2
    return {(x0 + x, y0 + y): col for (x, y) in text_cells(s)}

def pixels():
    """List of (x, y, colour) cells on the LOGO_W x LOGO_H grid, drawn in order (gate, then text)."""
    cells = _gate()
    cells.update(_text("WATARI", 5, TEXT1))     # fused: runs between the kasagi and the nuki
    cells.update(_text("BOOT", 18, TEXT2))      # between the pillars
    return [(x, y, c) for (x, y), c in sorted(cells.items(), key=lambda kv: (kv[0][1], kv[0][0]))]

def screen_cells():
    """The whole logo (gate and text) as (x, y, colour) cells on a LOGO_W x LOGO_H grid."""
    return pixels()

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
    gw, gh = LOGO_W + 2 * pad, LOGO_H + 2 * pad
    r = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {gw * cell} {gh * cell}" '
         f'width="{gw * cell // 2}" shape-rendering="crispEdges" role="img" aria-label="Watari Boot logo">',
         f'<rect width="100%" height="100%" rx="{cell * 2}" fill="{BG}"/>']
    for (x, y, col) in screen_cells():
        r.append(f'<rect x="{(pad + x) * cell}" y="{(pad + y) * cell}" width="{cell}" height="{cell}" fill="{col}"/>')
    r.append("</svg>")
    return "\n".join(r) + "\n"

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "docs/img/logo.svg"
    open(out, "w").write(svg())
    print("wrote", out)
