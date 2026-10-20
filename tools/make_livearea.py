"""Make the Vita LiveArea images from your own SF4 CE 1.08.03 .apkm.

Usage: python tools/make_livearea.py <game.apkm> [out_dir]    (default: game/livearea)

Writes icon0.png (bubble), pic0.png (start-up splash), bg0.png (LiveArea
background) and startup.png (Start gate), at the sizes the Vita requires and
as 8-bit palette PNGs. Build the loader with -DLIVEAREA_DIR=<out_dir> to use
them. They are built from Capcom's art in base.apk (the launcher icon, the
title screen's background and logo, the copyright line), so they are for your
own builds only: keep them out of git (game/ is ignored) and public VPKs.

Needs Pillow (pip install pillow).
"""
import io
import os
import struct
import sys
import zipfile
import zlib

from PIL import Image, ImageFilter

ICON_RES = 'res/mipmap-xxxhdpi-v4/'  # adaptive icon layers, 432x432


def decode_obm(data):
    """RGBA/RGB .obm, optionally zlib-wrapped (format: RESEARCH.md §4)."""
    if data[:2] != b'OI':
        raise ValueError('not an .obm file')
    fmt, bpp = data[2], data[3]
    w, h = struct.unpack_from('<HH', data, 4)
    if fmt == 0xFF:
        return decode_obm(zlib.decompress(data[8:]))
    if fmt == 0x01 and bpp == 32:
        return Image.frombytes('RGBA', (w, h), data[8:8 + w * h * 4])
    if fmt == 0x00 and bpp == 24:
        return Image.frombytes('RGB', (w, h), data[8:8 + w * h * 3])
    raise ValueError(f'.obm format {fmt:#x}/{bpp} bpp not supported')


def cover(im, size, focus=(0.5, 0.5)):
    """Scale im to cover size, cropping around focus (fractions of the overflow)."""
    w, h = size
    s = max(w / im.width, h / im.height)
    im = im.resize((round(im.width * s), round(im.height * s)), Image.LANCZOS)
    x = round((im.width - w) * focus[0])
    y = round((im.height - h) * focus[1])
    return im.crop((x, y, x + w, y + h)).convert('RGBA')


def fit(im, width):
    return im.resize((width, round(im.height * width / im.width)), Image.LANCZOS)


def paste(base, im, x, y, shadow=True):
    if shadow:
        sh = Image.new('RGBA', im.size, (0, 0, 0, 0))
        sh.putalpha(im.getchannel('A').filter(ImageFilter.GaussianBlur(6)).point(lambda v: v * 0.55))
        base.alpha_composite(sh, (x + 3, y + 4))
    base.alpha_composite(im, (x, y))


def tint(im, rgba):
    return Image.alpha_composite(im, Image.new('RGBA', im.size, rgba))


def save_palette(im, out_dir, name):
    # 8-bit palette PNGs: full-colour ones can make the VPK fail to install.
    im = im.convert('RGB').quantize(256, method=Image.Quantize.MEDIANCUT,
                                    dither=Image.Dither.FLOYDSTEINBERG)
    im.save(os.path.join(out_dir, name), optimize=True)


def main(apkm, out_dir):
    base = zipfile.ZipFile(io.BytesIO(zipfile.ZipFile(apkm).read('base.apk')))
    load = lambda name: Image.open(io.BytesIO(base.read(name))).convert('RGBA')

    atlas = decode_obm(base.read('assets/title_bg.obm')).convert('RGBA')  # 1024x1024
    ink = atlas.crop((0, 0, 1024, 640))       # the title screen's background
    logo = atlas.crop((9, 644, 681, 1022))    # "Street Fighter IV Champion Edition"
    copyright_line = load('assets/CP.png')
    icon_bg = load(ICON_RES + 'ic_launcher_background.png')
    icon_fg = load(ICON_RES + 'ic_launcher_foreground.png')
    os.makedirs(out_dir, exist_ok=True)

    # pic0 960x544, the start-up splash: the title screen without "tap screen"
    pic0 = cover(ink, (960, 544), (0.5, 0.45))
    lg = fit(logo, 600)
    paste(pic0, lg, (960 - lg.width) // 2, 70)
    cp = fit(copyright_line, 360)
    paste(pic0, cp, (960 - cp.width) // 2, 544 - cp.height - 22, shadow=False)
    save_palette(pic0, out_dir, 'pic0.png')

    # bg0 840x500, the LiveArea background: logo top left, Ryu on the right
    bg0 = tint(cover(ink, (840, 500), (0.5, 0.3)), (255, 255, 255, 40))
    ryu = icon_fg.crop((0, 40, 432, 432))
    ryu = ryu.resize((round(ryu.width * 500 / ryu.height), 500), Image.LANCZOS)
    paste(bg0, ryu, 840 - ryu.width + 70, 0)
    paste(bg0, fit(logo, 330), 26, 18)
    save_palette(bg0, out_dir, 'bg0.png')

    # startup 280x158, the Start gate: the logo on darkened ink
    gate = tint(cover(ink, (280, 158), (0.62, 0.55)), (0, 0, 0, 70))
    lg = fit(logo, 236)
    paste(gate, lg, (280 - lg.width) // 2, (158 - lg.height) // 2)
    save_palette(gate, out_dir, 'startup.png')

    # icon0 128x128, the bubble: the adaptive icon, cropped towards its safe zone
    # (108 dp canvas, 72 dp safe zone; keep 18 dp of margin, the bubble is round)
    icon = Image.alpha_composite(icon_bg, icon_fg)
    m = 432 * 18 // 108
    save_palette(icon.crop((m, m, 432 - m, 432 - m)).resize((128, 128), Image.LANCZOS),
                 out_dir, 'icon0.png')

    print(f'icon0.png, pic0.png, bg0.png, startup.png written to {out_dir}')


if __name__ == '__main__':
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    main(sys.argv[1], sys.argv[2] if len(sys.argv) == 3 else os.path.join(root, 'game', 'livearea'))
