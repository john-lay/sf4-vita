"""LiveArea artwork for public builds: fan art, no Capcom images or characters.

Draws the four images the VPK needs: a fan-art take on the game's logo (the
full name, "Street Fighter" in a flame-coloured brush script, a gold "IV",
"CHAMPION EDITION" in blue), drawn from scratch with free fonts, over red ink
on dark paper:

    icon0.png    128x128  home-screen bubble
    bg0.png      840x500  LiveArea background (the lettering stays top-left, away
                          from the Start gate)
    startup.png  280x158  LiveArea Start gate
    pic0.png     960x544  start-up image

Everything is generated from code (seeded, so the output is reproducible) and
saved as 8-bit palette PNGs like tools/make_livearea.py. The title fonts are
in tools/fonts/ (Kaushan Script and Bangers, SIL Open Font License); the small
print uses Segoe UI from the Windows fonts folder (pass another folder with
segoeuib.ttf as the second argument elsewhere).

Usage: python tools/make_original_art.py <out_dir> [font_dir]
"""
import math
import os
import random
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

RED = (200, 16, 46)
DARK_RED = (110, 8, 24)
INK = (16, 14, 14)
PAPER = (34, 30, 30)
WHITE = (240, 236, 228)

FONT_DIR = 'C:/Windows/Fonts'


def font(name, size):
    return ImageFont.truetype(os.path.join(FONT_DIR, name), size)


def noise_img(size, sigma, rnd):
    """Gaussian-ish grey noise around 128 from a seeded random.Random
    (Image.effect_noise ignores our seeds, so the art changed on every run)."""
    w, h = size
    a, b, c = (Image.frombytes('L', (w, h), rnd.randbytes(w * h)) for _ in range(3))
    mix = ImageChops.add(ImageChops.add(a, b, scale=2), c, scale=2)  # std ~45
    k = sigma / 45.0
    return mix.point(lambda v: max(0, min(255, int(128 + (v - 128) * k))))


