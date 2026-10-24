"""itch.io page artwork, in the same style as tools/make_original_art.py (fan
art, no Capcom images or characters):

    top.png   960x192  page banner: the fan logo over a red ink splash
    side.png  384x384  page background; tiles seamlessly in both directions

The tile is made seamless by drawing everything on a 3x3 canvas and folding
the nine pieces onto one tile, so ink that runs off one edge comes back on the
opposite one.

Usage: python tools/make_itch_art.py <out_dir> [font_dir]
"""
import os
import random
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

import make_original_art as art
from make_original_art import DARK_RED, PAPER, RED, WHITE


def fold(big, w, h, combine):
    """Folds a 3w x 3h image onto one w x h tile."""
    tile = big.crop((0, 0, w, h))
    for y in range(3):
        for x in range(3):
            if x or y:
                tile = combine(tile, big.crop((x * w, y * h, x * w + w, y * h + h)))
    return tile


def fold_mask(big, w, h):
    return fold(big, w, h, ImageChops.lighter)


def fold_layer(big, w, h):
    return fold(big, w, h, Image.alpha_composite)


def paper_tile(w, h, seed):
    """art.paper() without the vignette, with wrapped blotches."""
    rnd = random.Random(seed)
    im = Image.new('RGB', (w, h), PAPER)
    noise = art.noise_img((w, h), 22, rnd).convert('RGB')  # per-pixel, so it tiles as is
    im = ImageChops.add(im, noise, scale=1.6, offset=-50)
    blot = Image.new('L', (w * 3, h * 3), 0)
    d = ImageDraw.Draw(blot)
    for _ in range(14):
        r = rnd.randint(min(w, h) // 8, min(w, h) // 3)
        x, y = w + rnd.randint(0, w), h + rnd.randint(0, h)
        d.ellipse((x - r, y - r, x + r, y + r), fill=rnd.randint(10, 30))
    blot = fold_mask(blot.filter(ImageFilter.GaussianBlur(min(w, h) / 10)), w, h)
    return Image.composite(Image.new('RGB', (w, h), (70, 60, 58)), im, blot)


def splash_tile(w, h, cx, cy, size, seed, **kw):
    return fold_mask(art.splash_mask(w * 3, h * 3, w + cx, h + cy, size, seed, **kw), w, h)


def lines_tile(w, h, seed, count, color, alpha):
    rnd = random.Random(seed)
    layer = Image.new('RGBA', (w * 3, h * 3), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    for _ in range(count):
        x0, y = w + rnd.uniform(0, w), h + rnd.uniform(0, h)
        length = rnd.uniform(w * 0.2, w * 0.8)
        width = rnd.choice((1, 1, 2, 3))
        d.line((x0, y, x0 + length, y - length * 0.18), fill=color + (rnd.randint(alpha // 3, alpha),), width=width)
    return fold_layer(layer.filter(ImageFilter.GaussianBlur(0.6)), w, h)


SIDE = 384
SHADE = ((10, 8, 8), 0.25)  # darkens the background so page text stays readable


def side_base():
    """The background tile's paper and speed lines, without the splashes."""
    im = paper_tile(SIDE, SIDE, 7)
    lines = lines_tile(SIDE, SIDE, 71, 50, WHITE, 35)
    im.paste(lines, (0, 0), lines)
    return im


def shade(im):
    return Image.blend(im, Image.new('RGB', im.size, SHADE[0]), SHADE[1])


def make_top(out):
    """Itch puts the banner on the page's content column, so it can't be
    transparent. It is drawn on the background tile's own paper instead, with
    the splash kept to the middle, so its edges melt into the background."""
    w, h = 960, 192
    base = shade(side_base())
    im = Image.new('RGB', (w, h))
    for x in range(0, w, SIDE):
        im.paste(base, (x, 0))
    big = art.splash_mask(w, h, 480, 96, 95, 62, drops=40, streak=(1, 0.15))
    art.ink(im, big, DARK_RED, (6, 6))
    art.ink(im, big, RED)
    art.place(im, art.title_block(seed=2), w // 2, h // 2, 520, 172)
    note = art.font('segoeuib.ttf', 15)
    text = 'unofficial PS Vita port'
    tw = note.getbbox(text)[2]
    ImageDraw.Draw(im).text((w - tw - 16, h - 28), text, font=note, fill=(170, 160, 150))
    art.save_palette(im, out, 'top.png')


def make_side(out):
    """Background tile: paper, speed lines and red splashes, a bit darker
    than the LiveArea art so the page text in front stays readable."""
    w = h = SIDE
    im = side_base()
    # small, dim splashes (dark red, half see-through) so they stay in the background
    for cx, cy, size, seed, streak, color in (
            (90, 110, 34, 72, (1, -0.3), (120, 14, 30)),
            (290, 280, 28, 73, (-1, 0.35), (120, 14, 30)),
            (310, 70, 16, 74, (-1, 0.5), (90, 10, 24)),
            (60, 300, 18, 75, (1, -0.4), (90, 10, 24)),
            (190, 200, 11, 76, (1, 0.2), (90, 10, 24))):
        m = splash_tile(w, h, cx, cy, size, seed, drops=12, streak=streak)
        art.ink(im, m.point(lambda v: v * 3 // 5), color)
    art.save_palette(shade(im), out, 'side.png')


def main(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    make_top(out_dir)
    make_side(out_dir)


if __name__ == '__main__':
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    if len(sys.argv) == 3:
        art.FONT_DIR = sys.argv[2]
    main(sys.argv[1])
