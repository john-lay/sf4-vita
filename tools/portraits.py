"""Half-size character-select portraits for the Vita loader.

The character select streams in a 1024x1024 RGBA portrait (<char>_sel_chara.obm,
zlib-wrapped) per character. The loader uploads them at 512x512 to save video
memory, but halving them on the Vita at run time cost ~150 ms each and made the
menus stutter. This makes the half-size copies once, on the PC:

    <out>/half/<name>.half = b"SF4H" + the .obm's 8-byte outer header
                             + u16 width + u16 height (little-endian, 512x512)
                             + zlib(RGBA pixels)

The game still sees a 1024x1024 portrait (it lays the screen out from the
header); the loader uploads these pixels instead (loader/source/java.c).
The main menu's player cards (player_card_*.obm) get the same treatment;
the ones that aren't 1024x1024 are skipped.
The averaging matches the loader's run-time halving: 2x2 blocks, colours
weighted by alpha.

Usage: python tools/portraits.py <assets_dir> <out_dir>   (writes <out_dir>/half/)
"""
import os
import struct
import sys
import zlib

SUFFIX = '_sel_chara.obm'
# The main menu's player card pieces: also 1024x1024 RGBA (most of them) and
# drawn far smaller, 32 MB of textures while the menus are up.
PREFIX = 'player_card_'


def wanted(name):
    return name.endswith(SUFFIX) or (name.startswith(PREFIX) and name.endswith('.obm'))


def halve_rgba(px, w, h):
    hw, hh = w // 2, h // 2
    out = bytearray(hw * hh * 4)
    row = w * 4
    o = 0
    for y in range(hh):
        r0 = 2 * y * row
        r1 = r0 + row
        for x in range(hw):
            i0, i1 = r0 + 8 * x, r1 + 8 * x
            a0, a1, a2, a3 = px[i0 + 3], px[i0 + 7], px[i1 + 3], px[i1 + 7]
            sa = a0 + a1 + a2 + a3
            if sa == 1020 or sa == 0:
                for c in range(3):
                    out[o + c] = (px[i0 + c] + px[i0 + 4 + c] + px[i1 + c] + px[i1 + 4 + c] + 2) >> 2
            else:
                for c in range(3):
                    s = px[i0 + c] * a0 + px[i0 + 4 + c] * a1 + px[i1 + c] * a2 + px[i1 + 4 + c] * a3
                    out[o + c] = min(255, (2 * s + sa) // (2 * sa))
            out[o + 3] = (sa + 2) >> 2
            o += 4
    return bytes(out)


def make_half(obm_path, half_path):
    """Returns True if written, False if the file isn't a zlib-packed 1024x1024 RGBA image."""
    data = open(obm_path, 'rb').read()
    if data[:3] != b'OI\xff':
        return False
    inner = zlib.decompress(data[8:])
    fmt, bpp = inner[2], inner[3]
    w, h = struct.unpack_from('<HH', inner, 4)
    if (fmt, bpp, w, h) != (1, 32, 1024, 1024):
        return False
    small = halve_rgba(inner[8:8 + w * h * 4], w, h)
    with open(half_path, 'wb') as f:
        f.write(b'SF4H' + data[:8] + struct.pack('<HH', w // 2, h // 2) + zlib.compress(small, 6))
    return True


def make_all(assets_dir, out_dir):
    half_dir = os.path.join(out_dir, 'half')
    os.makedirs(half_dir, exist_ok=True)
    names = sorted(n for n in os.listdir(assets_dir) if wanted(n))
    done = 0
    for i, name in enumerate(names, 1):
        src = os.path.join(assets_dir, name)
        dst = os.path.join(half_dir, name + '.half')
        if os.path.exists(dst) and os.path.getmtime(dst) >= os.path.getmtime(src):
            done += 1
            continue
        print(f'  [{i}/{len(names)}] {name}', flush=True)
        if make_half(src, dst):
            done += 1
        else:
            print(f'    skipped: not a zlib-packed 1024x1024 RGBA image')
    print(f'{done} half-size images in {half_dir}')
    return done


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    make_all(sys.argv[1], sys.argv[2])
