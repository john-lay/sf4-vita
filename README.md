# sf4-vita

A port of the Android version of **Street Fighter IV: Champion Edition** to the **PlayStation Vita**, nearly finished.

> **Status: plays well on a real PS Vita** (and in the Vita3K emulator), stable, with correct graphics and clean sound, smooth menus and 20 fps fights, using either touch or the Vita's buttons. The loader is in [`loader/`](loader/). Loading is much faster than at first; the load into a fight still takes a while. Left before the 1.0 release: testing the release build, original app artwork, and (perhaps) a faster fight load and our own loading screen. Install steps: [Installing on a PS Vita](#installing-on-a-ps-vita). Out of the box only the four characters included in the APK bundle (Ryu, Chun-Li, Guile, Zangief) are available; the rest are the paid Champion Pack, which the port never unlocks itself. If you bought it, your own unlocked save and character download can be brought over, and the whole roster plays on the Vita: see [docs/full-roster.md](docs/full-roster.md).

## Progress

Tested in the Vita3K emulator and on a real PS Vita (with the latest kubridge).

- [x] **Research:** no blockers. A 32-bit ARM build exists, it uses GLES1 and there's no DRM. The 1.08.03 bundle has the base game's assets; most characters come from an on-demand Play download that it doesn't include
- [x] **Toolchain:** softfp VitaSDK, vitaGL and kubridge, plus a Vita3K test loop
- [x] **Loading:** the game's two native libraries load and link with no missing functions
- [x] **Startup:** the game's init runs, it loads its data and saves, and the frame loop runs
- [x] **Graphics:** the game draws to the screen
- [x] **Text:** in-game text is drawn with the Vita's built-in fonts
- [x] **Input:** touch screen and buttons reach the game
- [x] **Boots offline to the title screen:** skips the online licence, login and Google Play sign-in
- [x] **Menus:** nickname entry (Vita keyboard), main menu, Solo Play, character select
- [x] **Gameplay:** Arcade fights at the original speed, with correct sprite colours
- [x] **PNG images** (Capcom logo, some UI)
- [x] **Sound and music**
- [x] **Buttons:** every action (P, K, SP, F, Super, Ultra, pause) is on a Vita button; pressing one switches the game to controller mode and hides the touch pad
- [x] **Button layouts:** the Vita buttons follow the touch layout type picked in the game's Button configuration (A, B or C), and `controls.txt` can remap any action
- [x] **Real hardware:** runs end to end on a PS Vita; memory use brought within the Vita's limits; music fixed (2026-10-19)
- [x] **Faster loading:** about a fifth less time loading (direct file reads, no FIOS cache, libdeflate); own bubble and start-up splash for personal builds, made from your own copy of the game
- [x] **All characters from your own purchase:** your Champion Pack save and character download, copied from the official app, work on the Vita, without memory stalls (2026-10-22)
- [x] **Smooth menus:** the mode select screen no longer loads a replay fight behind its TVs (they show static), and the big menu images are uploaded at half size
- [x] **Privacy:** the save's account ID is hidden on the title screen, so screenshots and videos don't show it
- [ ] Polish: a faster fight load, our own loading screen instead of vitaGL's logo
- [ ] Release build, app artwork and install instructions
- Not planned: unlocking store-locked content without buying it

