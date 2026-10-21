"""Bring your own Champion Pack purchase (all characters) to the Vita loader.

The extra characters are a Google Play asset pack ("demand_assets") that the
official app downloads after the purchase, and the unlock is in its save files.
Both live in the app's private folder on Android (/data/data/jp.co.capcom.sf4ce),
which needs root to copy: docs/full-roster.md has the steps (a rooted Android
Studio emulator signed in to your Google account).

Usage: python tools/import_purchase.py <app_data_dir> <out_dir>

<app_data_dir> is a copy of /data/data/jp.co.capcom.sf4ce (the folder that
contains files/).
<out_dir> gets, laid out like ux0:data/sf4/:

    assets/   the pack's files (they don't overlap the APK's assets)
    save/     your save files (they replace the Vita's: back those up first)
    half/     half-size character-select portraits (tools/portraits.py)

Copy the contents of <out_dir> over ux0:data/sf4/. These are your own game
files: keep them out of git and don't share them. Files already present with
the same size are skipped.
"""
import glob
import os
import shutil
import sys

import portraits

SAVE_PATTERNS = ('*.dat', '*.rpl', '*.rpt', 'rpd_*.bin')
NOT_SAVES = {'profileinstaller_profileWrittenFor_lastUpdateTime.dat'}


def copy_if_changed(src, dst):
    if os.path.exists(dst) and os.path.getsize(dst) == os.path.getsize(src):
        return False
    shutil.copy2(src, dst)
    return True


def main(app_dir, out_dir):
    files = os.path.join(app_dir, 'files')
    packs = sorted(glob.glob(os.path.join(files, 'assetpacks', 'demand_assets', '*', '*', 'assets')))
    if not packs:
        sys.exit(f'no demand_assets pack under {files}/assetpacks: has the game downloaded the characters?')
    pack = packs[-1]  # the newest version folder if there are several

    assets = os.path.join(out_dir, 'assets')
    os.makedirs(assets, exist_ok=True)
    names = os.listdir(pack)
    copied = sum(copy_if_changed(os.path.join(pack, n), os.path.join(assets, n)) for n in names)
    print(f'pack: {len(names)} files ({copied} copied) from {pack}')

    save = os.path.join(out_dir, 'save')
    os.makedirs(save, exist_ok=True)
    saves = sorted({p for pat in SAVE_PATTERNS for p in glob.glob(os.path.join(files, pat))
                    if os.path.basename(p) not in NOT_SAVES})
    for p in saves:
        shutil.copy2(p, os.path.join(save, os.path.basename(p)))
    print(f'save: {len(saves)} files: {", ".join(os.path.basename(p) for p in saves)}')

    portraits.make_all(assets, out_dir)
    print(f'done: copy the contents of {out_dir} over ux0:data/sf4/ (back up ux0:data/sf4/save/ first)')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
