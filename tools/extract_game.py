"""Extract the files the Vita loader needs from your own SF4 CE 1.08.03 .apkm.

Usage: python tools/extract_game.py <game.apkm> <out_dir>

<out_dir> becomes the contents of ux0:data/sf4/ on the Vita (or Vita3K):

    libGltest2jni.so, libc++_shared.so   from split_config.armeabi_v7a.apk
    assets/<name>                         from base.apk and split_install_assets.apk
    half/<portrait>.half                  half-size character-select portraits (tools/portraits.py)

Nothing is modified. Files already present with the same size are skipped.
"""
import io
import os
import sys
import zipfile

import portraits

LIBS = ('lib/armeabi-v7a/libGltest2jni.so', 'lib/armeabi-v7a/libc++_shared.so')
ASSET_SPLITS = ('base.apk', 'split_install_assets.apk')


def write(out_path, data):
    if os.path.exists(out_path) and os.path.getsize(out_path) == len(data):
        return False
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, 'wb') as f:
        f.write(data)
    return True


def main(apkm, out_dir):
    bundle = zipfile.ZipFile(apkm)
    written = skipped = 0

    split = zipfile.ZipFile(io.BytesIO(bundle.read('split_config.armeabi_v7a.apk')))
    for lib in LIBS:
        if write(os.path.join(out_dir, os.path.basename(lib)), split.read(lib)):
            written += 1
        else:
            skipped += 1

    for name in ASSET_SPLITS:
        split = zipfile.ZipFile(io.BytesIO(bundle.read(name)))
        for info in split.infolist():
            if info.is_dir() or not info.filename.startswith('assets/'):
                continue
            rel = info.filename[len('assets/'):]
            if write(os.path.join(out_dir, 'assets', *rel.split('/')), split.read(info)):
                written += 1
            else:
                skipped += 1

    print(f'{written} files written, {skipped} already up to date, in {out_dir}')
    portraits.make_all(os.path.join(out_dir, 'assets'), out_dir)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
