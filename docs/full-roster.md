# All characters: bringing your own Champion Pack to the Vita

Out of the box the port has the four characters that come with the game's APK bundle: Ryu, Chun-Li, Guile and Zangief. The other characters are the **Champion Pack**, a paid in-app purchase. The port never unlocks it by itself. If you bought it on Android, this guide copies **your own** purchase to the Vita: the character download, and your save that holds the unlock.

> Only do this with a Champion Pack bought on your own Google account. The files you copy are your personal game files: don't share them or upload them anywhere. The save also holds your game account ID (the Vita build hides it on the title screen).

## Why it takes a few steps

After the purchase, the official app downloads the characters (about 1.7 GB) and stores them, together with the save that records the unlock, in its private folder on Android: `/data/data/jp.co.capcom.sf4ce`. Android doesn't let anything else read that folder, and Android backups leave it out. The only way to copy it is with root access. Rooting your phone is risky, so this guide uses Google's **Android emulator** on a PC instead: you sign in there with the same Google account, the game restores your purchase, and the emulator is rooted to copy the folder.

## What you need

- The port already working on your Vita with the base game (see [loader/README.md](../loader/README.md#testing-on-a-real-vita)).
- The Champion Pack bought on your Google account.
- A Windows, macOS or Linux PC with **Android Studio** and about **10 GB** free (the emulator, plus the copy).
- **Python 3** on the PC, and this repository.
- About **2 GB** free on the Vita's memory card.

## 1. Set up the Android emulator

In Android Studio, open the **Device Manager** and create a virtual device with these settings. Others failed in our tests.

| Setting | Use | What failed |
|---|---|---|
| Phone profile | **Pixel 7** (any phone profile with the Play Store icon) | |
| System image | **API 33, "Google Play", x86_64**, with normal 4 KB pages | The "16 KB page size" images Android Studio may suggest: black screen (the game's native libraries are too old for them) |
| Graphics | **Hardware**, set explicitly | "Automatic": the emulator crashed when the game started. "Software": black screen, and the phone stopped responding |
| RAM / internal storage | 4 GB / 16 GB | The default storage is too small for the download |

Start it. If `adb` commands hang later while the emulator is busy, wait and try again.

## 2. Restore your purchase in the emulator

1. In the emulator, open the **Play Store** and sign in with the Google account that bought the Champion Pack.
2. Install **Street Fighter IV Champion Edition**. The port is built for version **1.08.03**; check that the Play Store's version matches (on the app's page, "About this game").
3. Start the game and go through the first-run screens. With your purchase on the account, the game restores it and downloads the characters (about 1.7 GB). Keep the game open until the download finishes.
4. Check in **Arcade** that all the characters can be picked. Don't continue until they can.

## 3. Root the emulator

[rootAVD](https://gitlab.com/newbit/rootAVD) installs Magisk into the emulator's system image. Read its instructions first. In short, with the emulator running:

1. Download rootAVD and open a terminal in its folder.
2. Run it with the path of the emulator's ramdisk, relative to the Android SDK folder (on Windows the SDK is in `%LOCALAPPDATA%\Android\Sdk`):
   - Windows: `rootAVD.bat system-images\android-33\google_apis_playstore\x86_64\ramdisk.img`
   - macOS/Linux: `./rootAVD.sh system-images/android-33/google_apis_playstore/x86_64/ramdisk.img`

   On Windows, run it from its own folder (or by its full path); starting it some other way can fail with "not recognized".
3. When the emulator has shut down, start it again with a **cold boot** (Device Manager › the device's menu › Cold Boot Now).
4. Open the **Magisk** app once. It may reboot the emulator.
5. On the PC, check root: `adb shell su -c id`. Tap **Grant** when Magisk asks in the emulator. The answer should start with `uid=0(root)`.

`adb` is in the Android SDK's `platform-tools` folder. Add it to your PATH, or type its full path.

rootAVD saves the original as `ramdisk.img.backup` next to `ramdisk.img`. To undo the root later, copy it back over `ramdisk.img`, or delete the virtual device.

## 4. Copy the game's folder to the PC

Pack the game's files into one archive in the emulator, then pull it to the PC. (Streaming it straight out with `adb exec-out` corrupted the data in our tests.)

```sh
adb shell su -c "tar -cf /sdcard/sf4ce_data.tar -C /data/data/jp.co.capcom.sf4ce files"
adb pull /sdcard/sf4ce_data.tar
```

Check the copy is complete: the two checksums must match.

```sh
adb shell md5sum /sdcard/sf4ce_data.tar
certutil -hashfile sf4ce_data.tar MD5      # Windows; on macOS/Linux: md5sum sf4ce_data.tar
```

Then unpack it into a new folder (`tar` comes with Windows 10 and later):

```sh
mkdir sf4ce
tar -xf sf4ce_data.tar -C sf4ce
```

`sf4ce` should now contain a `files` folder, with your saves (`savedata2.dat` and others) and `assetpacks/demand_assets/…`. If you use Git Bash on Windows, put `MSYS_NO_PATHCONV=1` in front of the `adb` commands, or Git Bash rewrites the `/sdcard/...` paths.

You can delete the archive from the emulator afterwards: `adb shell rm /sdcard/sf4ce_data.tar`.

## 5. Prepare the files for the Vita

From this repository:

```sh
python tools/import_purchase.py sf4ce sf4_full
```

It writes `sf4_full/` laid out like the Vita's `ux0:data/sf4/`:

| Folder | What | Size |
|---|---|---|
| `assets/` | the characters (3,896 files; none of them replaces a base-game file) | 1.7 GB |
| `save/` | your saves from the emulator, with the unlock | under 1 MB |
| `half/` | half-size copies of the new characters' portraits (made on the PC, so the Vita doesn't have to) | about 7 MB |

## 6. Copy them to the Vita

1. **Back up the Vita's saves first:** copy `ux0:data/sf4/save/` to your PC with VitaShell. The emulator's saves replace them, so any progress made on the Vita so far is replaced by the emulator's.
2. Copy the **contents** of `sf4_full/` (`assets/`, `save/`, `half/`) into `ux0:data/sf4/` with VitaShell (USB or FTP), merging with the folders already there. This takes a while: it's 1.7 GB.
3. Start the game. In Arcade, all the characters should now be selectable. The unlock works offline.

## Good to know

- The title screen's **Delete Account** button is hidden on the Vita, because it could wipe the account in your save and the unlock with it. Keep the backup of `sf4_full/save/` on your PC anyway.
- The game runs within the Vita's memory with the full roster, but the load into a fight takes longer with the Champion Pack's bigger characters (Gouken, Ibuki, Juri, Rose, Evil Ryu, …).
- If something doesn't work, a Debug build writes `ux0:data/sf4/log.txt`; see [loader/README.md](../loader/README.md#testing-on-a-real-vita). Remove your account ID from the log before you share it.
- Background and what we found along the way: RESEARCH.md, "Full roster from the player's own purchase".
