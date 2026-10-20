# sf4-vita

A port of the Android version of **Street Fighter IV: Champion Edition** to the **PlayStation Vita**, nearly finished.

> **Status: plays well on a real PS Vita** (and in the Vita3K emulator), stable, with correct graphics and clean sound, using either touch or the Vita's buttons. The loader is in [`loader/`](loader/). Loading is much faster than at first, except the load into a fight. Left before a release: more responsive menus, a faster fight load, our own loading screen in place of vitaGL's logo, and a release build with install instructions. Only the four characters included in the APK bundle (Ryu, Chun-Li, Guile, Zangief) are available; the rest are the paid Champion Pack, which the port doesn't unlock. Training also opens the Store.

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
- [ ] Polish: menu speed and touch delay, a faster fight load, our own loading screen instead of vitaGL's logo
- [ ] Release build, app artwork and install instructions
- Not planned: unlocking store-locked content. The remaining characters are the paid Champion Pack; Training also opens the Store

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
| Speed | The game draws at 20 fps by design, as on Android. Fights hold 20 fps on a real Vita; menus are slower. The load into a fight is still slow (one file at a time from the memory card) |
| Memory | Tight: textures fill the Vita's graphics memory in fights. The loader avoids loading textures for characters that aren't present |

Details:
- [RESEARCH.md](RESEARCH.md): checklist of open and answered questions
- [docs/libGltest2jni.md](docs/libGltest2jni.md): analysis of the native library
- [docs/java-side.md](docs/java-side.md): what the Java side does and how the loader replaces it
- [docs/imports.md](docs/imports.md): every function the game imports, and which need special handling on the Vita
- [docs/dev-setup.md](docs/dev-setup.md): how to set up the build environment

## Legal

This repository contains **no game code or assets** and never will. Players must supply files from their own legally purchased copy of the game. Street Fighter IV is © CAPCOM. This project is not affiliated with or endorsed by Capcom.

## Requirements

- A PS Vita / PS TV with HENkaku/Ensō, with "Enable Unsafe Homebrew" turned on
- A recent [kubridge](https://github.com/bythos14/kubridge/releases) (older versions crash the loader at start-up). No RAM-unlock plugin is needed
- `libshacccg.suprx` (Sony's shader compiler) in `ur0:data/`, dumped from your own console with ShaRKF00D. Many Vita ports of Android games need this
- The game's 1.08.03 APK bundle (`.apkm`), with its files extracted to the memory card (about 500 MB)

Building the loader, preparing the game files and installing on a Vita are covered in [loader/README.md](loader/README.md). The toolchain setup (softfp VitaSDK in WSL 2) is in [docs/dev-setup.md](docs/dev-setup.md). There's no release build yet.