def paper(w, h, seed):
    """Dark paper: noise and a few soft blotches, darker towards the edges."""
    rnd = random.Random(seed)
    im = Image.new('RGB', (w, h), PAPER)
    noise = noise_img((w, h), 22, rnd).convert('RGB')
    im = ImageChops.add(im, noise, scale=1.6, offset=-50)
    blot = Image.new('L', (w, h), 0)
    d = ImageDraw.Draw(blot)
    for _ in range(14):
        r = rnd.randint(min(w, h) // 8, min(w, h) // 3)
        x, y = rnd.randint(0, w), rnd.randint(0, h)
        d.ellipse((x - r, y - r, x + r, y + r), fill=rnd.randint(10, 30))
    blot = blot.filter(ImageFilter.GaussianBlur(min(w, h) / 10))
    im = Image.composite(Image.new('RGB', (w, h), (70, 60, 58)), im, blot)
    # vignette
    vig = Image.radial_gradient('L').resize((w, h))
    vig = vig.point(lambda v: int(v * 0.75))
    return Image.composite(Image.new('RGB', (w, h), (6, 5, 5)), im, vig)


def splash_mask(w, h, cx, cy, size, seed, drops=24, streak=None):
    """An ink splash: blurred, thresholded blobs with droplets and an optional
    streak direction (dx, dy) for a thrown-paint look."""
    rnd = random.Random(seed)
    m = Image.new('L', (w, h), 0)
    d = ImageDraw.Draw(m)
    for _ in range(40):
        a = rnd.uniform(0, 2 * math.pi)
        dist = abs(rnd.gauss(0, size * 0.35))
        r = rnd.uniform(size * 0.15, size * 0.45)
        x, y = cx + math.cos(a) * dist, cy + math.sin(a) * dist * 0.8
        d.ellipse((x - r, y - r, x + r, y + r), fill=255)
    # spikes: tapering wedges thrown out from the centre
    for _ in range(18):
        a = rnd.uniform(0, 2 * math.pi)
        if streak and rnd.random() < 0.6:
            a = math.atan2(streak[1], streak[0]) + rnd.gauss(0, 0.6)
        length = rnd.uniform(size * 0.7, size * 1.3)
        half = rnd.uniform(size * 0.04, size * 0.1)
        tip = (cx + math.cos(a) * length, cy + math.sin(a) * length)
        n = (-math.sin(a) * half, math.cos(a) * half)
        base = (cx + math.cos(a) * size * 0.3, cy + math.sin(a) * size * 0.3)
        d.polygon([(base[0] + n[0], base[1] + n[1]), tip, (base[0] - n[0], base[1] - n[1])], fill=255)
    for _ in range(drops):
        a = rnd.uniform(0, 2 * math.pi)
        if streak and rnd.random() < 0.7:
            a = math.atan2(streak[1], streak[0]) + rnd.gauss(0, 0.5)
        dist = rnd.uniform(size * 0.6, size * 1.7)
        r = rnd.uniform(size * 0.015, size * 0.07)
        x, y = cx + math.cos(a) * dist, cy + math.sin(a) * dist
        d.ellipse((x - r, y - r, x + r, y + r), fill=255)
        if rnd.random() < 0.5:  # a trail back towards the splash
            d.line((x, y, x - math.cos(a) * r * 6, y - math.sin(a) * r * 6), fill=255, width=max(1, int(r)))
    # fine spatter
    for _ in range(drops * 4):
        a = rnd.uniform(0, 2 * math.pi)
        dist = rnd.uniform(size * 0.4, size * 2.0)
        r = rnd.uniform(0.6, max(1.2, size * 0.012))
        x, y = cx + math.cos(a) * dist, cy + math.sin(a) * dist
        d.ellipse((x - r, y - r, x + r, y + r), fill=255)
    m = m.filter(ImageFilter.GaussianBlur(max(1.0, size * 0.035)))
    # ragged edges: noise decides the threshold near the boundary
    noise = noise_img((w, h), 70, rnd).filter(ImageFilter.GaussianBlur(max(0.8, size * 0.012)))
    m = ImageChops.add(m, noise, scale=1.0, offset=-128)
    return m.point(lambda v: 255 if v > 100 else 0).filter(ImageFilter.GaussianBlur(0.7))


def speed_lines(w, h, seed, count, color, alpha, angle=-0.18):
    rnd = random.Random(seed)
    layer = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    for _ in range(count):
        y = rnd.uniform(-h * 0.2, h * 1.2)
        x0 = rnd.uniform(-w * 0.3, w * 0.6)
        length = rnd.uniform(w * 0.2, w * 0.8)
        width = rnd.choice((1, 1, 2, 3))
        d.line((x0, y, x0 + length, y + length * angle), fill=color + (rnd.randint(alpha // 3, alpha),), width=width)
    return layer.filter(ImageFilter.GaussianBlur(0.6))


FONTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fonts')  # OFL fonts, see fonts/OFL-*.txt
FLAME = ((255, 244, 140), (246, 86, 12))
GOLD = ((255, 246, 170), (226, 150, 14))
BLUE = ((170, 236, 255), (18, 84, 196))


def ofl(name, size):
    return ImageFont.truetype(os.path.join(FONTS, name), size)


def gradient(w, h, top, bottom):
    g = Image.linear_gradient('L').resize((w, h))
    return Image.composite(Image.new('RGB', (w, h), bottom), Image.new('RGB', (w, h), top), g)


def fancy(text, fnt, colors, outline_w, shadow, skew=0.18, brush_seed=None, highlight=True):
    """Fan-logo lettering: a top-to-bottom colour gradient, a thick black
    outline, a dark drop shadow and an optional brush-streak texture."""
    bbox = fnt.getbbox(text)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    pad = outline_w * 3 + 4
    w, h = tw + pad * 2 + int(th * abs(skew)) + 2, th + pad * 2
    mask = Image.new('L', (w, h), 0)
    ImageDraw.Draw(mask).text((pad - bbox[0], pad - bbox[1]), text, font=fnt, fill=255)
    if skew:
        mask = mask.transform((w, h), Image.AFFINE, (1, skew, -skew * h * 0.5, 0, 1, 0), Image.BICUBIC)
    fill = gradient(w, h, *colors)
    if brush_seed is not None:
        # dry-brush streaks: stretched noise darkens and nicks the fill
        rnd = random.Random(brush_seed)
        streak = noise_img((w // 6 + 1, h), 90, rnd).resize((w, h), Image.BICUBIC)
        streak = streak.filter(ImageFilter.GaussianBlur((max(2, w // 60), 0.6)))
        fill = ImageChops.multiply(fill, Image.merge('RGB', [streak.point(lambda v: 205 + v // 5)] * 3))
        nicks = streak.point(lambda v: 0 if v < 70 + rnd.randint(-5, 5) else 255)
        mask = ImageChops.multiply(mask, nicks.filter(ImageFilter.GaussianBlur(0.6)))
    if highlight:  # a thin light line along the top edges
        edge = ImageChops.subtract(mask, ImageChops.offset(mask, 0, max(1, outline_w // 2)))
        fill.paste(Image.new('RGB', (w, h), (255, 255, 235)), (0, 0), edge.point(lambda v: v // 2))
    grown = mask.filter(ImageFilter.MaxFilter(outline_w * 2 + 1))
    out = Image.new('RGBA', (w + shadow + 2, h + shadow + 2), (0, 0, 0, 0))
    out.paste(Image.new('RGBA', (w, h), (40, 4, 8, 210)), (shadow, shadow), grown)
    out.paste(Image.new('RGBA', (w, h), (8, 6, 6, 255)), (0, 0), grown)
    out.paste(fill.convert('RGBA'), (0, 0), mask)
    return out.crop(out.getbbox())


def title_block(size=300, seed=1):
    """A fan-art take on the game's logo, drawn from scratch: "Street" and
    "Fighter" stacked in a flame-coloured brush script, a big gold "IV"
    overlapping them on the right, "CHAMPION EDITION" in blue underneath.
    size is the height of the IV's font; scale the result with place()."""
    script = ofl('KaushanScript-Regular.ttf', int(size * 0.5))
    street = fancy('Street', script, FLAME, max(3, size // 40), size // 30, skew=-0.05, brush_seed=seed)
    fighter = fancy('Fighter', script, FLAME, max(3, size // 40), size // 30, skew=-0.05, brush_seed=seed + 1)
    iv = fancy('IV', ofl('Bangers-Regular.ttf', size), GOLD, max(4, size // 22), size // 22, skew=0.12)
    ce = fancy('CHAMPION EDITION', ofl('Bangers-Regular.ttf', int(size * 0.3)), BLUE, max(3, size // 45), size // 40, skew=0.12)
    script_w = max(street.width, fighter.width + int(size * 0.25))
    w = script_w + int(iv.width * 0.95)
    h = max(street.height + fighter.height - int(size * 0.12), iv.height) + ce.height
    block = Image.new('RGBA', (max(w, ce.width), h), (0, 0, 0, 0))
    block.alpha_composite(street, (0, 0))
    block.alpha_composite(fighter, (int(size * 0.25), street.height - int(size * 0.14)))
    block.alpha_composite(iv, (script_w - int(iv.width * 0.05), max(0, int(size * 0.02))))
    ce_y = max(street.height + fighter.height - int(size * 0.14), iv.height) - int(size * 0.06)
    block.alpha_composite(ce, ((block.width - ce.width) // 2, ce_y))
    return block.crop(block.getbbox())


def place(im, block, cx, cy, max_w, max_h):
    """Scales block to fit max_w x max_h and pastes it centred on (cx, cy)."""
    k = min(max_w / block.width, max_h / block.height)
    b = block.resize((max(1, int(block.width * k)), max(1, int(block.height * k))), Image.LANCZOS)
    im.paste(b, (int(cx - b.width / 2), int(cy - b.height / 2)), b)


def ink(base, mask, color, offset=(0, 0)):
    base.paste(Image.new('RGB', base.size, color), offset, mask)


def save_palette(im, out_dir, name):
    im = im.convert('RGB').quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
    im.save(os.path.join(out_dir, name), optimize=True)
    print(f'{name}: {im.size[0]}x{im.size[1]}')


def make_icon(out):
    w = h = 128
    im = paper(w, h, 1)
    splash = splash_mask(w, h, 64, 64, 34, 11, drops=10)
    ink(im, splash, DARK_RED, (3, 3))
    ink(im, splash, RED)
    place(im, title_block(seed=5), 64, 64, 100, 64)  # inside the round bubble
    save_palette(im, out, 'icon0.png')


# Where the LiveArea puts the Start gate on bg0 (measured on the Vita,
# 2026-10-22, about +-5 px), and the system buttons along the top middle.
GATE = (280, 139, 560, 297)
TOP_BUTTONS = (270, 0, 570, 62)


def make_bg(out):
    """No title here: the gate in the middle shows it. Ink splashes frame the
    gate from both sides, with speed lines running through."""
    w, h = 840, 500
    im = paper(w, h, 2)
    lines = speed_lines(w, h, 21, 80, WHITE, 40)
    im.paste(lines, (0, 0), lines)
    left = splash_mask(w, h, 130, 330, 135, 22, drops=40, streak=(1, -0.3))
    ink(im, left, DARK_RED, (6, 6))
    ink(im, left, RED)
    right = splash_mask(w, h, 720, 150, 110, 24, drops=34, streak=(-1, 0.35))
    ink(im, right, DARK_RED, (5, 5))
    ink(im, right, RED)
    ink(im, splash_mask(w, h, 690, 440, 45, 23, drops=14, streak=(-1, -0.4)), DARK_RED)
    ink(im, splash_mask(w, h, 90, 70, 35, 25, drops=10, streak=(1, 0.5)), DARK_RED)
    save_palette(im, out, 'bg0.png')


def make_startup(out):
    w, h = 280, 158
    im = paper(w, h, 3)
    stroke = splash_mask(w, h, 140, 80, 70, 31, drops=10, streak=(1, -0.1))
    ink(im, stroke, DARK_RED, (4, 4))
    ink(im, stroke, RED)
    # the LiveArea draws its "Start" pill over the bottom ~38 px (x ~75-205)
    place(im, title_block(seed=3), 140, 60, 250, 112)
    save_palette(im, out, 'startup.png')


def make_pic(out):
    w, h = 960, 544
    im = paper(w, h, 4)
    lines = speed_lines(w, h, 41, 110, WHITE, 45)
    im.paste(lines, (0, 0), lines)
    big = splash_mask(w, h, 470, 250, 190, 42, drops=50, streak=(1, 0.2))
    ink(im, big, DARK_RED, (8, 8))
    ink(im, big, RED)
    place(im, title_block(seed=2), w // 2, 250, 760, 420)
    note = font('segoeuib.ttf', 18)
    text = 'an unofficial fan port  \u00b7  not affiliated with Capcom'
    tw = note.getbbox(text)[2]
    ImageDraw.Draw(im).text(((w - tw) // 2, h - 40), text, font=note, fill=(170, 160, 150))
    save_palette(im, out, 'pic0.png')


def main(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    make_icon(out_dir)
    make_bg(out_dir)
    make_startup(out_dir)
    make_pic(out_dir)


if __name__ == '__main__':
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    if len(sys.argv) == 3:
        FONT_DIR = sys.argv[2]
    main(sys.argv[1])
