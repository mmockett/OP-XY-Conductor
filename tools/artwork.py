#!/usr/bin/env python3
"""Writes panel.svg.fragment: every node and text slot display.cpp places.

The header block is the SDK's two-row header (opxy/page_header.hpp: its bar
nodes and text slots 0..15, at their resting places). display.cpp sets every
other element's position each frame, so the artwork gives only shapes, fonts
and the palette, plus what never moves: the page labels and the track numbers.
Run from the machine folder after changing a shape here or a slot in
display.cpp's Node and Text enums.
"""
from pathlib import Path

PARKED = "matrix(1 0 0 1 640 320)"
FONT = 'font-family="UniversNextPro-Light"'
BRIGHT, GREY = "#CBCBCB", "#707070"

# The SDK header: eight 60 x 20 bar steps, black to light grey.
BAR = ["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]
# Header text slots 0..15: (x, y, size, fill), as page_header places them at rest.
HEADER_TEXTS = (
    [(5 + 120 * i, 10, 10, BRIGHT if i < 2 else "#000000") for i in range(4)] +
    [(65 + 120 * i, 17.4, 20, BRIGHT if i < 2 else "#000000") for i in range(4)] +
    [(5 + 120 * i, 33, 10, "#5A5A5A") for i in range(4)] +
    [(65 + 120 * i, 33, 13, GREY) for i in range(4)])
# Text slots 16..31 as display.cpp's Text enum: (size, fill).
PAGE_TEXTS = ([(10, "#A6A6A6")] * 8 + [(10, "#FFFFFF"), (15, "#FFFFFF"), (12, "#FFFFFF")] +
              [(11, "#7A7A7A")] * 3 + [(9, "#555555"), (10, "#FFFFFF")])

# Nodes 8.. as display.cpp's Node enum: (count, shape). A shape is
# ("rect", width, height) from its origin, ("centred", width, height) about it,
# or ("path", d).
WIRE = "M0,-0.4 L60,-0.4 L60,0.4 L0,0.4 Z"
NODES = [
    (1, ("rect", 112, 2)),          # page mark
    (8, ("rect", 26, 11)),          # track boxes
    (8, ("path", WIRE)),            # wires
    (1, ("rect", 2, 12)),           # accent
    (1, ("rect", 324, 14)),         # row band
    (1, ("rect", 81, 131)),         # bar band
    (1, ("rect", 20.25, 131)),      # step band
    (3, ("centred_x", 1, 131)),     # grid lines
    (1, ("centred_x", 1, 131)),     # playhead
    (1, ("path", "M-3,0 L3,0 L0,4 Z")),  # playhead's head
    (4, ("rect", 77, 1)),           # chord underlines
    (1, ("rect", 316, 2)),          # the sounding chord's mark: a phrase,
    (1, ("rect", 158, 2)),          # half a phrase,
    (1, ("rect", 77, 2)),           # a bar,
    (1, ("rect", 36.5, 2)),         # half a bar
    (4, ("centred_x", 1, 4)),       # chord ticks
    (5, ("rect", 10, 2)),           # meter
    (16, ("centred", 5, 5)),        # groove marks
    (16, ("centred", 1.4, 3)),      # off-beats
    (1, ("rect", 312, 1)),          # keyboard span
    (6, ("centred_y", 324, 1)),     # tone lines
    (128, ("centred", 12, 4)),      # cells
]
NODE_COUNT = 218

# Sixteen paints as display.cpp's Shade enum: (fill, stroke, stroke width).
SOLID = ["#000000", "#101010", "#141414", "#1C1C1C", "#262626", "#2E2E2E", "#3D3D3D", "#555555",
         "#7A7A7A", "#B8B8B8", "#FFFFFF"]
RINGS = ["#262626", "#3D3D3D", "#555555", "#7A7A7A", "#FFFFFF"]
PALETTE = [(c, c, 0.5) for c in SOLID] + [("#000000", c, 0.9) for c in RINGS]


def shape_d(shape):
    kind = shape[0]
    if kind == "path":
        return shape[1]
    w, h = shape[1], shape[2]
    x0 = -w / 2 if kind in ("centred", "centred_x") else 0
    y0 = -h / 2 if kind in ("centred", "centred_y") else 0
    f = lambda v: f"{v:g}"
    return f"M{f(x0)},{f(y0)} L{f(x0 + w)},{f(y0)} L{f(x0 + w)},{f(y0 + h)} L{f(x0)},{f(y0 + h)} Z"


def node(index, d, transform=PARKED, fill="#333333"):
    return (f'<path id="conductor_node_{index}" transform="{transform}" d="{d}" '
            f'fill="{fill}" stroke="{fill}" stroke-width="0.5"/>')


def main():
    lines = ['<g id="conductor_panel">',
             '<!-- SDK two-page header: keep this block before machine artwork below y=38. -->',
             '<path d="M0,0 L480,0 L480,38 L0,38 Z" fill="#000000"/>']
    lines += [node(i, "M0,0 L60,0 L60,20 L0,20 Z", f"matrix(1 0 0 1 {60 * i} 0)", fill) for i, fill in enumerate(BAR)]
    for i, (x, y, size, fill) in enumerate(HEADER_TEXTS):
        lines.append(f'<text id="conductor_text_{i}" transform="matrix(1 0 0 1 {x} {y})" fill="{fill}" '
                     f'{FONT} font-size="{size}px">-</text>')
    # What never moves: the page labels over M1..M4 and the track numbers.
    for p, name in enumerate(["conductor", "lanes", "lane", "route"]):
        lines.append(f'<path d="M{4 + 120 * p},203 L{116 + 120 * p},203 L{116 + 120 * p},204 L{4 + 120 * p},204 Z" fill="#3A3A3A"/>')
        lines.append(f'<text transform="matrix(1 0 0 1 {6 + 120 * p} 216)" fill="#6E6E6E" {FONT} font-size="10px">{name}</text>')
    index = 8
    for count, shape in NODES:
        lines += [node(index + i, shape_d(shape)) for i in range(count)]
        index += count
    assert index == NODE_COUNT, index
    # Over the track boxes, so a lit box keeps its number.
    for t in range(8):
        lines.append(f'<text transform="matrix(1 0 0 1 456.5 {82 + 14.4 * t + 3.2:g})" fill="#8A8A8A" {FONT} '
                     f'font-size="9px">{t + 1}</text>')
    for i, (size, fill) in enumerate(PAGE_TEXTS, start=16):
        lines.append(f'<text id="conductor_text_{i}" transform="{PARKED}" fill="{fill}" {FONT} font-size="{size}px">-</text>')
    for i, (fill, stroke, width) in enumerate(PALETTE):
        lines.append(f'<circle id="conductor_palette_{i}" cx="640" cy="0" r="1" '
                     f'fill="{fill}" stroke="{stroke}" stroke-width="{width}"/>')
    lines.append('</g>')
    Path("panel.svg.fragment").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
