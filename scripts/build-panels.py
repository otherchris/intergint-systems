#!/usr/bin/env python3
"""Convert <text> in panels/*.svg to outlined <path>s and write res/*.svg.

VCV Rack's SVG renderer (nanosvg) ignores <text>, so panel sources keep
editable text and this script bakes it into glyph outlines.

Supported on <text>: x, y (baseline), font-size, text-anchor
(start|middle|end), fill, letter-spacing, and font-family mapped through
FONTS below. Units are whatever the SVG viewBox uses (mm for our panels).
"""
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parent.parent
FONTS = {"default": ROOT / "fonts" / "Nunito-Bold.ttf"}
SVG_NS = "http://www.w3.org/2000/svg"
ET.register_namespace("", SVG_NS)

_font_cache = {}


def load_font(family):
    path = FONTS.get(family or "default", FONTS["default"])
    if path not in _font_cache:
        _font_cache[path] = TTFont(path)
    return _font_cache[path]


def num(value, default=0.0):
    if value is None:
        return default
    return float(str(value).replace("px", "").replace("mm", ""))


def text_to_path(el):
    text = "".join(el.itertext()).strip()
    font = load_font(el.get("font-family"))
    glyphs = font.getGlyphSet()
    cmap = font.getBestCmap()
    upm = font["head"].unitsPerEm
    size = num(el.get("font-size"), 3.0)
    scale = size / upm
    spacing = num(el.get("letter-spacing"))

    names = [cmap.get(ord(ch), ".notdef") for ch in text]
    advances = [glyphs[n].width * scale + spacing for n in names]
    width = sum(advances) - (spacing if names else 0)

    x = num(el.get("x"))
    y = num(el.get("y"))
    anchor = el.get("text-anchor", "start")
    if anchor == "middle":
        x -= width / 2
    elif anchor == "end":
        x -= width

    pen = SVGPathPen(glyphs)
    for name, adv in zip(names, advances):
        # Font units are y-up; SVG is y-down.
        glyphs[name].draw(TransformPen(pen, (scale, 0, 0, -scale, x, y)))
        x += adv

    path = ET.Element(f"{{{SVG_NS}}}path")
    path.set("d", pen.getCommands())
    path.set("fill", el.get("fill", "#000000"))
    if el.get("id"):
        path.set("id", el.get("id"))
    return path


def convert(src, dst):
    tree = ET.parse(src)
    for parent in tree.iter():
        for i, child in enumerate(list(parent)):
            if child.tag == f"{{{SVG_NS}}}text":
                parent.remove(child)
                parent.insert(i, text_to_path(child))
    dst.parent.mkdir(parents=True, exist_ok=True)
    tree.write(dst, encoding="UTF-8", xml_declaration=True)


def main():
    sources = [Path(p).resolve() for p in sys.argv[1:]] or sorted((ROOT / "panels").glob("*.svg"))
    for src in sources:
        dst = ROOT / "res" / src.name
        convert(src, dst)
        print(f"{src.relative_to(ROOT)} -> {dst.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
