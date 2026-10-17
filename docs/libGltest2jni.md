# libGltest2jni.so: native library analysis

Source: SF4 Champion Edition 1.08.03, `split_config.armeabi_v7a.apk` → `lib/armeabi-v7a/libGltest2jni.so`
(8,784,672 bytes, BuildID `4d0205be8c806ee4e8bb44de311af62267432707`). Analysed with pyelftools and a string scan.

## Summary

| Question | Answer | Impact on the Vita port |
|---|---|---|
| Architecture | ELF32 ARM EABI5, shared object, **softfp** float ABI (`e_flags` 0x5000200) | ✅ Loadable by so_loader. The loader must be built with VitaSDK's softfp world ([dev-setup.md](dev-setup.md)) |
| Graphics API | **OpenGL ES 1.x** (`libGLESv1_CM.so`), fixed-function | ✅ Very good: no shaders to translate. vitaGL supports the GLES1 fixed-function pipeline |
| Textures | PVRTC (`COglCore::NEW_loadPvrtcObmData`), with ATC/ETC1 variants also referenced | ✅ The SGX543 decodes PVRTC natively |
| Symbols | Static symbol table (`.symtab`) stripped, but **10,232 dynamic exports** with full C++ names. Section headers are present (27); an earlier version of this doc wrongly said they were stripped | ✅ Hooking and patching by name is easy. so_util needs the section headers |
| C++ runtime | libc++ (`std::__ndk1`), shared `libc++_shared.so`, NDK r26-era (`ne180000`) | **Decided:** load `libc++_shared.so` as a second module ([imports.md](imports.md)) |
| Audio | **Cricket Audio** (`Cki*`, `CkInit`) loads `libOpenSLES.so` via `dlopen`; there is also a "Java audio" path through AudioTrack | VitaSDK has an `opensles` package (OpenSL ES for Vita) that `dlopen`/`dlsym` could hand out (unconfirmed). Otherwise force the AudioTrack path and back it with SceAudio |
| File I/O | Plain `fopen`/`stat`/`opendir`, plus Java `AssetManager`/`InputStream` via JNI | Assets are read through Java, so falso_jni must serve `InputStream.read` from files on ux0: |
| Threads | pthreads (28 imports): mutex, cond, keys, create/join | Standard; existing so_loader ports cover these |
| Network | BSD sockets, `getaddrinfo`, UDT (`CUDT*`) P2P, HTTP through Java callbacks | Offline only: stub or let it fail gracefully |
| Size in memory | 3 PT_LOAD segments, ~8.7 MB total (`.text` 3.3 MB, `.rodata` 4.2 MB, `.bss` 73 KB) | ✅ Trivial compared with the Vita's RAM |
| Relocations | `R_ARM_RELATIVE`, `JUMP_SLOT`, `GLOB_DAT`, `ABS32` only | ✅ Standard set that so_loader already handles |

## DT_NEEDED

`libGLESv1_CM.so libz.so liblog.so libandroid.so libdl.so libc.so libm.so libc++_shared.so`

No `AAsset*`/`ANativeWindow*` imports, so `libandroid` appears to be linked but unused. The game doesn't create its own EGL context; it doesn't import any EGL functions. The Java `GLSurfaceView` provides the context, so the loader just needs to have a vitaGL context current before calling `NativeInit`.

## Import counts (341 total)

| Group | Count | Notes |
|---|---|---|
| libc/libm/other | 134 | Includes FORTIFY `__*_chk` variants, sockets, `getauxval`, `__sF`, `__errno` |
| C++ (`__ndk1`) | 82 | Strings, streams, `basic_filebuf`, `chrono::system_clock` |
| GLES1 | 68 | Fixed-function: matrices, lights, fog, `glAlphaFunc`, `glLogicOp`, client arrays. FBOs via `*OES` |
| pthread | 28 | |
| C++ ABI | 16 | Exceptions are used (`__cxa_throw`, `__gxx_personality_v0`). The loader must support ARM EHABI unwinding (`.ARM.exidx`) |
| aeabi mem | 8 | |
| dl | 3 | `dlopen`/`dlsym`/`dlclose` (used for OpenSL) |
| log | 2 | |

GLES1 calls to check against vitaGL support: `glLogicOp`, `glCopyTexImage2D`/`glCopyTexSubImage2D`, `glReadPixels`, `glFog*`, `glLight*`/`glMaterial*`, `glShadeModel`, plus the OES framebuffer/renderbuffer entry points (vitaGL may need aliases for the `OES` names).

## Java ↔ native interface

