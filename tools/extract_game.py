"""Extract the files the Vita loader needs from your own SF4 CE 1.08.03 .apkm.

Usage: python tools/extract_game.py <game.apkm> <out_dir> [--any-version]

It first checks that the game library is the 1.08.03 build the loader's
patches are for (SHA-256), and stops if not, unless --any-version is given.

<out_dir> becomes the contents of ux0:data/sf4/ on the Vita (or Vita3K):

    libGltest2jni.so, libc++_shared.so   from split_config.armeabi_v7a.apk
    assets/<name>                         from base.apk and split_install_assets.apk
    half/<portrait>.half                  half-size character-select portraits (tools/portraits.py)

Nothing is modified. Files already present with the same size are skipped.
"""
import hashlib
import io
import os
import sys
import zipfile

import portraits

LIBS = ('lib/armeabi-v7a/libGltest2jni.so', 'lib/armeabi-v7a/libc++_shared.so')
ASSET_SPLITS = ('base.apk', 'split_install_assets.apk')
GAME_LIB_SHA256 = '8e3bae3f4bc5386daac9d321ffdc32e0f3d421d07f30a1d0be2deff0fe4ef8a6'  # 1.08.03


def write(out_path, data):
    if os.path.exists(out_path) and os.path.getsize(out_path) == len(data):
        return False
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, 'wb') as f:
        f.write(data)
    return True


def main(apkm, out_dir, any_version=False):
    bundle = zipfile.ZipFile(apkm)
    written = skipped = 0

    split = zipfile.ZipFile(io.BytesIO(bundle.read('split_config.armeabi_v7a.apk')))
    # The loader's patches are for this exact game library (1.08.03). The
    # .apkm itself differs between download sites, so check the library.
    digest = hashlib.sha256(split.read(LIBS[0])).hexdigest()
    if digest == GAME_LIB_SHA256:
        print('libGltest2jni.so matches 1.08.03 (SHA-256 checked)')
    elif any_version:
        print(f'warning: libGltest2jni.so is not the 1.08.03 build (SHA-256 {digest}); continuing anyway')
    else:
        sys.exit(f'libGltest2jni.so is not the 1.08.03 build the loader expects (SHA-256 {digest}).\n'
                 'Use the 1.08.03 .apkm (README.md, "What you need"), or add --any-version to extract anyway.')
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
    args = [a for a in sys.argv[1:] if a != '--any-version']
    if len(args) != 2:
        sys.exit(__doc__)
    main(args[0], args[1], '--any-version' in sys.argv[1:])
