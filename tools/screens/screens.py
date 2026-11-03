#!/usr/bin/env python3
"""Renders Conductor's four pages, and M1 frozen and stopped, from its own control and display code, at the
OP-XY's 480 x 220 (captured at 2x) in the device's font, into docs/.

    XY_FONT=/path/to/UniversNextPro-Light.ttf python3 tools/screens/screens.py --sdk ../open-xy

XY_FONT is the OP-XY's screen font, never committed here. Needs a C++ compiler
and Google Chrome. Writes docs/m1.png..m4.png, docs/m1-frozen.png, docs/m1-stopped.png,
docs/m3-drum.png (M3 on the drum lane), docs/m2-shift.png and docs/m2-shift-row2.png (Shift on
each header row), and docs/screens.png (M1..M4, 2 x 2).
"""
import argparse
import os
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
# (file, page, selected lane, states): M2..M4 select the arp.
PAGES = [("m1", 0, 4, []), ("m2", 1, 4, []), ("m3", 2, 4, []), ("m4", 3, 4, []),
         ("m1-frozen", 0, 4, ["frozen"]), ("m1-stopped", 0, 4, ["stopped"]), ("m3-drum", 2, 0, []),
         ("m2-shift", 1, 4, ["shift"]), ("m2-shift-row2", 1, 4, ["shift", "row2"])]


def frame_svg(art, dump):
    palette = {int(i): (f, s, w) for i, f, s, w in re.findall(
        r'id="conductor_palette_(\d+)"[^>]*fill="(#[0-9A-Fa-f]{6})" stroke="(#[0-9A-Fa-f]{6})" stroke-width="([0-9.]+)"', art)}
    nodes, texts = {}, {}
    for line in dump.splitlines():
        if line.startswith("n "):
            p = line.split(" ")
            nodes[int(p[1])] = tuple(int(v) for v in p[2:9])
        else:
            p = line.split(" ", 5)
            texts[int(p[1])] = (int(p[2]), int(p[3]), int(p[4]), p[5] if len(p) > 5 else "")

    def node(m):
        el = m.group(0)
        x, y, scale, cos, sin, shade, paint = nodes[int(re.search(r"conductor_node_(\d+)", el).group(1))]
        k = scale / 1024 / 1024
        el = re.sub(r'transform="[^"]*"', f'transform="matrix({k * cos} {k * sin} {-k * sin} {k * cos} {x / 1024} {y / 1024})"', el)
        if paint == 2:
            fill, stroke, width = palette[shade]
            el = re.sub(r'fill="#[0-9A-Fa-f]{6}"', f'fill="{fill}"', el)
            el = re.sub(r'stroke="#[0-9A-Fa-f]{6}"', f'stroke="{stroke}"', el)
            el = re.sub(r'stroke-width="[0-9.]+"', f'stroke-width="{width}"', el)
        return el

    def text(m):
        x, y, scale, s = texts[int(m.group(1))]
        k = scale / 1024
        head = re.sub(r'transform="[^"]*"', f'transform="matrix({k} 0 0 {k} {x / 1024} {y / 1024})"', m.group(0).split(">")[0])
        s = s.replace("&", "&amp;").replace("<", "&lt;")
        return f'{head} xml:space="preserve">{s}</text>'

    art = re.sub(r'<(path|circle) id="conductor_node_\d+"[^>]*/>', node, art)
    art = re.sub(r'<text id="conductor_text_(\d+)"[^>]*>[^<]*</text>', text, art)
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="480" height="220" viewBox="0 0 480 220"><rect width="480" height="220" fill="#000"/>{art}</svg>'


def screenshot(html_path, png_path, width, height):
    subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--allow-file-access-from-files", "--hide-scrollbars",
                    "--force-device-scale-factor=2", f"--window-size={width},{height}",
                    f"--screenshot={png_path}", html_path.as_uri()], check=True, capture_output=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", default=str(ROOT.parent / "open-xy"), help="Open-XY SDK checkout")
    parser.add_argument("--cxx", default="c++")
    args = parser.parse_args()
    font = pathlib.Path(os.environ.get("XY_FONT", ""))
    if not font.is_file():
        sys.exit("screens.py: set XY_FONT to the path of UniversNextPro-Light.ttf (the OP-XY's screen font)")
    style = (f'@font-face {{ font-family: "UniversNextPro-Light"; src: url("{font.resolve().as_uri()}"); }}'
             "html, body { margin: 0; background: #000; }")
    art = (ROOT / "panel.svg.fragment").read_text()
    docs = ROOT / "docs"
    docs.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        binary = tmp / "render"
        subprocess.run([args.cxx, "-std=c++11", "-O1", "-I", str(pathlib.Path(args.sdk) / "sdk/include"), "-I", str(ROOT),
                        str(ROOT / "tools/screens/render.cpp"), "-o", str(binary)], check=True)
        for name, page, lane, states in PAGES:
            dump = subprocess.run([str(binary), str(page), str(lane)] + states, check=True, capture_output=True,
                                  text=True).stdout
            html = tmp / f"{name}.html"
            html.write_text(f'<!doctype html><meta charset="utf-8"><style>{style}</style>{frame_svg(art, dump)}')
            screenshot(html, docs / f"{name}.png", 480, 220)
        images = "".join(f'<img src="{(docs / f"{name}.png").as_uri()}">' for name, _, _, _ in PAGES[:4])
        grid = tmp / "grid.html"
        grid.write_text("<!doctype html><style>html,body{margin:0;background:#1a1a1a}"
                        "div{display:grid;grid-template-columns:480px 480px;gap:8px;padding:8px}"
                        f"img{{width:480px;height:220px;display:block}}</style><div>{images}</div>")
        screenshot(grid, docs / "screens.png", 976, 464)


if __name__ == "__main__":
    main()
