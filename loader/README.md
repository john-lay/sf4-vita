# sf4-vita loader

The Vita program that loads Street Fighter IV CE's Android libraries and runs them. It's based on [soloader-boilerplate](https://github.com/v-atamanenko/soloader-boilerplate) ("solobop") by Volodymyr Atamanenko, copied from commit `0a382f0e` (2026-07-22). See that project's README for how the template works in general.

## Status

- **Milestone 1:** both libraries load and link with no unresolved imports, and `JNI_OnLoad` runs.
- **Milestone 2:** `initCricket` and `NativeInit` run, and the frame loop reaches the title screen, which loads its assets and waits for a tap.
- **Milestone 3a:** game graphics reach the screen.
- **Milestone 4:** text, touch and gamepad input work, and the game boots offline to its **title screen**.
- **Milestone 5:** menus, nickname entry and **Arcade fights**, at the right speed, with sound, correct colours and controller mode.
- **Milestone 6:** runs end to end **on a real PS Vita**: menus, Arcade and fights with correct graphics and clean sound, within the Vita's memory.

Milestones 1–4 were reached in Vita3K on 2026-10-15 and 5 on 2026-10-17. Milestone 6 came on 2026-10-18/19. What's left (load times, menu speed, splash screens, a Release build) is listed under [Next steps in RESEARCH.md](../RESEARCH.md#next-steps).

## Data files

Extract them from your own copy of the game (1.08.03 `.apkm`) with:

```sh
python tools/extract_game.py <game.apkm> <out_dir>   # then copy <out_dir> to ux0:data/sf4/
```

That gives `libGltest2jni.so`, `libc++_shared.so`, `assets/` (1,133 files, about 500 MB) and `half/` (half-size character-select portraits, made by `tools/portraits.py`). Saves go to `ux0:data/sf4/save/`.

**All characters (your own Champion Pack purchase):** the extra characters are a Google Play download that lives, with the unlock in the save files, in the app's private storage. With a copy of that folder (`/data/data/jp.co.capcom.sf4ce`, which needs root: RESEARCH.md describes the rooted Android emulator route), `python tools/import_purchase.py <that folder> <out_dir>` lays out the pack (`assets/`, 3,896 files, 1.7 GB), your saves (`save/`) and the half-size portraits (`half/`) like `ux0:data/sf4/`. Back up the Vita's `save/` first. These are your own files: never commit or share them.

You also need `ur0:data/libshacccg.suprx` (dumped with ShaRKF00D). On real hardware you need kubridge too.

## Building

Needs the softfp VitaSDK ([docs/dev-setup.md](../docs/dev-setup.md)). Clone with `--recurse-submodules`, or run `git submodule update --init` first.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug   # Debug enables the loader's logging (also written to ux0:data/sf4/log.txt)
cmake --build build                            # → build/sf4_vita.vpk
```

From Windows, `tools/wsl-build.sh` does the same in one command (see dev-setup.md). Build options:

| Option | Default | Meaning |
|---|---|---|
| `KUBRIDGE` | `ON` | `ON` for real hardware. `OFF` builds so_util in VM-domain mode and skips the kubridge check, because Vita3K can't load kernel plugins. Use `OFF` only for Vita3K |
| `USE_SCELIBC_IO` | `ON` | Template default: route stdio through SceLibc |
| `SDK_VITAGL` | `ON` | Link VitaSDK's softfp vitaGL package. `OFF` builds the template's `lib/vitagl` submodule instead, which renders nothing in Vita3K |
| `VITAGL_SPLASH` | `ON` | Only affects the submodule build: `OFF` passes `NO_SPLASHSCREEN=1`. VitaSDK's package (the default) always shows vitaGL's logo until the first frame |
| `AUTO_TAP` | — | Test hook: `-DAUTO_TAP=x,y,ms[,x,y,ms...]` injects one 150 ms tap at each (x, y), screen pixels, that many ms after the first frame |
| `AUTO_IME` | — | Test hook: `-DAUTO_IME=text` answers every text-entry box with `text` instead of opening the IME dialog |
| `AUDIO_DUMP` | — | Diagnostics: `-DAUDIO_DUMP=skip,seconds` skips `skip` s of audio, then writes the next `seconds` of what goes to the audio port to `ux0:data/sf4/audio_dump.raw` (raw 48 kHz stereo s16). It contains game audio, so keep it in `game/` |
| `ETC1_DECODE` | ON if `KUBRIDGE=OFF` | Decode ETC1 textures on the CPU (Vita3K's renderer crashes on them) |
| `VITA3K_TEX16` | ON if `KUBRIDGE=OFF` | Re-pack 16-bit RGBA textures for Vita3K, which reads their bit order differently from the real GPU. Must be OFF on hardware |
| `PALETTED_NATIVE` | ON if `KUBRIDGE=ON` | Keep the game's paletted textures (the fight sprites) at 8 bpp as GXM paletted textures instead of expanding them to 16 bpp: half the memory, which the Champion Pack characters' 2048×1024 sheets need. Off for Vita3K until checked there |
| `MENU_REPLAY` | `OFF` | Let the mode select screen's TVs play a replay. The game loads that whole fight on top of the menus, which ran vitaGL out of memory with Champion Pack characters (4 s frames). Off: the TVs show static ([source/patch.c](source/patch.c)) |
| `LIVEAREA_DIR` | `extras/livearea` | Folder with the VPK's `icon0.png` (bubble), `pic0.png` (start-up splash), `bg0.png` and `startup.png`. `python tools/make_livearea.py <game.apkm>` makes a set from your own copy of the game into `game/livearea/` (the game's icon and title art). For your own builds only; never commit or share them |
| `MSAA` | `OFF` | 4× multisample anti-aliasing. Off since 2026-10-20: no visible difference on the Vita, and 20 MB more free VRAM |
| `FIOS_CACHE` | `OFF` | The template's FIOS2 RAM cache (128 KB blocks) over `ux0:data/sf4/`. Off since 2026-10-20: without it file reads are twice as fast on the Vita |
| `IO_BENCH` | `OFF` | Diagnostics: at start-up, time file opens (different assets, the same asset again, a save file), small reads, closes and log writes, and log the averages (`io bench` lines) |
| `TEXT_TRACE` | `OFF` | Diagnostics: log every text call (`TextBegin`/`TextEnd`, text sizes, each draw). Off by default because the menus make hundreds per second, and writing them to `log.txt` slowed the Vita |
| `GL_TRACE` | `OFF` | Diagnostics: log each distinct GL call/argument combination once, every call for frame 300, and draw counts every 60 frames ([source/gltrace.c](source/gltrace.c)) |

To test in Vita3K: `powershell -File tools\vita3k-run.ps1 <vpk> SFIV00001` installs the VPK, runs it, and prints the log. `tools\vita3k-shot.ps1 SFIV00001 out.png -Seconds N` runs the installed app and saves a screenshot of just the Vita3K window.

## Testing on a real Vita

Tested on a PS Vita since 2026-10-18. Setup:

1. **Console:** a PS Vita or PS TV on HENkaku/Ensō with VitaShell. In HENkaku Settings, turn on **Enable Unsafe Homebrew** (the loader needs it).
2. **kubridge:** copy a recent `kubridge.skprx` ([bythos14 releases](https://github.com/bythos14/kubridge/releases); older versions lack `kuKernelMemProtect` and the loader crashes at start-up) to `ur0:tai/` and add the line `ur0:tai/kubridge.skprx` under `*KERNEL` in `ur0:tai/config.txt`. Reboot.
3. **Shader compiler:** put `libshacccg.suprx` in `ur0:data/`. Extract it on your own console with [ShaRKF00D](https://github.com/Rinnegatamante/ShaRKF00D); it can't be redistributed.
4. **Game data** from your own 1.08.03 `.apkm` (about 500 MB):
   ```sh
   python tools/extract_game.py <game.apkm> <out_dir>
   ```
   Copy the *contents* of `<out_dir>` (`libGltest2jni.so`, `libc++_shared.so`, `assets/`) to `ux0:data/sf4/` with VitaShell (USB or FTP).
5. **Install** the VPK (`build/sf4_vita.vpk` from a default build, i.e. without `-DKUBRIDGE=OFF`) with VitaShell, then start *Street Fighter IV CE* from the home screen.

The loader stops with a message if kubridge, `libshacccg.suprx` or the game data is missing. Debug builds write their log to `ux0:data/sf4/log.txt` (overwritten on each start); copy it off with VitaShell after a test. Saves go to `ux0:data/sf4/save/`. After a crash, the Vita writes a core dump (`psp2core-*`) to `ux0:data/`; read it with vita-parse-core and the unstripped ELF of the same build (RESEARCH.md, Findings 2026-10-19).

## Controls

Touch works everywhere, as on Android. In a fight the Vita buttons act as a controller: pressing one hides the touch pad, and tapping the gamepad icon brings it back.

| Action | Type A (default) | Type B | Type C |
|---|---|---|---|
| Punch (P) | □ | ✕ | □ |
| Kick (K) | ✕ | ○ | △ |
| Special (SP) | △ | △ | ✕ |
| Focus (F) | ○ | □ | ○ |

Super = R, Ultra = L, pause = Start, D-pad or left stick to move. The type follows the touch layout picked in the game's Options › Button configuration (TYPE A/B/C). To change anything, edit `ux0:data/sf4/controls.txt`, which is written with instructions on first run. It can fix the preset (`preset = a`) or give any action other buttons (`super = r triangle`).

## Changes from the template

- **Two modules:** `libc++_shared.so` is loaded at `0x97000000` before the game at `0x98000000`. so_util resolves the game's C++ imports against it through `DT_NEEDED` ([source/utils/init.c](source/utils/init.c)).
- **[source/reimpl/sf4_compat.c](source/reimpl/sf4_compat.c):** the 66 bionic imports the template didn't cover (FORTIFY `_chk` variants, `*_l` locale functions, `getauxval`, `stderr`, …), plus a real `dl_unwind_find_exidx` so C++ exceptions can unwind. It also forwards `glLightf`/`glLightModelf` to vitaGL's vector forms, and wraps the texture uploads: paletted textures are expanded, `glCompressedTexSubImage2D` is implemented, and for Vita3K only, 16-bit RGBA data is re-packed into Vita3K's bit order (`VITA3K_TEX16`) and ETC1 is decoded (`ETC1_DECODE`). It also counts texture memory for the debug log. Its table is placed *before* the template's, so its entries take priority.
- **[source/game.c](source/game.c):** plays the part of `GlTest2Renderer`. It allocates the three direct `ByteBuffer`s and the touch table, runs `initCricket`/`NativeInit`, and calls `NativePreUpdate`/`Update`/`Draw`/`PostUpdate` each frame, paced like `onDrawFrame` (the game's frame interval, 60 Hz at most). Offline HTTP requests fail the way Android reports a connection error. It also patches FalsoJNI: direct buffers, which FalsoJNI lacks, and receiver tracking for instance calls, which FalsoJNI drops.
- **[source/java.c](source/java.c):** about 100 of the Java methods native code can call. File loading, saves, display size, timing, `String(byte[], charset)`/`getBytes`, `android.os.Build` and `AudioTrack` constants for Cricket, `PngFileLoad` (libpng), the text-entry calls, and offline stubs for store, network and platform services.
- **[source/text.c](source/text.c):** the Android Canvas text API (`TextInit`/`TextDraw`/`TextEnd`, measurements) with the Vita's system fonts (`ScePvf`), drawing into texture 2234.
- **[source/input.c](source/input.c):** front touch into the game's touch table, buttons and sticks into its gamepad bitmask. The game treats the buttons as a physical controller: pressing one hides the touch pad.
- **[source/buttons.c](source/buttons.c):** which Vita button does which action. It follows the touch layout type picked in the game's Button configuration (by hooking `AppMain::Set_PadSetting`), and reads `controls.txt` (see Controls).
- **[source/ime.c](source/ime.c):** text entry (nickname) through the Vita IME dialog.
- **[source/reimpl/sl_vita.c](source/reimpl/sl_vita.c):** a minimal OpenSL ES (engine, output mix, PCM buffer-queue player) on `sceAudioOut` for Cricket Audio, because VitaSDK's `opensles` library never fires buffer-queue callbacks. It plays through a **BGM** audio port, because on real hardware the MAIN port made the music sound muffled. A feeder thread (priority 80) runs Cricket's callbacks and fills an 85 ms ring; an output thread (priority 64) drains it into `sceAudioOut`. Both run above the game's main thread (160). [source/patch.c](source/patch.c) also raises Cricket's mix period from 5 to 20 ms by hooking `CkInit`, because the Vita's CPU couldn't keep up with 5 ms passes.
- **[source/java_stubs.h](source/java_stubs.h):** generated by `tools/gen_java_stubs.py`. Every remaining Java method is registered, and logs its name when called.
- **[source/patch.c](source/patch.c):** offline boot (skip the licence web view and online login), Google Play Games disabled, and offline nickname registration, as small patches that check the original instructions first. See [docs/libGltest2jni.md](../docs/libGltest2jni.md).
- **`dlsym`:** replaced, because the template's returns the table slot address instead of the symbol. Cricket Audio gets `slCreateEngine` from `sl_vita.c` and the `SL_IID_*` constants from VitaSDK's `opensles`.
- **[source/reimpl/bigalloc.c](source/reimpl/bigalloc.c):** allocations of 1 MB or more get their own memory blocks instead of fragmenting the newlib heap (the game ran out of memory without it). The heap is 184 MB (`main.c`), and `vglInitExtended` leaves 72 MB more free for big blocks (`utils/glutil.c`).
- **Character-select portraits at half size** ([source/java.c](source/java.c), [source/reimpl/sf4_compat.c](source/reimpl/sf4_compat.c)): the 1024×1024 portraits are uploaded at 512×512 (the game still sees 1024×1024; GL texture coordinates are relative). With `half/<name>.half` copies made on the PC, `FileLoad` hands the game a stand-in `.obm`, `uncompress` writes only its header, and the prepared pixels are uploaded; without them the pixels are halved at upload (slow on the Vita). With the full roster this keeps the character select within memory.
- **Missing character portraits:** `FileLoad` returns a transparent 8×8 `.obm` for a missing `*_sel_chara.obm` (the Champion Pack's downloadable characters). Otherwise the game makes a 4 MB texture for each of them anyway, and vitaGL runs out of memory at the VS screen (see RESEARCH.md).
- **`struct stat`:** the template's bionic layout was wrong (packed, newlib-sized fields), so `st_size` was read from the wrong offset. Fixed in [source/reimpl/io.h](source/reimpl/io.h), with static asserts; `lstat` converts too.
- **Faster loading:** `java.c` reads files whole with `sceIo` (not stdio), the template's FIOS2 RAM cache is off (`FIOS_CACHE`), and the game's `uncompress` calls go to [libdeflate](https://github.com/ebiggers/libdeflate) (submodule `lib/libdeflate`, MIT; zlib as the fallback). Debug builds log a load-time breakdown every 5 s.
- **`__sce_headroom`:** the link reserves 16 KB after the code segment so `vita-elf-create` always has room for its module data (without it, some code sizes fail with "segment 1 overlaps").
- **`kuser_patch` is disabled:** neither library uses the Linux kuser helpers.
- **Startup checks:** the loader refuses to start without `libshacccg.suprx`, instead of crashing inside vitaGL.
- **vitaGL:** linked from VitaSDK's package by default, because the template's pinned submodule build renders nothing in Vita3K (see `SDK_VITAGL`).
- **Build fixes:**
  - `_getentropy_r` is provided (the softfp newlib lacks it).
  - `lib/libc_bridge` declares its stub as a CMake `BYPRODUCTS`, so the Ninja generator works.
  - The `KUBRIDGE` option compiles a patched copy of so_util; the submodule itself is unchanged.

## Credits and licence

This code started as soloader-boilerplate, with contributions from TheFloW (original so-loader, gtasa_vita), Rinnegatamante (vitaGL, so_util, many ports), Volodymyr Atamanenko (solobop, FalsoJNI) and fgsfds. The template code is MIT licensed ([LICENSE](LICENSE)). The submodules keep their own licences: so_util (MIT), FalsoJNI (MIT), vitaGL (LGPL-3.0), libdeflate (MIT, by Eric Biggers).
