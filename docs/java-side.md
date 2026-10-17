# Java side: what the loader has to emulate

Source: SF4 CE 1.08.03 `base.apk`, decompiled with jadx 1.5.1 (output kept outside the repo). Game code is in `jp.gltest2.android`, about 7.5k lines. Everything else is AndroidX, Google Play services, Kotlin, okio, AppsFlyer and Helpshift.

## Verdict

**No blocker on the Java side.** There is no licence check at startup. The Netflix layer is a stub in this build (`NetflixGamesHelper` returns `null`/no-ops, and `isCurrentUserExists()` returns `true`), and no Netflix SDK is bundled. Everything Java does for the native library is simple enough to reimplement in C inside the loader.

## Startup sequence

1. `GlTest2Activity.onCreate`: sets up OBB data (legacy), goes full screen, creates `TouchSurfaceView` (a GLSurfaceView subclass, `GLSurfaceView2`) and `GCControllerLib`, and initialises Google Play Games.
2. Static init of `GlTest2Renderer`: `System.loadLibrary("Gltest2jni")`, which runs `JNI_OnLoad`.
3. `onSurfaceCreated` (GL context is now current):
   - allocates three direct ByteBuffers: **data 32 MB**, send 4800 B, receive 4800 B
   - `initCricket(context)` (audio)
   - `NativeInit(data, send, recv)`
   - `TouchInit(5)`: touch table of 5 points × 8 shorts
4. `onSurfaceChanged` is empty. Resolution comes from `getDisplaySize` earlier.

**Loader equivalent:** create a vitaGL context, allocate the three buffers, call `JNI_OnLoad`, `initCricket`, `NativeInit`, then run the frame loop.

## Frame loop (`onDrawFrame`)

Each frame:
- Busy-waits until `s_frame_interval` ms have passed. Native code sets this via `FrameIntervalSet(ms)` or `setAnimationInterval(n)`, where `n` is in units of 1/10000 s. While waiting, it calls `NativeP2PUpdate()` every 10 ms (online only).
- Delivers any pending HTTP or purchase results (we never produce any).
- `NativePreUpdate()` → `NativeUpdate(short[] touchTable)` → `NativeDraw()` → `NativePostUpdate()`.

On the Vita: `game.c` sleeps until the interval has passed, as Android does (60 Hz at most). The game asks for 50 ms (20 fps) almost all the time, battle included, and steps its logic accordingly.

## Input

**Touch:** `short[5*8]`. Each slot holds `{x*10, y*10, curX*10, curY*10, prevX*10, prevY*10, phase, active}`, where phase is 1 = down, 2 = move, 3 = up, 4 = cancel. Coordinates are in screen pixels × 10. This maps directly from `sceTouch`.

**Gamepad:** native code polls `getKeyDownData_GamceController()` (bitmask) and four stick-axis getters (floats). Bits, as derived from `GCControllerLib.checkKeyCode` (Android keycode → bit):

| Bit | Value | Android button | Suggested Vita button |
|---|---|---|---|
| 0 | 0x1 | D-pad left | Left |
| 1 | 0x2 | D-pad right | Right |
| 2 | 0x4 | D-pad up | Up |
| 3 | 0x8 | D-pad down | Down |
| 5 | 0x20 | A | Cross |
| 6 | 0x40 | B | Circle |
| 7 | 0x80 | X | Square |
| 8 | 0x100 | Y | Triangle |
| 9 | 0x200 | L1 | L |
| 10 | 0x400 | R1 | R |
| 11 | 0x800 | L2 | — (a second Super key) |
| 12 | 0x1000 | R2 | — (a second Ultra key) |
| 13 | 0x2000 | R3 (thumb right) | — |
| 16 | 0x10000 | Start | Start |
| 17 | 0x20000 | Select | Select |

The game already supports physical controllers, so Vita buttons feed this path with no native patching. Battle uses four attacks plus Super/Ultra (`AppMain::GamePadInit`): P = X (□), K = A (✕), SP = Y (△), F = B (○), Ultra = L1 or R2, Super = R1 or L2, pause = Start. Pressing any of them switches the game to controller mode and hides the touch pad. That needs `getInitFrg_GamceController()` to return false, because it means "controller unplugged" (RESEARCH.md, Findings 2026-10-17).

## File I/O

| Java method | Behaviour | Loader implementation |
|---|---|---|
| `FileLoad(name)` | Finds the file (see search order below), copies it **in full into the 32 MB data buffer**, returns its size. It caches the last name/size and returns early if the same file is requested again | `fopen` + `fread` into the buffer |
| `PngFileLoad(name)` / `LocalPngFileLoad` | Decodes the PNG and uploads it with `glTexImage2D` to the currently bound texture; returns `(w<<16)\|h` | libpng + glTexImage2D (premultiplied, like an Android Bitmap) |
| `LocalFileLoad` / `LocalFileSave` | Reads/writes app-private files (max 1 MB) through the same buffer. **This is the save data** | Map to `ux0:data/sf4/save/` |
| `isFileExist`, `deleteFile`, `getFolderSize`, `getExternalPath`, etc. | Small file helpers | Simple to implement |

