# Research checklist

Status key: `[ ]` open · `[~]` in progress · `[x]` answered (write the answer and source underneath)

## 0. Go / no-go blockers (answer these first)

- [x] **Which Android releases exist, and which ABIs does each ship?** Street Fighter IV Volt (2014) vs Champion Edition (2018+). Does the APK contain `lib/armeabi-v7a/`, or only `arm64-v8a`? No 32-bit ARM binary means the so_loader approach is dead and we would need a far harder route.
  - **Champion Edition (`jp.co.capcom.sf4ce`) ships `armeabi-v7a` in both 1.04.00 and 1.08.03. ✅** The main library is `libGltest2jni.so` (8.9 MB in 1.04, 8.8 MB in 1.08.03), alongside `libc++_shared.so` and Crashlytics libs (the Crashlytics ones can be ignored or stubbed). Source: our own APKMirror downloads, `unzip -l`.
  - **1.04.00** (single APK, 33 MB): code plus front-end assets only (48 files in `assets/`). The ~1,086 game assets (characters, stages, BGM, video) came from a separate Play **OBB expansion file** that we don't have. The dex contains the Google LicenseChecker/expansion downloader. On its own it is incomplete.
  - **1.08.03** (`.apkm` bundle, 389 MB): an App Bundle with an install-time asset pack, `split_install_assets.apk` (497 MB uncompressed, 1,090 files), which contains every game asset. The code lives in `base.apk` and `split_config.armeabi_v7a.apk`. **This is the version to target**, because it is self-contained.
  - The older Volt release (2014) was not investigated. Champion Edition 1.08.03 is enough.
  - Asset formats seen: `.ckb` (audio, likely CRI or Capcom-specific), `.obm` (textures/images), `.bmv` (video?), `.png`, and one 143 MB `sf4_online.mp4`.
- [x] **Which graphics API does it use?** GLES 2.0 is fine for vitaGL. GLES 3.x features could be a problem, and Vulkan would be a blocker.
  - **OpenGL ES 1.x fixed-function** (`libGLESv1_CM.so`, 68 GL imports, no EGL). No shaders. Textures are PVRTC, which the Vita supports natively. Details: [docs/libGltest2jni.md](docs/libGltest2jni.md).
- [~] **Memory footprint.** The code itself takes ~8.7 MB once loaded (fine). Runtime heap use and asset streaming are still unknown. Size of the `.so`, size of the assets, and peak RAM on a real Android device. Does it fit in roughly 330 MB?
- [ ] **Has anyone already tried?** Search the Vita homebrew scene (GitHub, VitaDB, GBAtemp, the Vita Nuova Discord) for previous SF4 attempts or requests.

## 1. The APK itself

- [x] Get a legally owned copy of the APK, plus OBB/split APKs if there are any. Record versions and hashes.
  - APKMirror downloads, SHA-256 (in `game/`):
    - 1.08.03 `.apkm`: `a6178ccc048d18ce0e626cc60814ea247cd26e6d8e82fd07583771156aa92e2b`
    - 1.04.00 `.apk`: `bc8ce0257632ee01e43f46a9c046c3c5383eb413e7b27f0a83113336d296043d`
- [x] List every native library (`libSF4.so`?, `libc++_shared.so`, middleware libs).
  - `lib/armeabi-v7a/` in 1.08.03: `libGltest2jni.so` (game), `libc++_shared.so`, four Crashlytics libs and `libdatastore_shared_counter.so`. We only load the first two.
- [~] Find where the assets live (inside the APK, an OBB, or downloaded on first run?). Note their format and whether they are compressed or encrypted.
  - In `split_install_assets.apk` (see §0). Formats `.ckb`/`.obm`/`.bmv`/`.png`/`.mp4`. **Not yet checked:** compression or encryption. LZMA and zlib are linked in, so some assets are probably compressed.
- [x] Is the binary stripped? How many exported symbols are there? (Exports make hooking much easier.)
  - Section headers are stripped, but there are 10,232 dynamic exports with full C++ names ([docs/libGltest2jni.md](docs/libGltest2jni.md)).
