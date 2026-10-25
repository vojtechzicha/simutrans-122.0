"""Draws the toolbar icons of the fork's landscaping tools into landscape_icons.png.

One row of 32x32 icons, in the order of landscape_icons.dat: level, raise, lower, ramp, smooth, gentle.
Drawn at 4x and scaled down; edges are cut hard against Simutrans' transparent colour, so no
light fringe shows on the toolbar. Run with Pillow: python make_icons.py
"""
import os
from PIL import Image, ImageDraw

S = 4                 # supersampling
W = 32 * S            # one icon
TRANSPARENT = (231, 255, 255)

GRASS = (106, 168, 79)
GRASS_LIGHT = (147, 196, 125)
EARTH = (153, 102, 51)
EARTH_DARK = (110, 72, 36)
OUTLINE = (40, 40, 40)
ARROW = (255, 255, 255)
LEVEL = (30, 80, 170)


def p(x, y):
    return (x * S, y * S)


def box(d, ox, top, bottom, w=13, h=6.5):
    """An isometric block: top face at height top, ground at bottom (y of the front corner)."""
    cx = ox
    # top diamond
    t = [p(cx, top - h), p(cx + w, top), p(cx, top + h), p(cx - w, top)]
    if bottom > top:
        left = [p(cx - w, top), p(cx, top + h), p(cx, bottom + h), p(cx - w, bottom)]
        right = [p(cx, top + h), p(cx + w, top), p(cx + w, bottom), p(cx, bottom + h)]
        d.polygon(left, fill=EARTH, outline=OUTLINE, width=S)
        d.polygon(right, fill=EARTH_DARK, outline=OUTLINE, width=S)
    d.polygon(t, fill=GRASS, outline=OUTLINE, width=S)


def arrow(d, x, y0, y1):
    """Vertical arrow from y0 to y1 (head at y1)."""
    up = y1 < y0
    head = 5 if up else -5
    d.rectangle([p(x - 1.6, min(y0, y1 + head)), p(x + 1.6, max(y0, y1 + head))], fill=ARROW, outline=OUTLINE, width=S)
    d.polygon([p(x - 5, y1 + head), p(x + 5, y1 + head), p(x, y1)], fill=ARROW, outline=OUTLINE, width=S)


def icon_level(d):
    box(d, 16, 17, 24)
    # a level: bar with end marks, bubble in the middle
    d.rectangle([p(4, 6), p(28, 9)], fill=LEVEL, outline=OUTLINE, width=S)
    d.rectangle([p(13, 6.5), p(19, 8.5)], fill=(255, 230, 80))
    d.line([p(4, 3), p(4, 12)], fill=OUTLINE, width=S * 2)
    d.line([p(28, 3), p(28, 12)], fill=OUTLINE, width=S * 2)


def icon_raise(d):
    box(d, 16, 16, 25)
    arrow(d, 16, 15, 1.5)


def icon_lower(d):
    box(d, 16, 22, 25)
    arrow(d, 16, 2, 16)


def icon_ramp(d):
    # a wedge on one tile: west and south corners on the ground, north and east raised
    w, s_, e, n = p(3, 22), p(16, 28.5), p(29, 22), p(16, 15.5)
    rise = 11
    e_top, n_top = p(29, 22 - rise), p(16, 15.5 - rise)
    d.polygon([s_, e, e_top], fill=EARTH_DARK, outline=OUTLINE, width=S)
    d.polygon([w, s_, e_top, n_top], fill=GRASS_LIGHT, outline=OUTLINE, width=S)
    # a track up the ramp
    d.line([p(9.5, 25.25), p(22.5, 9.75)], fill=(80, 80, 80), width=S * 2)


def icon_smooth(d):
    # rough ground on the left, smooth on the right, an arrow between
    rough = [p(1, 27), p(1, 18), p(4, 12), p(6, 19), p(9, 10), p(11, 20), p(13, 15), p(14, 27)]
    d.polygon(rough, fill=GRASS, outline=OUTLINE, width=S)
    smooth = [p(18, 27)]
    import math
    for i in range(0, 15):
        x = 18 + i
        smooth.append(p(x, 27 - 9 * math.sin(math.pi * i / 14)))
    smooth.append(p(32, 27))
    d.polygon(smooth, fill=GRASS_LIGHT, outline=OUTLINE, width=S)
    d.rectangle([p(8, 5.5), p(17, 9.5)], fill=ARROW, outline=OUTLINE, width=S)
    d.polygon([p(16, 2), p(24, 7.5), p(16, 13)], fill=ARROW, outline=OUTLINE, width=S)


def icon_gentle(d):
    # a steep bank on the left, the same rise as a gentle slope on the right
    d.polygon([p(1, 28), p(1, 12), p(5, 12), p(9, 28)], fill=EARTH, outline=OUTLINE, width=S)
    d.polygon([p(1, 12), p(5, 12), p(5, 10), p(1, 10)], fill=GRASS, outline=OUTLINE, width=S)
    d.polygon([p(13, 28), p(13, 12), p(16, 12), p(31, 28)], fill=GRASS_LIGHT, outline=OUTLINE, width=S)
    d.rectangle([p(6, 4.5), p(14, 8.5)], fill=ARROW, outline=OUTLINE, width=S)
    d.polygon([p(13, 1.5), p(20, 6.5), p(13, 11.5)], fill=ARROW, outline=OUTLINE, width=S)


ICONS = [icon_level, icon_raise, icon_lower, icon_ramp, icon_smooth, icon_gentle]


def main():
    out = Image.new('RGB', (32 * len(ICONS), 32), TRANSPARENT)
    for i, draw in enumerate(ICONS):
        big = Image.new('RGBA', (W, W), (0, 0, 0, 0))
        draw(ImageDraw.Draw(big))
        small = big.resize((32, 32), Image.LANCZOS)
        px = small.load()
        for y in range(32):
            for x in range(32):
                r, g, b, a = px[x, y]
                if a >= 128:
                    c = (r, g, b)
                    if c == TRANSPARENT:
                        c = (230, 254, 254)
                    out.putpixel((32 * i + x, y), c)
    here = os.path.dirname(os.path.abspath(__file__))
    out.save(os.path.join(here, 'landscape_icons.png'))


if __name__ == '__main__':
    main()