Asset search order in `FileLoadCheck(name)`: `assets/External/<name>`, then `assets/<name>`, then `assets/SF_IV_DATA/<name with dds→DDS>`, then the `demand_assets` asset pack, then `<external>/resource/<name>`, then the OBB. On the Vita we flatten everything into one directory, e.g. `ux0:data/sf4/assets/`, filled from the APK's `assets/` and the asset pack's `assets/`.

## Text rendering (needs real work)

The game draws its text through Android's Canvas:
- `TextInit(w, h)` creates an ARGB bitmap and canvas; GL texture id is hard-coded to 2234
- `TextBegin` clears it; `TextDraw(str, x, y, r, g, b, a, size, scaleX, …)` draws a string; `TextEnd` uploads the bitmap to texture 2234
- `TextSizeGet` / `GetTextWidth` / `GetTextHeight` measure text (values × 10)

On the Vita: implemented with the Vita's built-in system fonts through `ScePvf` (`loader/source/text.c`, notes below), so no font has to be shipped. Japanese text is still unchecked.

Text input (`KeyBordBase` → `TextBoxActivity`) is used for player-name entry. It is mapped to the Vita IME dialog (`loader/source/ime.c`); native code polls `CharFinishGet`/`CharNumGet`/`CharGet`.

## Video

`MoviePlay(id)` launches an Android activity that plays `sf4_online.mp4` (143 MB, copied out to `files/resource/`). The name suggests it's online-mode promo or intro footage. Options: play it with `SceAvPlayer` (H.264 MP4 is supported if the profile and resolution suit the Vita), or have `isMovieExist()` return false or skip it. **TODO:** check the codec and resolution, and find out where the game plays it.

## Audio

`initCricket(Context)` is a native method. Cricket Audio then picks OpenSL ES (`dlopen("libOpenSLES.so")`) or the Java AudioTrack fallback (`android/media/AudioTrack` via JNI). On the Vita it picks OpenSL ES, served by the loader's own `sl_vita.c`. Its BGM banks are opened with `fopen` on the path from `getExternalFullPath`, which the loader points at `assets/`.

## Implementation notes (from running it, 2026-10-15)

- **Strings:** native code builds *all* its Java strings, file names included, with `new String(byte[], charset)` and uses `String.getBytes(charset)`. The loader treats every charset as UTF-8 for now.
- **Startup order** matches `onSurfaceCreated`: `initCricket(context)` → `NativeInit(data, send, recv)`. `NativeInit` caches about 60 method IDs up front; most are never called.
- **First boot** creates `savedata2.dat`, `dumcnt2.dat`, `mycard2.dat`, `cardlist2.dat`, `repList.rpl` and `repTemp00–06.rpt` (88 KB each) through `LocalFileSave`.
- **Assets:** every APK asset sits directly in `assets/`. There are no `External/` or `SF_IV_DATA/` subfolders, so `FileLoad` finds everything in the second search location.
- **Cricket Audio** reads `Context.getFilesDir()`, `Environment.getExternalStorageDirectory()`, `android.os.Build` fields and `AudioTrack` statics and constants, then picks OpenSL ES.

- **Text** (implemented in `loader/source/text.c`): the game measures every line with `GetTextWidth`/`GetTextHeight` first and lays lines out from the heights, so the metrics must be close to Android's (Roboto: ascent 0.928, descent 0.244 × size). `TextDraw`'s `y` is the top of the line, because Java draws at `y - ascent`. The canvas is premultiplied RGBA and goes to texture 2234.
- **Permissions:** `requestPermissionPostNotifications()` is followed by polling `getRequestPermissionStatus()` until it reports 2 (answered). `getRequestPermissionResultCode()` returns 1 for granted and -1 for denied.
- **Network:** at boot the game calls `isNetworkEnable()`, and offline it loops on an "Upload failed" dialog. The loader patches the native boot flow instead of faking a connection (see docs/libGltest2jni.md).

## Stub list (safe no-ops / fixed returns)

Netflix* (stub already), Google Play Games/achievements, billing/`PurchaseResponse`, AppsFlyer, Helpshift, Firebase push/local notifications, `connectHTTP`/`cancelHTTP` (fail immediately, or have `isNetworkEnable()` return 0), Bluetooth (`startBluetooth`, etc.), `openURL`, `openExitCheckView`, OBB functions (return "not needed"), `DownLoad` (LG store), `getDeviceSerial`/`AndroidId`/`GUID` (fixed strings), battery (`getBatteryLevel` via `scePower`), `callGC`, `StatusBarSetting`, `LanguageGet` (from the Vita system language: 0 = JP, 1 = EN, 2 = FR, 3 = KR).
