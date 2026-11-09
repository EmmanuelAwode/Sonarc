"""Draws the Sonarc logo: a musical "S" whose tail ends in a note head.

    python tools/make_logo.py            # writes resources/logo.png and resources/logo-small.png

The S is one continuous stroke, shaded along its length through the Sonarc
spectrum colours (cyan -> blue -> violet), with a soft glow, on a dark rounded
tile. logo-small.png uses a thicker stroke and no fine detail so it stays
legible at 16-32 px (taskbar, title bar). Needs Pillow and numpy.
"""
import math
import pathlib

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "resources"

SS = 4            # supersampling factor
SIZE = 1024       # output size

# Sonarc theme colours.
CYAN = (34, 211, 238)
BLUE = (47, 123, 255)
VIOLET = (107, 75, 255)
PINK = (190, 70, 255)


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def gradient(t):
    stops = [(0.0, CYAN), (0.45, BLUE), (0.8, VIOLET), (1.0, PINK)]
    for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
        if t <= t1:
            return lerp(c0, c1, (t - t0) / (t1 - t0))
    return stops[-1][1]


def s_path(cx, cy, rx, ry, steps=1600):
    """Centre line of the S: an upper bowl drawn anticlockwise from the
    top-right, then a lower bowl drawn clockwise, ending at the bottom-left."""
    upper = (cx, cy - ry)
    lower = (cx, cy + ry)
    points = []
    # Upper bowl: from -35 deg (upper right) round the top and left to 90 deg (bottom of bowl).
    a0, a1 = math.radians(-35), math.radians(-270)
    for i in range(steps // 2):
        a = a0 + (a1 - a0) * i / (steps // 2)
        points.append((upper[0] + rx * math.cos(a), upper[1] + ry * math.sin(a)))
    # Lower bowl: from -90 deg (top of bowl) round the right and bottom to 145 deg (lower left).
    a0, a1 = math.radians(-90), math.radians(145)
    for i in range(steps // 2 + 1):
        a = a0 + (a1 - a0) * i / (steps // 2)
        points.append((lower[0] + rx * math.cos(a), lower[1] + ry * math.sin(a)))
    return points


def rounded_tile(size, radius):
    """Dark tile with a vertical gradient and a faint top highlight."""
    top, bottom = np.array([28, 33, 44], float), np.array([8, 10, 14], float)
    y = np.linspace(0, 1, size)[:, None, None]
    rgb = (top + (bottom - top) * y) * np.ones((1, size, 1))
    tile = Image.fromarray(rgb.astype(np.uint8), "RGB").convert("RGBA")

    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, size - 1, size - 1), radius, fill=255)
    tile.putalpha(mask)

    # 1-px-ish inner rim, brighter at the top.
    rim = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    w = max(2, size // 256)
    ImageDraw.Draw(rim).rounded_rectangle((w, w, size - 1 - w, size - 1 - w), radius - w,
                                          outline=(255, 255, 255, 34), width=w)
    fade = Image.fromarray((np.linspace(255, 40, size)[:, None] * np.ones((1, size))).astype(np.uint8), "L")
    rim.putalpha(Image.fromarray((np.array(rim.getchannel("A"), float) * np.array(fade, float) / 255).astype(np.uint8)))
    return Image.alpha_composite(tile, rim)


def draw_logo(size, small=False):
    n = size * SS
    canvas = rounded_tile(n, int(n * 0.225))

    stroke = int(n * (0.125 if small else 0.105))
    cx, cy = n * 0.5, n * 0.5
    rx, ry = n * (0.19 if small else 0.185), n * 0.162
    points = s_path(cx, cy - n * 0.01, rx, ry)

    # Note head at the tail: a tilted oval, like engraved notation, set off from
    # the stroke by a thin gap in the tile colour so it reads as a note head.
    tail = points[-1]
    head_w, head_h = n * (0.215 if small else 0.2), n * (0.15 if small else 0.138)
    hx, hy = tail[0] - n * 0.045, tail[1] + n * 0.02
    gap = n * (0.018 if small else 0.014)

    layer = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    r = stroke / 2
    for i, (x, y) in enumerate(points):
        c = gradient(i / (len(points) - 1))
        d.ellipse((x - r, y - r, x + r, y + r), fill=c + (255,))

    def oval(grow, fill):
        im = Image.new("RGBA", (n, n), (0, 0, 0, 0))
        ImageDraw.Draw(im).ellipse((hx - head_w / 2 - grow, hy - head_h / 2 - grow,
                                    hx + head_w / 2 + grow, hy + head_h / 2 + grow), fill=fill)
        return im.rotate(24, center=(hx, hy), resample=Image.BICUBIC)

    # Cut the gap out of the stroke, then lay the head in it.
    cut = np.array(oval(gap, (0, 0, 0, 255)).getchannel("A"), float) / 255
    alpha = np.array(layer.getchannel("A"), float) * (1 - cut)
    layer.putalpha(Image.fromarray(alpha.astype(np.uint8)))
    layer = Image.alpha_composite(layer, oval(0, PINK + (255,)))

    # Soft glow underneath, in the stroke's own colours.
    glow = layer.filter(ImageFilter.GaussianBlur(n * (0.02 if small else 0.035)))
    glow.putalpha(Image.fromarray((np.array(glow.getchannel("A"), float) * (0.55 if small else 0.75)).astype(np.uint8)))

    # Subtle sheen: lighten the upper half of the stroke a touch.
    sheen = Image.fromarray((np.linspace(60, 0, n)[:, None] * np.ones((1, n))).astype(np.uint8), "L")
    white = Image.new("RGBA", (n, n), (255, 255, 255, 0))
    white.putalpha(Image.fromarray((np.array(sheen, float) * np.array(layer.getchannel("A"), float) / 255).astype(np.uint8)))

    tile_alpha = canvas.getchannel("A")
    art = Image.alpha_composite(Image.alpha_composite(glow, layer), white)
    art.putalpha(Image.fromarray(np.minimum(np.array(art.getchannel("A")), np.array(tile_alpha))))
    out = Image.alpha_composite(canvas, art)
    return out.resize((size, size), Image.LANCZOS)


if __name__ == "__main__":
    OUT.mkdir(exist_ok=True)
    draw_logo(SIZE).save(OUT / "logo.png")
    draw_logo(256, small=True).save(OUT / "logo-small.png")
    print("wrote", OUT / "logo.png", "and", OUT / "logo-small.png")
