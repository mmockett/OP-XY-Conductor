#!/usr/bin/env python3
"""Writes panel.svg.fragment: every node and text slot display.cpp places.

The header block is the SDK's two-row header (opxy/page_header.hpp: its bar
nodes and text slots 0..15, at their resting places). display.cpp sets every
other element's position each frame, so the artwork gives only shapes, fonts
and the palette. Run from the machine folder after changing a shape here or a
slot in display.cpp's Node and Text enums.
"""
from pathlib import Path

PARKED = "matrix(1 0 0 1 640 320)"
FONT = 'font-family="UniversNextPro-Light"'
BRIGHT, LIGHT, GREY = "#CBCBCB", "#A6A6A6", "#707070"

# The SDK header: eight 60 x 20 bar steps, black to light grey.
BAR = ["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]
# Header text slots 0..15: (x, y, size, fill), as page_header places them at rest.
HEADER_TEXTS = (
    [(5 + 120 * i, 10, 10, BRIGHT if i < 2 else "#000000") for i in range(4)] +
    [(65 + 120 * i, 17.4, 20, BRIGHT if i < 2 else "#000000") for i in range(4)] +
    [(5 + 120 * i, 33, 10, "#5A5A5A") for i in range(4)] +
    [(65 + 120 * i, 33, 13, GREY) for i in range(4)])
# Text slots 16..31 as display.cpp's Text enum: rows, the chord, the page
# labels (the shown page's bright, the others grey) and small notes.
PAGE_TEXTS = [(12, LIGHT)] * 8 + [(20, BRIGHT), (10, BRIGHT)] + [(10, GREY)] * 3 + [(10, GREY)] * 3

# (first node, count, width, height) rectangles, as display.cpp's Node enum.
RECTS = [
    (8, 1, 480, 18),     # highlight: the selected lane's row
    (9, 4, 68, 28),      # chord boxes
    (13, 1, 68, 2),      # under the sounding chord
    (14, 1, 1.5, 156),   # playhead
    (15, 4, 116, 2),     # page marks
    (19, 8, 260, 1),     # mute strikes
]
CIRCLES = (27, 15, 5)    # first node, count, radius: the wheel's degrees and the lanes' lights
CELLS = (42, 192, 24, 10)
# Sixteen greys, black to white.
PALETTE = ["#000000", "#0D0D0D", "#141414", "#1C1C1C", "#262626", "#333333", "#404040", "#4D4D4D",
           "#5C5C5C", "#6E6E6E", "#808080", "#999999", "#B3B3B3", "#CCCCCC", "#E6E6E6", "#FFFFFF"]


def rect(index, width, height, transform=PARKED, fill="#333333"):
    return (f'<path id="conductor_node_{index}" transform="{transform}" '
            f'd="M0,0 L{width},0 L{width},{height} L0,{height} Z" '
            f'fill="{fill}" stroke="{fill}" stroke-width="0.5"/>')


def main():
    lines = ['<g id="conductor_panel">',
             '<!-- SDK two-page header: keep this block before machine artwork below y=38. -->',
             '<path d="M0,0 L480,0 L480,38 L0,38 Z" fill="#000000"/>']
    lines += [rect(i, 60, 20, f"matrix(1 0 0 1 {60 * i} 0)", fill) for i, fill in enumerate(BAR)]
    for i, (x, y, size, fill) in enumerate(HEADER_TEXTS):
        lines.append(f'<text id="conductor_text_{i}" transform="matrix(1 0 0 1 {x} {y})" fill="{fill}" '
                     f'{FONT} font-size="{size}px">-</text>')
    for first, count, width, height in RECTS:
        lines += [rect(first + i, width, height) for i in range(count)]
    first, count, radius = CIRCLES
    lines += [f'<circle id="conductor_node_{first + i}" transform="{PARKED}" cx="0" cy="0" r="{radius}" '
              f'fill="#333333" stroke="#333333" stroke-width="0.5"/>' for i in range(count)]
    first, count, width, height = CELLS
    lines += [rect(first + i, width, height) for i in range(count)]
    for i, (size, fill) in enumerate(PAGE_TEXTS, start=16):
        lines.append(f'<text id="conductor_text_{i}" transform="{PARKED}" fill="{fill}" {FONT} font-size="{size}px">-</text>')
    for i, colour in enumerate(PALETTE):
        lines.append(f'<circle id="conductor_palette_{i}" cx="640" cy="0" r="1" '
                     f'fill="{colour}" stroke="{colour}" stroke-width="1"/>')
    lines.append('</g>')
    Path("panel.svg.fragment").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
