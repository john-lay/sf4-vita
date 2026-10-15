# sf4-vita

An in-progress port of the Android version of **Street Fighter IV: Champion Edition** to the **PlayStation Vita**.

> **Status: boots to the title screen, not yet playable.** In the Vita3K emulator the loader (in [`loader/`](loader/)) runs the game's own code up to its title screen, with graphics, text and touch input working. Menus, gameplay and audio are untested, and it hasn't been tried on a real Vita yet.

## Progress

Tested in the Vita3K emulator only. Nothing has run on a real Vita yet.

- [x] **Research:** no blockers. A 32-bit ARM build exists, it uses GLES1, there's no DRM, and every asset is in the 1.08.03 bundle
- [x] **Toolchain:** softfp VitaSDK, vitaGL and kubridge, plus a Vita3K test loop
- [x] **Loading:** the game's two native libraries load and link with no missing functions
- [x] **Startup:** the game's init runs, it loads its data and saves, and the frame loop runs
- [x] **Graphics:** the game draws to the screen
- [x] **Text:** in-game text is drawn with the Vita's built-in fonts
- [x] **Input:** touch screen and buttons reach the game
- [x] **Boots offline to the title screen:** skips the online licence, login and Google Play sign-in
- [ ] Menus and gameplay
- [ ] PNG images (Capcom logo, some UI)
- [ ] Sound and music (the audio engine starts; playback untested)
- [ ] Button mapping for all six attacks
- [ ] First run on real hardware, then speed (60 fps?) and memory use
- [ ] Release build and install instructions

The detailed task list is under [Next steps in RESEARCH.md](RESEARCH.md#next-steps).

## How it will work

This follows the approach of existing Vita ports of Android games such as GTA: San Andreas, Max Payne and Bully. The game's own ARM code is not recompiled. Instead, a small Vita program (the *loader*) does the following:

1. loads the game's native library (`libGltest2jni.so`) from the Android APK into memory
2. links it against Vita versions of the Android functions it expects: OpenGL ES via [vitaGL](https://github.com/Rinnegatamante/vitaGL), libc, pthreads and audio
3. stands in for the Java half of the app: file loading, text rendering, touch and controller input
4. runs the game's own init, update and draw functions every frame

## Findings so far

| | |
|---|---|
| Target version | Champion Edition **1.08.03** (bundle that includes all assets) |
| CPU code | 32-bit ARM (`armeabi-v7a`) build present ✅ |
| Graphics | OpenGL ES 1.x fixed-function, no shaders ✅ |
| Textures | Mostly uncompressed RGBA/RGB in Capcom's `.obm` format (plus paletted and zlib-compressed variants); all load fine ✅ |
| Symbols | ~10k exported functions with readable names ✅ |
| DRM | No licence check at startup ✅. The first-boot online licence agreement and login are skipped by a small patch |
| Controller | The game already supports gamepads ✅ |
| Audio | Cricket Audio, which starts on the Vita's OpenSL ES library ✅ (playback untested) |
| Text | Drawn by Android's Canvas; reimplemented with the Vita's system fonts ✅ |
| Online features | Stubbed; the port is offline only |
| Unknown | Memory use at runtime and whether it holds 60 fps |

Details:
- [RESEARCH.md](RESEARCH.md): checklist of open and answered questions
- [docs/libGltest2jni.md](docs/libGltest2jni.md): analysis of the native library
- [docs/java-side.md](docs/java-side.md): what the Java side does and how the loader will replace it
- [docs/imports.md](docs/imports.md): every function the game imports, and which need special handling on the Vita
- [docs/dev-setup.md](docs/dev-setup.md): how to set up the build environment

## Legal

This repository contains **no game code or assets** and never will. Players must supply files from their own legally purchased copy of the game. Street Fighter IV is © CAPCOM. This project is not affiliated with or endorsed by Capcom.

## Planned requirements (subject to change)

- A PS Vita / PS TV with HENkaku/Ensō
- [kubridge](https://github.com/bythos14/kubridge) and possibly a RAM-unlock plugin
- `libshacccg.suprx` (Sony's shader compiler) in `ur0:data/`, dumped from your own console with ShaRKF00D. Many Vita ports of Android games need this
- The game's APK bundle, with its files extracted to the memory card

Building the loader and preparing the game files are covered in [loader/README.md](loader/README.md). The toolchain setup (softfp VitaSDK in WSL 2) is in [docs/dev-setup.md](docs/dev-setup.md). There's no release build yet.