- [x] NDK version, C++ runtime (gnustl / libc++), and whether it is linked statically or shared.
  - Shared libc++ (`std::__ndk1`), NDK r26-era. Float ABI is **softfp**. We load `libc++_shared.so` as a second module.
- [~] Any packing, obfuscation or anti-tamper on the native side.
  - No sign of any: readable symbols, standard relocation types, and `JNI_OnLoad` disassembles as plain ARM code (spot check only). Not actively searched for integrity checks.

## 2. Native imports (what the loader must provide)

- [x] Full dynamic import list (`readelf --dyn-syms`), grouped by library: libc, libm, libdl, liblog, libGLESv2, libEGL, libOpenSLES, libandroid, etc.
  - Game lib: 245 external imports, plus 96 C++ ones served by `libc++_shared.so`. That lib adds 161 of its own. **Decision: load `libc++_shared.so` as a second module**, since `__ndk1` symbols can't resolve against Vita libstdc++. The list of bionic/newlib ABI mismatches that need wrappers (`__sF`, `stat`, `dirent`, `open` flags, 4-byte pthread types, `dl_unwind_find_exidx`, …) is in [docs/imports.md](docs/imports.md). Generator: `tools/list_imports.py`.
- [ ] pthread usage: thread count, TLS (`__thread` or `pthread_key`), and atomics.
- [x] File I/O patterns: AAssetManager vs fopen, and the paths it expects.
  - Assets: Java `FileLoad(name)` copies the whole file into a 32 MB shared ByteBuffer. No AAssetManager. Also some direct `fopen` (paths TBD).
- [~] Anything unusual: `dlopen` of other libs, `mmap` of large files, signals, `/proc` reads.
  - From the import list: `dlopen` is used (OpenSL ES); `signal` is imported; no `mmap`. Not yet checked: what `dlopen` and `fopen` are called with, and any `/proc` paths.

## 3. Java / JNI side

Full write-up: [docs/java-side.md](docs/java-side.md).

- [x] Decompile with jadx. Find the entry Activity and how much game logic lives in Java versus native.
  - `GlTest2Activity` → `GlTest2Renderer` (GLSurfaceView renderer). All game logic is native. Java is glue: file loading, text drawing, input, platform services.
- [~] List every `JNI_OnLoad`, `RegisterNatives` and `Java_*` export, and every Java method the native code calls back into (`GetMethodID` strings).
  - 24 native entry points. About 150 static Java methods native code *could* call; most are stubs. **TODO:** confirm in Ghidra exactly which ones native code looks up.
- [x] Input path: how touch, gamepad and keyboard events reach native code. Is there existing controller support we can drive from Vita buttons?
  - Touch: a `short[40]` table passed to `NativeUpdate`. Gamepad: native code polls a bitmask plus stick floats. **The game already supports controllers.** The bit table is in the doc.
- [~] Audio path: OpenSL ES, AAudio, or Java AudioTrack? Any middleware such as CRI ADX or FMOD?
  - Cricket Audio middleware; OpenSL ES via dlopen, or Java AudioTrack fallback. Not yet known which one it picks.
  - The softfp repo has an `opensles` package (`libOpenSLES.a`, OpenSL ES for Vita). Our `dlopen("libOpenSLES.so")` shim can probably hand out its symbols, so we may not need to write an OpenSL ES shim at all. Unconfirmed.
- [~] Video: are cutscenes or FMVs played by Android MediaPlayer? (Those would need replacing or skipping.)
  - One video, `sf4_online.mp4` (143 MB), plays in its own Activity. Can be skipped or played via SceAvPlayer. Codec still to check.
- [x] Licensing/DRM, Google Play Games, IAP, ads, analytics, online play: what must be stubbed, and does the game refuse to start without them?
  - **No licence check at startup.** Netflix layer is a stub in this build. Play Games, billing, AppsFlyer, Helpshift, Firebase and HTTP all get stubbed.
