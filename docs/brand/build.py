#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Watari Boot logo: exact construction on an equilateral-triangle lattice.

Every vertex sits on a skew lattice (i, j) with module v:
    x = X0 + (i + j/2) * v        y = Y0 + j * (sqrt(3)/2) * v
so every edge keeps i constant (60 deg), i + j constant (120 deg) or j constant (0 deg).
Two numbers drive everything: T (stroke) and a (small side). See README.md.

Usage: python3 -I build.py            writes geometry.json and the SVGs next to this file
       python3 -I build.py --tables   also prints the Markdown vertex tables for README.md
"""
import json
import math
import os
import sys

# ---- the two parameters --------------------------------------------------
T = 6    # stroke thickness, in modules (measured along a lattice row)
a = 13   # small side: B counters, W short arm, accent length, plate corner radius

# ---- derived values ------------------------------------------------------
BOWL = a + 3 * T          # 31  side of each B bowl (an equilateral triangle)
OFF = a + T               # 19  B bowl offset = left edge of the B = row of the middle bar
H = 2 * a + 4 * T         # 50  mark height, in lattice rows
W = OFF + BOWL + OFF      # 69  mark width, in modules
SQ = 4 * (a + T)          # 76  plate side, in modules
R = a                     # 13  plate corner radius, in modules

CANVAS = 1024.0
v = 864.0 / SQ            # module in px on the 1024 canvas (plate = 864 px)
ROW = math.sqrt(3) / 2    # row height / module
X0 = CANVAS / 2 - W / 2 * v
Y0 = CANVAS / 2 - H / 2 * ROW * v

# ---- colours (VGA 16-colour palette) ---------------------------------------
COLORS = {
    "plate": ("#AAAAAA", "VGA light grey (7)"),
    "W": ("#0000AA", "VGA blue (1): Windows"),
    "B": ("#00AAAA", "VGA cyan (3): blue + white (Apple)"),
    "accent": ("#0000AA", "VGA blue (1), same colour as the W"),
    "background": ("#000000", "VGA black (0)"),
    "light cyan": ("#55FFFF", "VGA light cyan (11), spare"),
    "white": ("#FFFFFF", "VGA white (15), spare"),
}

# ---- shapes, in lattice coordinates -----------------------------------------
ARM_END = OFF - T                                   # 13: W arm stops one stroke before the B
SHAPES = {
    # W, first V: left arm band 0 <= i <= T, right arm band H-T <= i+j <= H, cut at i = ARM_END
    "W": [[(0, 0), (T, 0), (T, H - 2 * T), (ARM_END, H - 2 * T + T - ARM_END),
           (ARM_END, H - ARM_END), (0, H)]],
    # B: union of two congruent equilateral triangles of side BOWL, the second OFF rows lower,
    # each with an equilateral counter inset by T
    "B": [[(OFF, 0), (OFF + BOWL, 0), (BOWL, OFF), (OFF + BOWL, OFF), (OFF, H)],
          [(OFF + T, T), (OFF + BOWL - 2 * T, T), (OFF + T, BOWL - 2 * T)],
          [(OFF + T, OFF + T), (OFF + BOWL - 2 * T, OFF + T), (OFF + T, OFF + BOWL - 2 * T)]],
    # accent: the lower bowl's right band (i+j in [H+OFF-T, H+OFF]) continued above it,
    # length a, one stroke T above the bowl
    "accent": [[(H + OFF - T, 0), (H + OFF, 0), (H + OFF - a, a), (H + OFF - T - a, a)]],
}


def check():
    """Every edge on the lattice directions; the stated relations hold."""
    for name, polys in SHAPES.items():
        for poly in polys:
            for (i0, j0), (i1, j1) in zip(poly, poly[1:] + poly[:1]):
                di, dj = i1 - i0, j1 - j0
                assert dj == 0 or di == 0 or di == -dj, (name, (i0, j0), (i1, j1))
    w_arm = SHAPES["W"][0]
    assert w_arm[3][0] + w_arm[3][1] == H - T and w_arm[4][0] + w_arm[4][1] == H
    assert sum(SHAPES["B"][1][1]) == H - T            # upper counter right edge: same band as the W arm
    assert OFF + BOWL == H                            # lower bowl ends on the baseline
    assert BOWL - 3 * T == a                          # counters have side a
    assert SHAPES["accent"][0][2][1] + T == OFF       # gap accent -> bowl is T


def px(i, j):
    return (X0 + (i + j / 2) * v, Y0 + j * ROW * v)


def signed_area(pts):
    return sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(pts, pts[1:] + pts[:1])) / 2


def path_d(polys):
    out, sign = [], None
    for k, poly in enumerate(polys):
        pts = [px(*q) for q in poly]
        s = signed_area(pts) > 0
        if k == 0:
            sign = s
        elif s == sign:
            pts = pts[::-1]                           # counters wind the other way
        out.append("M" + " L".join(f"{x:.3f} {y:.3f}" for x, y in pts) + " Z")
    return " ".join(out)


def svg(colors, plate=True, background=False):
    p0, side, rad = CANVAS / 2 - SQ / 2 * v, SQ * v, R * v
    body = []
    if background:
        body.append(f'<rect width="{CANVAS:g}" height="{CANVAS:g}" fill="{colors["background"]}"/>')
    if plate:
        body.append(f'<rect x="{p0:.3f}" y="{p0:.3f}" width="{side:.3f}" height="{side:.3f}" '
                    f'rx="{rad:.3f}" fill="{colors["plate"]}"/>')
    for name in ("W", "B", "accent"):
        body.append(f'<path id="{name}" d="{path_d(SHAPES[name])}" fill="{colors[name]}" '
                    f'fill-rule="nonzero"/>')
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024" '
            'width="1024" height="1024">\n<title>Watari Boot</title>\n  '
            + "\n  ".join(body) + "\n</svg>\n")


def tables():
    rows = ["| Shape | Vertex | Lattice (i, j) | Canvas px (x, y) |", "|---|---|---|---|"]
    for name, polys in SHAPES.items():
        for k, poly in enumerate(polys):
            part = name if k == 0 else f"{name} counter {k}"
            for n, (i, j) in enumerate(poly):
                x, y = px(i, j)
                rows.append(f"| {part} | {n + 1} | ({i}, {j}) | ({x:.3f}, {y:.3f}) |")
    return "\n".join(rows)


def main():
    check()
    here = os.path.dirname(os.path.abspath(__file__))
    col = {k: c for k, (c, _) in COLORS.items()}
    mono = {"plate": "#FFFFFF", "W": "#000000", "B": "#000000", "accent": "#000000",
            "background": "#000000"}
    outputs = {
        "watari-boot-icon.svg": svg(col),
        "watari-boot-icon-mono.svg": svg(mono),
        "watari-boot-mark.svg": svg(col, plate=False),
        "watari-boot-mark-mono.svg": svg(mono, plate=False),
    }
    for name, text in outputs.items():
        with open(os.path.join(here, name), "w") as f:
            f.write(text)
    geo = {
        "parameters": {"T": T, "a": a},
        "derived": {"bowl": BOWL, "offset": OFF, "height_rows": H, "width_modules": W,
                    "plate_modules": SQ, "corner_radius_modules": R},
        "canvas_px": {"size": CANVAS, "module": v, "row": ROW * v, "origin": [X0, Y0]},
        "colors": {k: {"hex": c, "note": n} for k, (c, n) in COLORS.items()},
        "shapes_lattice": SHAPES,
        "shapes_px": {k: [[list(px(*q)) for q in poly] for poly in polys]
                      for k, polys in SHAPES.items()},
    }
    with open(os.path.join(here, "geometry.json"), "w") as f:
        json.dump(geo, f, indent=1)
        f.write("\n")
    if "--tables" in sys.argv:
        print(tables())


if __name__ == "__main__":
    main()
