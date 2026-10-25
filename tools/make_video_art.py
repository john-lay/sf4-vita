"""Video artwork at 1280x720, in the same style as tools/make_original_art.py
(fan art: our own logo, ink splashes, speed lines on dark paper):

    thumbnail.png  video thumbnail: the logo and a Vita showing a fight
    title.png      opening card: "ported from Android to PS Vita"
    hardware.png   middle card: "play on original hardware"
    play.png       closing card: "play it now"

The Vita is web/vita.png with its screen replaced by one of our own
gameplay screenshots (screenshots in docs/ or screenshots/).

Usage: python tools/make_video_art.py <out_dir> [font_dir]
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFilter

import make_original_art as art
from make_original_art import DARK_RED, RED, WHITE

W, H = 1280, 720
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
VITA = os.path.join(ROOT, 'web', 'vita.png')
SHOT = os.path.join(ROOT, 'docs', '2026-10-22-195621.jpg')
VITA_SCREEN = (118, 122, 484, 330)  # the screen in web/vita.png

WHITE_TEXT = ((255, 255, 255), (206, 200, 190))
GREY = (170, 160, 150)


def background(seed, splashes):
    """Paper and speed lines, then (cx, cy, size, seed, streak, dark) splashes."""
    im = art.paper(W, H, seed)
    lines = art.speed_lines(W, H, seed * 10 + 1, 140, WHITE, 45)
    im.paste(lines, (0, 0), lines)
    for cx, cy, size, s, streak, dark in splashes:
        m = art.splash_mask(W, H, cx, cy, size, s, drops=int(size / 4), streak=streak)
        if dark:
            art.ink(im, m, DARK_RED)
        else:
            art.ink(im, m, DARK_RED, (8, 8))
            art.ink(im, m, RED)
    return im


def text_block(text, size, colors=WHITE_TEXT, skew=0.12):
    return art.fancy(text, art.ofl('Bangers-Regular.ttf', size), colors, max(3, size // 14), max(3, size // 16), skew=skew)


def put(im, block, cx, cy):
    im.paste(block, (int(cx - block.width / 2), int(cy - block.height / 2)), block)


def footer(im, text='an unofficial fan port  ·  not affiliated with Capcom'):
    note = art.font('segoeuib.ttf', 20)
    tw = note.getbbox(text)[2]
    ImageDraw.Draw(im).text(((W - tw) // 2, H - 46), text, font=note, fill=GREY)


def vita(scale, shot=SHOT):
    """The Vita with our screenshot in its screen, as RGBA, with a soft shadow."""
    v = Image.open(VITA).convert('RGBA')
    x0, y0, x1, y1 = VITA_SCREEN
    v.paste(Image.open(shot).convert('RGB').resize((x1 - x0, y1 - y0), Image.LANCZOS), (x0, y0))
    v = v.crop(v.getbbox())
    v = v.resize((int(v.width * scale), int(v.height * scale)), Image.LANCZOS)
    pad = 40
    out = Image.new('RGBA', (v.width + pad * 2, v.height + pad * 2), (0, 0, 0, 0))
    shadow = Image.new('RGBA', v.size, (0, 0, 0, 200))
    shadow.putalpha(v.getchannel('A').point(lambda a: a * 3 // 4))
    out.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(14)), (pad + 8, pad + 14))
    out.alpha_composite(v, (pad, pad))
    return out


def make_title(out):
    im = background(11, [
        (640, 330, 250, 111, (1, 0.2), False),
        (120, 600, 70, 112, (1, -0.4), True),
        (1180, 110, 60, 113, (-1, 0.5), True)])
    art.place(im, art.title_block(seed=2), W // 2, 300, 1040, 470)
    put(im, text_block('PORTED FROM ANDROID TO PS VITA', 66, skew=0.1), W // 2, 600)
    footer(im)
    art.save_palette(im, out, 'title.png')


def make_hardware(out):
    im = background(12, [
        (330, 360, 230, 121, (1, 0.25), False),
        (1180, 640, 80, 122, (-1, -0.3), True),
        (90, 90, 55, 123, (1, 0.5), True)])
    v = vita(1.15)
    im.paste(v, (W - v.width + 10, (H - v.height) // 2 + 10), v)
    put(im, text_block('PLAY ON', 108, art.GOLD), 280, 250)
    put(im, text_block('ORIGINAL', 108), 280, 372)
    put(im, text_block('HARDWARE', 108, art.BLUE), 280, 494)
    art.save_palette(im, out, 'hardware.png')


def make_play(out):
    im = background(13, [
        (640, 330, 270, 131, (1, -0.15), False),
        (150, 130, 60, 132, (1, 0.5), True),
        (1150, 600, 75, 133, (-1, -0.4), True)])
    put(im, text_block('PLAY IT NOW', 190, art.GOLD, skew=0.14), W // 2, 250)
    art.place(im, art.title_block(seed=2), W // 2, 475, 640, 220)
    put(im, text_block('LINK IN THE DESCRIPTION', 54), W // 2, 640)
    art.save_palette(im, out, 'play.png')


def make_thumbnail(out):
    im = background(14, [
        (400, 380, 300, 141, (1, 0.2), False),
        (1230, 60, 70, 142, (-1, 0.5), True),
        (60, 690, 60, 143, (1, -0.4), True)])
    v = vita(1.1)
    im.paste(v, (W - v.width + 10, H - v.height - 10), v)
    art.place(im, art.title_block(seed=2), 390, 190, 720, 310)
    put(im, text_block('ON PS VITA', 130, art.GOLD, skew=0.14), 310, 495)
    put(im, text_block('PORTED FROM ANDROID', 50), 330, 630)
    art.save_palette(im, out, 'thumbnail.png')


def main(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    make_thumbnail(out_dir)
    make_title(out_dir)
    make_hardware(out_dir)
    make_play(out_dir)


if __name__ == '__main__':
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    if len(sys.argv) == 3:
        art.FONT_DIR = sys.argv[2]
    main(sys.argv[1])