The detailed task list is under [Next steps in RESEARCH.md](RESEARCH.md#next-steps).

## How it works

This follows the approach of existing Vita ports of Android games such as GTA: San Andreas, Max Payne and Bully. The game's own ARM code is not recompiled. Instead, a small Vita program (the *loader*) does the following:

1. loads the game's native library (`libGltest2jni.so`) from the Android APK into memory
2. links it against Vita versions of the Android functions it expects: OpenGL ES via [vitaGL](https://github.com/Rinnegatamante/vitaGL), libc, pthreads and audio
3. stands in for the Java half of the app: file loading, text rendering, touch and controller input
4. runs the game's own init, update and draw functions every frame

## Findings so far

| | |
|---|---|
| Target version | Champion Edition **1.08.03** (bundle with the base game's assets; most characters are an on-demand download it doesn't include) |
| CPU code | 32-bit ARM (`armeabi-v7a`) build present ✅ |
| Graphics | OpenGL ES 1.x fixed-function, no shaders ✅ |
| Textures | Mostly uncompressed RGBA/RGB in Capcom's `.obm` format, plus paletted, zlib-compressed and a few ETC1 textures. The loader converts the formats vitaGL mishandles ✅ |
| Symbols | ~10k exported functions with readable names ✅ |
| DRM | No licence check at startup ✅. The first-boot online licence agreement and login are skipped by a small patch |
| Controller | The game already supports gamepads ✅ |
| Audio | Cricket Audio, playing through the loader's own small OpenSL ES implementation ✅ |
| Text | Drawn by Android's Canvas; reimplemented with the Vita's system fonts ✅ |
| Online features | Stubbed; the port is offline only |
| Speed | The game draws at 20 fps by design, as on Android. Fights and menus hold 20 fps on a real Vita. The load into a fight is still slow (one file at a time from the memory card) |
| Memory | Tight: textures fill the Vita's graphics memory in fights. The loader keeps sprites at 8 bits per pixel, uploads the big menu images and portraits at half size, and skips textures for characters that aren't present |

Details:
- [RESEARCH.md](RESEARCH.md): checklist of open and answered questions
- [docs/libGltest2jni.md](docs/libGltest2jni.md): analysis of the native library
- [docs/java-side.md](docs/java-side.md): what the Java side does and how the loader replaces it
- [docs/imports.md](docs/imports.md): every function the game imports, and which need special handling on the Vita
- [docs/dev-setup.md](docs/dev-setup.md): how to set up the build environment
- [docs/full-roster.md](docs/full-roster.md): bringing your own Champion Pack purchase (all characters) to the Vita

## Legal

This project's own code and docs are MIT licensed ([LICENSE](LICENSE)); the loader keeps its template's MIT licence and its libraries their own ([loader/README.md](loader/README.md#credits-and-licence)). This repository contains **no game code or assets** and never will. Players must supply files from their own legally purchased copy of the game. Street Fighter IV is © CAPCOM. This project is not affiliated with or endorsed by Capcom.

## Installing on a PS Vita

### What you need

- A **PS Vita** with homebrew enabled (HENkaku) and **VitaShell**. PS TV isn't supported yet: the menus need the touch screen.
- A **PC** (Windows, macOS or Linux) with **Python 3** (nothing else to install) and a copy of this repository.
- The game's **1.08.03** Android release as an `.apkm` bundle (`base.apk`, `split_config.armeabi_v7a.apk`, `split_install_assets.apk`), from your own copy of the game. This project can't provide it. Other versions don't work.
- About **500 MB** free on the memory card (about 2.2 GB more with the Champion Pack).

### 1. Prepare the Vita (once)

1. In **HENkaku Settings**, turn on **Enable Unsafe Homebrew**.
2. **kubridge:** download the latest `kubridge.skprx` from [github.com/bythos14/kubridge/releases](https://github.com/bythos14/kubridge/releases) and copy it to `ur0:tai/`. Add this line under `*KERNEL` in `ur0:tai/config.txt`, then reboot:
   ```
   ur0:tai/kubridge.skprx
   ```
   Older kubridge versions don't work: the game shows "your kubridge.skprx is too old" if yours is one.
3. **Shader compiler:** install [ShaRKF00D](https://github.com/Rinnegatamante/ShaRKF00D) and run it once. It extracts `libshacccg.suprx` from your Vita's own firmware into `ur0:data/`. (It can't be shared, so every player makes their own. Many Vita ports of Android games need it.)

### 2. Prepare the game files (on the PC)

**Check your copy.** The loader is made for one exact build of the game. These are the SHA-256 hashes of version 1.08.03:

| File | SHA-256 |
|---|---|
| `jp.co.capcom.sf4ce_1.08.03-156_….apkm` (APKMirror's bundle) | `a6178ccc048d18ce0e626cc60814ea247cd26e6d8e82fd07583771156aa92e2b` |
| `base.apk` (inside the bundle) | `7f6516ccf2ff39c5a15059ddea2fb6cafae0bc091afdc52c3e2cc1152498c063` |
| `split_config.armeabi_v7a.apk` | `45767d3374e0a377a1e070df8e92adaedcaed44168b316b5a9b0982a15b0d48d` |
| `split_install_assets.apk` | `31bdec6e1245e8f272f84eb45d8b1abdc41441028cc72cbdd755d0356dedb76f` |
| `lib/armeabi-v7a/libGltest2jni.so` (the game, inside `split_config.armeabi_v7a.apk`) | `8e3bae3f4bc5386daac9d321ffdc32e0f3d421d07f30a1d0be2deff0fe4ef8a6` |

To hash a file: `certutil -hashfile <file> SHA256` (Windows), `shasum -a 256 <file>` (macOS), `sha256sum <file>` (Linux). A bundle from another source can be packed differently, so its `.apkm` hash may not match even though the game inside is the same: what counts is `libGltest2jni.so`, and `extract_game.py` checks that one for you and stops if it's a different build.

```sh
python tools/extract_game.py <your .apkm> sf4_data
```

This writes `sf4_data/` with the game's two libraries, `assets/` (1,133 files, about 500 MB) and `half/` (smaller copies of a few big images, made on the PC so the Vita doesn't have to). Nothing in the bundle is modified.

### 3. Copy everything to the Vita

1. With VitaShell (USB or FTP), copy the **contents** of `sf4_data/` into `ux0:data/sf4/` (create the folder). It should look like this:
   ```
   ux0:data/sf4/
   ├── libGltest2jni.so     the game
   ├── libc++_shared.so
   ├── assets/              1,133 files (about 5,000 with the Champion Pack)
   ├── half/                smaller copies of big images, made by extract_game.py
   ├── save/                created on the first run: your saves
   └── controls.txt         created on the first run: button mapping
   ```
   The two `.so` files must be directly in `ux0:data/sf4/`, not in a subfolder such as `ux0:data/sf4/sf4_data/`.
2. Copy the VPK to the Vita and install it with VitaShell. (Download it from this project's Releases page, or build it yourself: [loader/README.md](loader/README.md#building).)
3. Start **Street Fighter IV CE** from the home screen. The first time you tap the title screen, the game asks for a nickname (the Vita keyboard opens), then shows a short controls tutorial.

### Optional: all characters

If you bought the **Champion Pack** on Android, you can bring your own purchase over: [docs/full-roster.md](docs/full-roster.md).

### Controls

Touch works everywhere, as on Android. In fights the buttons work too: □ punch, ✕ kick, △ special, ○ focus, R super, L ultra, Start pause. They follow the touch layout picked in the game's Options › Button configuration, and `ux0:data/sf4/controls.txt` can remap them ([details](loader/README.md#controls)).

### If something goes wrong

| What you see | Fix |
|---|---|
| "kubridge.skprx is not installed" / "is too old" | Step 1.2, then reboot |
| "libshacccg.suprx is not installed" | Step 1.3 |
| "Looks like you haven't installed the data files" | Step 3.1: check the files are directly in `ux0:data/sf4/`, not in a subfolder |
| `extract_game.py`: "not the 1.08.03 build" | Your bundle is another version of the game: get 1.08.03 (step 2 lists the hashes) |
| The app won't start at all ("unsafe homebrew") | Step 1.1 |

Your saves are in `ux0:data/sf4/save/`; back them up before reinstalling or updating. To uninstall, delete the app in VitaShell (or the home screen) and the `ux0:data/sf4/` folder.

## Building

Building the loader yourself is covered in [loader/README.md](loader/README.md#building). The toolchain setup (softfp VitaSDK in WSL 2) is in [docs/dev-setup.md](docs/dev-setup.md).
