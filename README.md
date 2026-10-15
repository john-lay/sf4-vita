# sf4-vita

An in-progress port of the Android version of **Street Fighter IV: Champion Edition** to the **PlayStation Vita**.

> **Status: research and toolchain done, loader not started.** Nothing here is playable. We found no blockers. The build setup is working: a vitaGL test using the same GLES1 calls as the game runs at 60 fps in the Vita3K emulator. The next step is the loader.

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
| Textures | PVRTC, supported natively by the Vita GPU ✅ |
| Symbols | ~10k exported functions with readable names ✅ |
| DRM | No licence check at startup ✅ |
| Controller | The game already supports gamepads ✅ |
| Audio | Cricket Audio over OpenSL ES / AudioTrack. A Vita OpenSL ES library exists and may cover it (unconfirmed) |
| Text | Drawn by Android's Canvas. Needs reimplementing |
| Online features | Will be stubbed; the port is offline only |
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

Build instructions will be added once there is something to build. The toolchain setup (softfp VitaSDK in WSL 2) is described in [docs/dev-setup.md](docs/dev-setup.md).
