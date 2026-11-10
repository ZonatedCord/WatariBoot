# SPDX-License-Identifier: MIT
"""Watari Boot logo: original pixel-art torii gate in black, red and gold.

One pixel grid feeds both outputs: `python3 tools/logo/watari_logo.py docs/img/logo.svg` writes the
SVG used by the README; `screen_cells()` is also used to draw the logo on the M1 screen through m1n1.
"""
import sys

LOGO_W, LOGO_H = 53, 50     # whole logo, gate and text on one grid; W is odd, axis = column 26
G = LOGO_W
BG = "#0d0a08"
RED, DRED, GOLD, GOLD2 = "#c8102e", "#7a0a1c", "#d4a017", "#ffcc33"
TEXT1, TEXT2, SHADOW = "#ffcc33", "#d4a017", "#7a0a1c"
AXIS = LOGO_W // 2

# Bold arcade pixel font, 9 rows, 2-cell strokes. A, T, I, O, W are exactly mirror-symmetric.
FONT = {
 'A': [".###.", "##.##", "##.##", "##.##", "#####", "##.##", "##.##", "##.##", "##.##"],
 'T': ["#####", "#####", ".###.", ".###.", ".###.", ".###.", ".###.", ".###.", ".###."],
 'I': ["#####", "#####", ".###.", ".###.", ".###.", ".###.", ".###.", "#####", "#####"],
 'O': [".###.", "##.##", "##.##", "##.##", "##.##", "##.##", "##.##", "##.##", ".###."],
 'R': ["####.", "##.##", "##.##", "##.##", "####.", "###..", "##.##", "##.##", "##.##"],
 'B': ["####.", "##.##", "##.##", "##.##", "####.", "##.##", "##.##", "##.##", "####."],
 'W': ["##...##", "##...##", "##...##", "##...##", "##.#.##", "##.#.##", "#######", "###.###", "##...##"],
}

def _width(s):
    return sum(len(FONT[ch][0]) for ch in s) + len(s) - 1

def text_cells(s):
    """(x, y) cells of a string in the font, 1-cell spacing, left edge at x = 0."""
    out, x0 = [], 0
    for ch in s:
        for r, row in enumerate(FONT[ch]):
            out.extend((x0 + c, r) for c, b in enumerate(row) if b == "#")
        x0 += len(FONT[ch][0]) + 1
    return out

def _gate():
    """Gate cells, built for the left half (x <= AXIS) and mirrored, so it is exactly symmetric."""
    half = {}
    def put(x0, x1, y, col):
        for x in range(x0, x1 + 1):
            half[(x, y)] = col
    for x in range(AXIS + 1):               # kasagi (top beam) with upward-curving tips
        top = min(3, x // 4)
        bot = 3 if x < 4 else 5
        for y in range(top, bot + 1):
            half[(x, y)] = RED
        if x >= 4:
            half[(x, 6)] = DRED
            half[(x, 7)] = DRED
    for y in range(8, 47):                  # pillars
        put(4, 5, y, RED)
        put(6, 6, y, DRED)
    for y in (21, 22, 23):                  # nuki (second beam)
        put(2, AXIS, y, RED)
    put(2, AXIS, 24, DRED)
    put(2, 9, 47, GOLD)                     # stone bases
    put(2, 9, 48, GOLD2)
    put(1, AXIS, 49, GOLD)                  # ground line
    out = {}
    for (x, y), col in half.items():
        out[(x, y)] = col
        out[(LOGO_W - 1 - x, y)] = col
    return out

def _text(s, y0, col):
    x0 = (LOGO_W - _width(s)) // 2
    return {(x0 + x, y0 + y): col for (x, y) in text_cells(s)}

def pixels():
    """List of (x, y, colour) cells on the LOGO_W x LOGO_H grid, drawn in order (gate, then text)."""
    cells = _gate()
    cells.update(_text("WATARI", 10, TEXT1))    # fused: between the kasagi and the nuki
    cells.update(_text("BOOT", 31, TEXT2))      # between the pillars
    return [(x, y, c) for (x, y), c in sorted(cells.items(), key=lambda kv: (kv[0][1], kv[0][0]))]

def screen_cells():
    """The whole logo (gate and text) as (x, y, colour) cells on a LOGO_W x LOGO_H grid."""
    return pixels()

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
