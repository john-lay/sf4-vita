# Research checklist

Status key: `[ ]` open · `[~]` in progress · `[x]` answered (write the answer and source underneath)

## Milestones

Milestones 1–4 reached on 2026-10-15, 5 on 2026-10-17, all in Vita3K. None has been run on real hardware yet.

| # | Milestone | Details |
|---|---|---|
| 1 | Both `.so` files load and link, 0 unresolved imports; `JNI_OnLoad` runs | §6 |
| 2 | `initCricket` + `NativeInit` run, the frame loop runs, saves and assets load | §6 |
| 3 | Graphics reach the screen (fix: VitaSDK's vitaGL instead of the template's pinned one) | §4 |
| 4 | Text, input and offline-boot patches: **the title screen** | §4, [docs/libGltest2jni.md](docs/libGltest2jni.md#boot-flow-and-the-loaders-patches-10803) |
| 5 | Menus (2026-10-17): nickname entry (Vita IME), offline nickname patch, main menu, Solo Play, Arcade options, character select. `PngFileLoad`, BGM bank loading, offline HTTP failures | see "Findings 2026-10-17" below |
| 5b | **Arcade fights play** (2026-10-17): correct speed (frame pacing), sound (own OpenSL ES), correct sprite colours (vitaGL 16-bit texture fix). Checked by the user | see "Findings 2026-10-17" below |
| 5c | *Next:* all six attack buttons; the store-locked modes and characters | |
| 6 | *Later:* real hardware, then performance and memory | |

## Findings 2026-10-17 (Vita3K)

- **Title tap → "Nickname registration".** `KeyBordBase` opens Android's `TextBoxActivity`; native code polls `CharFinishGet`/`CharNumGet`/`CharGet`. Now mapped to the Vita IME dialog (`loader/source/ime.c`). On OK, `GT_NameEntryDialog` uploads the name with `ST_NetworkConnectStartBase(11, on_success, on_error)`, which shows "Upload failed" offline. Patched to call `on_success` directly, which saves the name (`patch.c`, [docs/libGltest2jni.md](docs/libGltest2jni.md#boot-flow-and-the-loaders-patches-10803)).
- **`convertStringFromBytes`** (UTF-16LE → charset; the game asks for UTF-8) was a stub returning a String instead of `byte[]`, which crashed after the nickname step. Implemented, along with `convertString`.
- **First menu visit** shows a multi-page battle-controls tutorial (tap to page, top-right button to close), then the main menu.
- **`PngFileLoad`** implemented with VitaSDK libpng (premultiplied RGBA, like an Android Bitmap).
- **Cricket banks:** `CSound::FileLoad` opens `getExternalFullPath(name)` = `<files>/resource/<name>`. Android copies the asset there first (`CopyToResourceDir`); we return `assets/<name>` instead. Cricket then reported "Invalid offset 0" because the template's bionic `struct stat` was wrong (packed, 16-bit nlink/uid/gid, 64-bit time_t → `st_size` at offset 38 instead of 48). Fixed in `reimpl/io.h`, with static asserts; `lstat` now converts too. (Sound still didn't play; see the OpenSL ES item below.)
- **HTTP:** `connectHTTP` now fails on the next frame through `NativeHttpConnectionResponce` / `…DidFailWithError` / `…DidFinish`, as Android does with no network.
- **ETC1:** some textures (e.g. the Store background) are ETC1 (`GL_ETC1_RGB8_OES`). Vita3K's Vulkan renderer crashes on GXM format `0x84000000`, so Vita3K builds decode ETC1 to RGB565 on the CPU (`ETC1_DECODE`, on by default when `KUBRIDGE=OFF`). Real hardware supports ETC1 natively.
- **Training opens the Store**, so in this version Training appears to be a purchase. The shop list request fails offline ("Upload failed"). Unconfirmed: how the game decides what is owned (`setPurchaseList`, `NativePurchaseResponse`, `isPurchaseDataEmpty`).
- **Character select:** only Ryu, Chun-Li, Guile and Zangief are selectable; the rest show as locked silhouettes. The install-time asset pack only contains fight data for those four. The other characters' data (e.g. `*_sel_chara.obm`, 28 missing) come from the **on-demand `demand_assets` Play Asset Delivery pack**, which is not in the `.apkm`. A full roster would need that pack from a device where Play downloaded it (unconfirmed where it is stored, probably app-private storage).
- The character-select world map drew with blue/green speckles: the vitaGL 16-bit texture bug below. **Fixed.**
- **User playtest (Vita3K, Arcade):** a fight runs. Mouse-click touch works; arrow keys move, and Z/X/C (□/✕/○) give punch, kick and focus attack. Bugs found: gameplay too fast, no sound, pink outlines around the sprites. **All three fixed**, see below.
- **Speed fixed:** `game.c` now paces frames like `onDrawFrame` (wait until `s_frame_interval` ms have passed since the previous frame), with a 60 Hz floor standing in for phone vsync. Checked against the in-game clock.
- **Frame rate (measured with the 5 s `frames:` log in `game.c`):** the game's default interval is 0.05 s (`AppMain::SetDefaultAnimationInterval`, via `CFile::setAnimationInterval`, ×10000 → `setAnimationInterval(500)`). Menus **and battle** run at 20 fps; the 5 ms interval only appears during loading and transitions. Game logic evidently advances more than one step per drawn frame, which is why the unpaced loop ran about 2.5× fast. Vita3K holds 20.0 fps in battle; game work is about 1–6 ms/frame. Android uses the same `onDrawFrame` wait, so this matches the original. (Unconfirmed: whether real Android devices actually show 20 fps.)
- **Crackle:** the first `sl_vita.c` called Cricket's callback (which renders the next buffer, 5–20 ms in Vita3K) on the output thread before writing the current 5 ms grain, and Cricket keeps only one buffer queued, so the port ran dry at every buffer boundary. Now a feeder thread copies buffers into a 43 ms ring and runs the callbacks, while the output thread only drains the ring. No underruns logged in Vita3K; the user confirms speed and audio are good.
- **No sound, root cause:** Cricket's `GraphOutputAndroid` sets up a standard Android buffer-queue player and renders only from the buffer-done callback. VitaSDK's `libOpenSLES` (Android's reference "wilhelm" with a `Vita.c` backend) created everything but never called the callback, so Cricket's task queue just grew ("Reallocating audio task buffer"). Confirmed by hooking `bufferDoneCallback` (never called) and `GraphOutput::start` (called). **Fix:** `reimpl/sl_vita.c`, our own minimal OpenSL ES (engine, output mix, PCM buffer-queue player, Play, Volume), one `sceAudioOut` port and thread per player. Cricket now renders. In Vita3K it warned once that a render took 11 ms. First user check: audible but slightly crackly (fixed, see Crackle above).
- **Pink outlines, magenta sprite tiles and the speckled character-select map: one vitaGL bug.** vitaGL stores `GL_RGBA` + `GL_UNSIGNED_SHORT_5_5_5_1` (and `4_4_4_4`) unconverted as GXM `U5U5U5U1_RGBA` (`U4U4U4U4_RGBA`), which reads B, G, R, A from the low bits up: alpha at the top instead of the bottom, and red and blue swapped (the swap was found by comparing with gameplay footage: skin was blue, the map's oceans brown). The game uploads some character sprites (palette already expanded) and the world map this way. The magenta key `0xF83E` (alpha 0) then shows as opaque (247, 8, 247): measured on screen, an exact match (magenta is symmetric in R and B). Fix in `sf4_compat.c`: re-pack 5551/4444 data into the layout vitaGL actually stores (still 16-bit). Expanding to RGBA8888 instead ran vitaGL out of memory at the stage-select screen (write to address 0x1). Paletted textures (`NEW_loadIndexObmData`, `PALETTE4/8_*`) are now expanded by us as well, into the palette's own 16-bit format, which also gives `glCompressedTexSubImage2D` (vitaGL has none) something to write into. Result: 3×40-frame fight bursts show no magenta (previously 2–10 per 40), and the world map and character colours match gameplay footage (user-confirmed).
- Leads that turned out wrong, so don't repeat them: Vita3K's texture cache (same with it off), `glLogicOp` (stubbed, but not called in fights), `glIsTexture`-based name allocation in `NEW_loadIndexObmData` (safe with vitaGL).
- Tools: `vita3k-shot.ps1 -Count N -Interval s` takes a burst of shots. The GL trace logs each distinct GL state that paletted sprites are drawn with.
- Test hooks: `-DAUTO_TAP=x,y,ms[,x,y,ms...]` (a sequence of one-off taps, timed from the first frame) and `-DAUTO_IME=text`.

## Next steps

In rough order. Each is small enough to start without more research.

Done on 2026-10-17: the title screen, menus, `PngFileLoad`, a first fight and sound (see the findings above). In Vita3K a mouse click is a front-panel tap, and Vita3K's keyboard mapping drives the buttons. Don't tap **Delete Account**. Every unimplemented Java call logs `Java method X() called` (`loader/source/java_stubs.h`).

1. **Controls.** The gamepad bitmask works (`loader/source/input.c`). Work out how the game maps its six attacks to the face buttons, L/R and the "L2/R2" bits (docs/java-side.md, bits 11/12), then decide on a Vita layout. The rear touch panel is a candidate for the extra buttons. (User test: arrows move; Z/X/C, i.e. □/✕/○, give punch, kick and focus.)
2. **Store-locked content.** Training opens the Store, and most characters are locked. Find how ownership is decided (`setPurchaseList`, `NativePurchaseResponse`, `isPurchaseDataEmpty`, `getPurchaseInventory`) and whether the locked characters' data exists at all without the `demand_assets` pack.
3. **Real hardware.** Build without `-DKUBRIDGE=OFF`, install kubridge and `libshacccg.suprx`, and copy `tools/extract_game.py` output to `ux0:data/sf4/`. Things to settle there:
   - whether the template's own vitaGL works on hardware (`SDK_VITAGL=OFF`; it fails in Vita3K)
   - frame rate (20 fps by design; check the 5 ms loading phases and Cricket's render time)
   - the 16-bit texture re-pack in `sf4_compat.c` (confirm the vitaGL layout on real hardware too)
   - memory use (`_newlib_heap_size_user` is 256 MB in `main.c`)
   - load times
4. **Tidy-ups**, each written up where it was found:
   - stdio `FILE` bridging (§3)
   - `String(byte[], charset)` assumes UTF-8, so check Japanese text (Shift_JIS?)
   - `LanguageGet` should follow the Vita's system language
   - the optional `sf4_online.mp4` video (`isMovieExist` returns false for now)
   - the logger mishandles `%f` (it shifts later arguments); fix it or avoid `%f`
   - the `Java method deleteIndicator()` / `setPurchaseList()` stubs that are hit
5. **Release:** a Release build (no debug logging), LiveArea images, and install instructions in the README.

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
  - Neither library uses the Linux kuser helpers (`0xFFFF0FC0`/`0xFFFF0FA0`), so the template's `kuser_patch` is disabled.

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
  - **Answered (2026-10-15):** Cricket Audio 1.6.5 logs "Using OpenSL ES for output" once `dlopen`/`dlsym` hand it the Vita `opensles` library's symbols (all 50 `SL_IID_*` plus `slCreateEngine` are mapped). It initialises without errors. Whether sound actually plays is untested.
- [~] Video: are cutscenes or FMVs played by Android MediaPlayer? (Those would need replacing or skipping.)
  - One video, `sf4_online.mp4` (143 MB), plays in its own Activity. Can be skipped or played via SceAvPlayer. Codec still to check.
- [x] Licensing/DRM, Google Play Games, IAP, ads, analytics, online play: what must be stubbed, and does the game refuse to start without them?
  - **No licence check at startup.** Netflix layer is a stub in this build. Play Games, billing, AppsFlyer, Helpshift, Firebase and HTTP all get stubbed.
- [x] Save data: format, location, and whether it goes through Java (SharedPreferences) or native code.
  - `LocalFileSave`/`LocalFileLoad` write raw blobs (≤1 MB) to app-private files through the shared buffer. Format is opaque, but we don't need to understand it.
- [ ] **stdio streams:** with `USE_SCELIBC_IO`, the template hands copies of newlib `FILE` structs (`__sF`, and our `stderr`) to SceLibc functions. Harmless if the game only logs to `stderr`; check before trusting `stdin`/`stdout`/`stderr` I/O.
- [ ] **New:** text is rendered by Android Canvas into GL texture 2234. Needs a replacement (stb_truetype/FreeType + a free font with CJK glyphs).

## 4. Graphics

- [~] GL call inventory and which extensions are queried (ETC1/ETC2/ASTC/PVRTC textures).
  - All 68 GLES1 imports are listed in [docs/imports.md](docs/imports.md). Which extensions it queries is not yet checked.
  - **vitaGL lacks `glCompressedTexSubImage2D` and `glLogicOp`**, so both are currently no-ops. Find out whether and how the game uses them (partial PVRTC uploads? XOR-style effects?).
- [x] **Why the screen was black (2026-10-15):** the template's pinned vitaGL (`lib/vitagl` @ `9c23758`) renders **nothing at all** in Vita3K. Even a bare red quad drawn by the loader, with the game never run, stayed black. VitaSDK's packaged softfp vitaGL (`r1488.g2bdbe89`) works, so the loader now links that by default (`SDK_VITAGL=ON`). Untested on hardware. Ruled out on the way: face culling, the game's off-screen framebuffer, 4× MSAA.
  - The faint spinning "GL" shape in earlier screenshots was **vitaGL's boot splash** (spotted by the user). It never went away because the broken build never presented a frame.
  - With the SDK vitaGL, the game draws a **red-framed dialog box** from its UI atlas, with a blinking "confirm" arrow. It's empty because text drawing is still a stub, and it's waiting for a tap. **Next: text rendering (texture 2234) and touch input.**
  - `glReadPixels` always returns zeros in Vita3K, so it's no use for checking what was rendered. Use window screenshots (`tools/vita3k-shot.ps1`).
- [x] **Milestone 4 (2026-10-15, Vita3K): the title screen.** "Street Fighter IV Champion Edition", "TAP SCREEN", version 1.08.03, rendered by the game with its own text. To get there:
  - **Text:** implemented the Android Canvas text API with the Vita's own system fonts through `ScePvf` (`loader/source/text.c`). Two Vita3K quirks: `scePvfGetCharInfo` returns a 0×0 bitmap and `scePvfGetFontInfo` returns zero metrics, so glyph size comes from `scePvfGetCharImageRect` and placement from the glyph bearings. Line metrics use Android's default font (Roboto) ratios (ascent 0.928 × size, descent 0.244 × size), which the game's layout depends on.
  - **Input:** front touch feeds the game's touch table exactly like `TouchSet`, and the buttons feed the gamepad bitmask (`loader/source/input.c`). A tap is confirmed to reach the game (it dismissed a dialog).
  - **First-boot dialogs:**
    - The notification-permission request is answered at once ("granted").
    - The "Upload failed" dialog was the boot-time network check. The game wants an online licence agreement and login on first boot, so the loader patches the boot state machine to skip both, and disables Google Play Games sign-in. Details: [docs/libGltest2jni.md](docs/libGltest2jni.md#boot-flow-and-the-loaders-patches-10803).
  - Every Java method native code can call is now registered. The roughly 60 without an implementation log their name when called (`loader/source/java_stubs.h`, generated by `tools/gen_java_stubs.py`).
  - After sitting on the title for about 15 s the screen fades to black (probably an attract/demo sequence). **Next: tap through the title into the menus, implement `PngFileLoad` (`CP.png`, checkboxes), audio, and the real hardware test.**
- [x] **`glTexParameterf` "receives garbage": false alarm.** The game passes correct floats (9728.0 `GL_NEAREST`, 9729.0 `GL_LINEAR`, 33071.0 `GL_CLAMP_TO_EDGE`; literals checked in `COglCore::Texture_setParam`). The odd "raw bits" came from our logger, which mishandles `%f` and shifts the arguments after it. Don't trust `%f` in `l_*` output (2026-10-17).
- [x] Texture formats in the assets.
  - **`.obm` format** (our own analysis): magic `"OI"`, a format byte, bits per pixel, then width and height as LE u16, then the pixel data. Seen: `01`/32 bpp = RGBA8888 (`glTexImage2D` RGBA), `00`/24 bpp = RGB888, `04`/4 bpp = 16-colour palette + indices (uploaded as `GL_PALETTE4_RGBA4_OES`), `ff` = zlib-wrapped inner `.obm`. Non-power-of-two sizes occur (e.g. 248×54). None of the title/UI files checked are PVRTC. The Vita supports PVRTC and ETC1, not ASTC or ETC2. Re-encoding may be needed.
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
- [x] Pick a base port template to fork. Candidates are the open-source Android→Vita loaders by Rinnegatamante, TheFloW and others (e.g. gtasa_vita, maxpayne_vita, and newer ports using the "so_loader" + falso_jni template). Compare their licenses and how current they are.
  - **[soloader-boilerplate](https://github.com/v-atamanenko/soloader-boilerplate)** (MIT, updated 2026-07). It's the base of a dozen shipped ports, requires the softfp SDK, and already wraps most bionic/newlib mismatches. Submodules: so_util (MIT), FalsoJNI (MIT), vitaGL (LGPL-3.0). gtasa_vita (MIT, 2024) is the older single-game ancestor.
  - so_util resolves `DT_NEEDED` against already-loaded modules, so loading `libc++_shared.so` first needs no extra code. It needs section headers: both libs have them, and `.ARM.exidx` is present. The game lib has only `.gnu.hash`; so_util falls back to a linear scan for lookups.
  - Details and our changes: [loader/README.md](loader/README.md).
- [x] **Milestone 1 (2026-10-15, Vita3K):** both modules load, relocate and link with **0 unresolved imports**. Static constructors run, and `JNI_OnLoad` returns `0x10006` (`JNI_VERSION_1_6`). Not yet run on real hardware.
  - **Vita3K can't load kubridge** (kernel plugin), so `-DKUBRIDGE=OFF` builds so_util in VM-domain mode for emulator testing. Real hardware uses the default kubridge build.
- [x] **Milestone 2 (2026-10-15, Vita3K):** `initCricket` and `NativeInit` return, and the frame loop runs without crashing. The game loads its saves and title-screen assets (`title_bg.obm`, `logo.obm`, `tap_screen.obm`, `system_text.csv`) and waits at the title screen. **The screen is almost entirely black** (a faint blue shape in the centre), so rendering is the next problem. Not yet run on real hardware.
  - Findings on the way, all handled in `loader/source/java.c` / `game.c`:
    - The game builds every string, file names included, with `new String(byte[], charset)` and uses `getBytes(charset)`. FalsoJNI drops the receiver of instance calls, so the loader wraps `CallObjectMethod*` to record it.
    - FalsoJNI has no direct `ByteBuffer`s; the loader implements them as byte arrays.
    - The template's `dlsym` returns the address of the table slot instead of the symbol. Replaced.
    - Cricket queries `AudioTrack` statics and constants even when it ends up on OpenSL ES. A sample rate of -1 made it jump to address 0.
- [~] Requirements on the user's Vita: kubridge, **libshacccg.suprx in `ur0:data/` (confirmed required; dumped with ShaRKF00D)**, fd_fix, and the RAM-unlock plugin if needed.
- [ ] Debugging workflow: the vita-parse-core coredump parser, PrincessLog/netlog, and a GDB stub if one exists.

## 7. Legal / distribution

- [ ] Confirm the approach: distribute only our loader. Users supply their own APK. No Capcom assets or code in the repo or in releases.
- [ ] Check the licenses of the template code and libraries we reuse.