- [x] Save data: format, location, and whether it goes through Java (SharedPreferences) or native code.
  - `LocalFileSave`/`LocalFileLoad` write raw blobs (≤1 MB) to app-private files through the shared buffer. Format is opaque, but we don't need to understand it.
- [ ] **New:** text is rendered by Android Canvas into GL texture 2234. Needs a replacement (stb_truetype/FreeType + a free font with CJK glyphs).

## 4. Graphics

- [~] GL call inventory and which extensions are queried (ETC1/ETC2/ASTC/PVRTC textures).
  - All 68 GLES1 imports are listed in [docs/imports.md](docs/imports.md). Which extensions it queries is not yet checked.
- [ ] Texture formats in the assets. The Vita supports PVRTC and ETC1, not ASTC or ETC2. Re-encoding may be needed.
- [x] Shaders: shipped as GLSL source (vitaGL can translate them at runtime via `libshacccg`) or built at runtime? Count them and check their complexity.
  - The game has none (GLES1 fixed-function). vitaGL generates fixed-function shaders itself and compiles them at runtime, so `libshacccg.suprx` is still required (see §6).
- [ ] Render resolution and how it is chosen. Can it be forced to 960x544?
- [ ] Draw calls per frame and fill rate, as a rough guess at whether 60 fps is reachable on the SGX543.

## 5. Performance and timing

- [ ] CPU cost: SF4 needs a locked 60 fps for gameplay to feel right. Profile on a low-end ARMv7 Android device if one is available.
- [ ] Threading model (render thread, game thread, audio thread) and how it maps onto the Vita's 3 cores.
- [ ] Frame pacing and vsync assumptions (Choreographer?).

## 6. Vita-side tooling and prior art

- [x] Set up VitaSDK on this Windows machine (WSL or MSYS2) and build a hello-world VPK.
  - 2026-10-15: **WSL 2 / Ubuntu 24.04 with the softfp VitaSDK** (`sdk-vita_softfp-snapshot-20260828.667.1`, GCC 15.2.0). Must be softfp because `libGltest2jni.so` is (`e_flags` 0x5000200, `EF_ARM_ABI_FLOAT_SOFT`). vdpm 0.1.4 can't parse the schema-2 softfp channel manifest, so `tools/setup-vitasdk-softfp.sh` does the refresh by hand (signature and sha256s verified). Installed: vitaGL, vitaShaRK, SceShaccCgExt, mathneon, kubridge, opensles, zlib, taihen and freetype. Full details in [docs/dev-setup.md](docs/dev-setup.md).
  - First attempt was WSL 1 / Ubuntu 18.04 with the hard-float SDK: the compiler worked, but vdpm needs glibc ≥ 2.28 and the float ABI was wrong. Abandoned.
  - Smoke tests in Vita3K: a framebuffer-fill VPK (`SF4H00001`) and a vitaGL **GLES1 fixed-function** test (`SF4H00002`: client arrays, matrix stack, `glRotatef`). The GLES1 test renders correctly at **60 FPS, 960x544**. Not yet run on real hardware.
  - Without `libshacccg.suprx`, vitaGL crashes on the first draw: it calls the missing compiler through a null pointer. The loader should check for the file at startup and show an error.
- [ ] Pick a base port template to fork. Candidates are the open-source Android→Vita loaders by Rinnegatamante, TheFloW and others (e.g. gtasa_vita, maxpayne_vita, and newer ports using the "so_loader" + falso_jni template). Compare their licenses and how current they are.
- [~] Requirements on the user's Vita: kubridge, **libshacccg.suprx in `ur0:data/` (confirmed required; dumped with ShaRKF00D)**, fd_fix, and the RAM-unlock plugin if needed.
- [ ] Debugging workflow: the vita-parse-core coredump parser, PrincessLog/netlog, and a GDB stub if one exists.

## 7. Legal / distribution

- [ ] Confirm the approach: distribute only our loader. Users supply their own APK. No Capcom assets or code in the repo or in releases.
- [ ] Check the licenses of the template code and libraries we reuse.