Java → native entry points are the 24 exports on `jp.gltest2.android.GlTest2Renderer`, plus `JNI_OnLoad`:

- Lifecycle: `NativeInit`, `NativeTerm`, `NativePause`, `NativeResume`, `NativePauseSave`, `ReturnToLogoScene`, `SetShowResumeScreen`
- Frame: `NativePreUpdate`, `NativeUpdate`, `NativePostUpdate`, `NativeDraw`, `NativeCommon`, `NativeDispatchAsyncBT`
- Network/store (stub out): `NativeHttpConnection*` (4), `NativeP2PUpdate`, `NativePurchaseResponse`, `NativeisConnectEnvFamily`, `appURLSchemeCheck`
- Netflix: `SetNetflixAccountHandleInUserName`, `SetShowNetflixUIFrg`. This build shares its code with the Netflix Games release. Need to check whether a Netflix/account gate is active in the Capcom build.
- Audio: `initCricket`

Native → Java calls go through about 34 distinct JNI method signatures. Classes referenced from native code: `GlTest2Renderer`, `android/media/AudioTrack`/`AudioFormat`/`AudioManager`, `android/os/Build`, `Environment`, `java/io/InputStream`, `java/lang/String`. Method names seen nearby include `getAssets`/`AssetManager` + `InputStream` reads, `getAssetPackDownloadProgress`, `getSdCardFreeBlocks`, `getFolderSize`, `getDeviceOSVersion`, `encodeHmacSHA256`, `convertStringFromBytes`, purchase/notification helpers, and `selfFinish`. **TODO:** pair each name with its signature exactly by tracing `GetMethodID` call sites in Ghidra.

## Boot flow and the loader's patches (1.08.03)

`AppMain::ST_logoSystem()` runs the boot sequence as a small state machine (state variable in a global, jump table over states 0 to 11):

| State | What it does |
|---|---|
| 0 | `requestPermissionPostNotifications()` → 1 |
| 1 → 10 | after the permission answer |
| 10 | `DeviceManager::IsNetworkEnable()`. Online → 3. Offline → "Upload failed / check your Internet connection" dialog (state 11), which loops back to 10 for ever |
| 3 | first-run user licence agreement in a **web view** (`GT_CreateFirstUserLicenseWebViewDialog`) |
| 4–6 | `ST_NetworkConnectStartBase`: online login, with wait/retry |
| 7 | logo timer, fade, `checkLoginGameCenter()`, then resume data / movie / **`SetSceneTitle()`** |
| 8, 9 | asset-pack download checks |

`AppMain::checkLoginGameCenter()` starts a Google Play Games sign-in if `Lib_GameCenter::isEnabled()` (a flag byte at `this+4`) is true, and waits behind a spinner until it completes.

The loader patches both, so the game boots offline (`loader/source/patch.c`). Each patch checks the original instruction words first and is skipped if they differ:

| Function + offset | Original | Patched | Effect |
|---|---|---|---|
| `ST_logoSystem` +0xD8 | `0x0A000004` (`beq` to the network error) | `0xE320F000` (`nop`) | never show the offline error |
| `ST_logoSystem` +0xDC | `0xE3000003` (`movw r0, #3`) | `0xE3000007` (`movw r0, #7`) | state 10 continues to state 7 (skips the licence web view and online login) |
| `Lib_GameCenter::isEnabled` +0x0 / +0x4 | `0xE24DD004`, `0xE58D0000` | `0xE3A00000`, `0xE12FFF1E` (`mov r0, #0; bx lr`) | Google Play Games off, no sign-in wait |
| `AppMain::GT_NameEntryDialog` +0xB9C | `0xEB09B989` (`bl ST_NetworkConnectStartBase`) | `0xEB0000A3` (`bl` to its `on_success` callback at +0xE30) | nickname registration saves the name instead of uploading it ("Upload failed" offline) |

Calls between exported functions inside the library go through PLT stubs, so a plain `objdump` labels them wrongly. `tools/plt_calls.py` resolves them.

## Other observations

- Engine and game classes keep their names: `AppMain`, `COglCore`, `CSound`, `CFile`, `GENERAL_TASK`, `DojoDataControl`, `PlayerCardDataControl`, `AndroidController`, `AndroidGCController` (game controller support, which helps for mapping Vita buttons).
- The game includes LZMA (`LzmaDec_*`) and zlib, probably for asset decompression.
- `ss::` is the SpriteStudio effect runtime (2D effects).
- Online backend URL templates (`%s/api/...`) cover auth, ranking, replays, P2P and the shop. All of it gets stubbed.
