# sf4-vita loader

The Vita program that loads Street Fighter IV CE's Android libraries and runs them. It's based on [soloader-boilerplate](https://github.com/v-atamanenko/soloader-boilerplate) ("solobop") by Volodymyr Atamanenko, copied from commit `0a382f0e` (2026-07-22). See that project's README for how the template works in general.

## Status

- **Milestone 1:** both libraries load and link with no unresolved imports, and `JNI_OnLoad` runs.
- **Milestone 2:** `initCricket` and `NativeInit` run, and the frame loop reaches the title screen, which loads its assets and waits for a tap.
- **Milestone 3a:** game graphics reach the screen.
- **Milestone 4:** text, touch and gamepad input work, and the game boots offline to its **title screen**.
- **Milestone 5:** menus, nickname entry and **Arcade fights**, at the right speed, with sound, correct colours and controller mode.

Milestones 1–4 were reached in Vita3K on 2026-10-15 and 5 on 2026-10-17. None has been tried on real hardware.

## Data files

Extract them from your own copy of the game (1.08.03 `.apkm`) with:

```sh
python tools/extract_game.py <game.apkm> <out_dir>   # then copy <out_dir> to ux0:data/sf4/
```

That gives `libGltest2jni.so`, `libc++_shared.so` and `assets/` (1,133 files, about 500 MB). Saves go to `ux0:data/sf4/save/`.

You also need `ur0:data/libshacccg.suprx` (dumped with ShaRKF00D). On real hardware you need kubridge too.

## Building

Needs the softfp VitaSDK ([docs/dev-setup.md](../docs/dev-setup.md)). Clone with `--recurse-submodules`, or run `git submodule update --init` first.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug   # Debug enables the loader's logging
cmake --build build                            # → build/sf4_vita.vpk
```

From Windows, `tools/wsl-build.sh` does the same in one command (see dev-setup.md). Build options:

| Option | Default | Meaning |
|---|---|---|
| `KUBRIDGE` | `ON` | `ON` for real hardware. `OFF` builds so_util in VM-domain mode and skips the kubridge check, because Vita3K can't load kernel plugins. Use `OFF` only for Vita3K |
| `USE_SCELIBC_IO` | `ON` | Template default: route stdio through SceLibc |
| `SDK_VITAGL` | `ON` | Link VitaSDK's softfp vitaGL package. `OFF` builds the template's `lib/vitagl` submodule instead, which renders nothing in Vita3K |
| `VITAGL_SPLASH` | `ON` | Only affects the submodule build: `OFF` passes `NO_SPLASHSCREEN=1` |
| `AUTO_TAP` | — | Test hook: `-DAUTO_TAP=x,y,ms[,x,y,ms...]` injects one 150 ms tap at each (x, y), screen pixels, that many ms after the first frame |
| `AUTO_IME` | — | Test hook: `-DAUTO_IME=text` answers every text-entry box with `text` instead of opening the IME dialog |
| `ETC1_DECODE` | ON if `KUBRIDGE=OFF` | Decode ETC1 textures on the CPU (Vita3K's renderer crashes on them) |
| `GL_TRACE` | `OFF` | Diagnostics: log each distinct GL call/argument combination once, every call for frame 300, and draw counts every 60 frames ([source/gltrace.c](source/gltrace.c)) |

To test in Vita3K: `powershell -File tools\vita3k-run.ps1 <vpk> SFIV00001` installs the VPK, runs it, and prints the log. `tools\vita3k-shot.ps1 SFIV00001 out.png -Seconds N` runs the installed app and saves a screenshot of just the Vita3K window.

## Changes from the template

- **Two modules:** `libc++_shared.so` is loaded at `0x97000000` before the game at `0x98000000`. so_util resolves the game's C++ imports against it through `DT_NEEDED` ([source/utils/init.c](source/utils/init.c)).
- **[source/reimpl/sf4_compat.c](source/reimpl/sf4_compat.c):** the 66 bionic imports the template didn't cover (FORTIFY `_chk` variants, `*_l` locale functions, `getauxval`, `stderr`, …), plus a real `dl_unwind_find_exidx` so C++ exceptions can unwind. It also forwards `glLightf`/`glLightModelf` to vitaGL's vector forms, and wraps the texture uploads: 16-bit RGBA data is re-packed into the layout vitaGL really stores, paletted textures are expanded, `glCompressedTexSubImage2D` is implemented, and ETC1 is decoded for Vita3K (`ETC1_DECODE`). Its table is placed *before* the template's, so its entries take priority.
- **[source/game.c](source/game.c):** plays the part of `GlTest2Renderer`. It allocates the three direct `ByteBuffer`s and the touch table, runs `initCricket`/`NativeInit`, and calls `NativePreUpdate`/`Update`/`Draw`/`PostUpdate` each frame, paced like `onDrawFrame` (the game's frame interval, 60 Hz at most). Offline HTTP requests fail the way Android reports a connection error. It also patches FalsoJNI: direct buffers, which FalsoJNI lacks, and receiver tracking for instance calls, which FalsoJNI drops.
- **[source/java.c](source/java.c):** about 100 of the Java methods native code can call. File loading, saves, display size, timing, `String(byte[], charset)`/`getBytes`, `android.os.Build` and `AudioTrack` constants for Cricket, `PngFileLoad` (libpng), the text-entry calls, and offline stubs for store, network and platform services.
- **[source/text.c](source/text.c):** the Android Canvas text API (`TextInit`/`TextDraw`/`TextEnd`, measurements) with the Vita's system fonts (`ScePvf`), drawing into texture 2234.
- **[source/input.c](source/input.c):** front touch into the game's touch table, buttons and sticks into its gamepad bitmask. The game treats the buttons as a physical controller: pressing one hides the touch pad.
- **[source/ime.c](source/ime.c):** text entry (nickname) through the Vita IME dialog.
- **[source/reimpl/sl_vita.c](source/reimpl/sl_vita.c):** a minimal OpenSL ES (engine, output mix, PCM buffer-queue player) on `sceAudioOut` for Cricket Audio, because VitaSDK's `opensles` library never fires buffer-queue callbacks.
- **[source/java_stubs.h](source/java_stubs.h):** generated by `tools/gen_java_stubs.py`. Every remaining Java method is registered, and logs its name when called.
- **[source/patch.c](source/patch.c):** offline boot (skip the licence web view and online login), Google Play Games disabled, and offline nickname registration, as small patches that check the original instructions first. See [docs/libGltest2jni.md](../docs/libGltest2jni.md).
- **`dlsym`:** replaced, because the template's returns the table slot address instead of the symbol. Cricket Audio gets `slCreateEngine` from `sl_vita.c` and the `SL_IID_*` constants from VitaSDK's `opensles`.
- **`struct stat`:** the template's bionic layout was wrong (packed, newlib-sized fields), so `st_size` was read from the wrong offset. Fixed in [source/reimpl/io.h](source/reimpl/io.h), with static asserts; `lstat` converts too.
- **`kuser_patch` is disabled:** neither library uses the Linux kuser helpers.
- **Startup checks:** the loader refuses to start without `libshacccg.suprx`, instead of crashing inside vitaGL.
- **vitaGL:** linked from VitaSDK's package by default, because the template's pinned submodule build renders nothing in Vita3K (see `SDK_VITAGL`).
- **Build fixes:**
  - `_getentropy_r` is provided (the softfp newlib lacks it).
  - `lib/libc_bridge` declares its stub as a CMake `BYPRODUCTS`, so the Ninja generator works.
  - The `KUBRIDGE` option compiles a patched copy of so_util; the submodule itself is unchanged.

## Credits and licence

This code started as soloader-boilerplate, with contributions from TheFloW (original so-loader, gtasa_vita), Rinnegatamante (vitaGL, so_util, many ports), Volodymyr Atamanenko (solobop, FalsoJNI) and fgsfds. The template code is MIT licensed ([LICENSE](LICENSE)). The submodules keep their own licences: so_util (MIT), FalsoJNI (MIT), vitaGL (LGPL-3.0).
