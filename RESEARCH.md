# Research checklist

Status key: `[ ]` open · `[~]` in progress · `[x]` answered (write the answer and source underneath)

## Milestones

Milestones 1–5d were reached in Vita3K (2026-10-15 to 2026-10-18). Milestone 6, real hardware, followed on 2026-10-18 to 2026-10-19.

| # | Milestone | Details |
|---|---|---|
| 1 | Both `.so` files load and link, 0 unresolved imports; `JNI_OnLoad` runs | §6 |
| 2 | `initCricket` + `NativeInit` run, the frame loop runs, saves and assets load | §6 |
| 3 | Graphics reach the screen (fix: VitaSDK's vitaGL instead of the template's pinned one) | §4 |
| 4 | Text, input and offline-boot patches: **the title screen** | §4, [docs/libGltest2jni.md](docs/libGltest2jni.md#boot-flow-and-the-loaders-patches-10803) |
| 5 | Menus (2026-10-17): nickname entry (Vita IME), offline nickname patch, main menu, Solo Play, Arcade options, character select. `PngFileLoad`, BGM bank loading, offline HTTP failures | see "Findings 2026-10-17" below |
| 5b | **Arcade fights play** (2026-10-17): correct speed (frame pacing), sound (own OpenSL ES), correct sprite colours (vitaGL 16-bit texture fix). Checked by the user | see "Findings 2026-10-17" below |
| 5c | Controller mode: every action on a Vita button, touch pad hidden while buttons are used (user-confirmed) | see "Battle controls" below |
| 5d | Vita buttons follow the game's touch layout type (A/B/C); `controls.txt` remapping (2026-10-18) | see "Touch layout types" below |
| 5e | Store-locked modes and characters: answered, they are the paid Champion Pack; the port won't bypass it | see "Locked characters" below |
| 6 | **Real hardware** (2026-10-18/19): menus, Arcade and fights run end to end with correct graphics; memory fits; music fixed (BGM port). User: stability good | see "Findings 2026-10-18" and "2026-10-19" below |
| 7 | *Next:* polish and release (load times, menu speed, splash screens, Release build) | see "Next steps" |

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
- **Battle controls (`AppMain::GamePadInit`, `GamePad::SetKey`):** mobile SF4 has four attack buttons, not six: P, K, SP and F. Super and Ultra are fired by tapping the Super gauge and the Revenge icon (invisible hit areas). `GamePadInit` creates each touch button together with the controller key bit that also presses it (bits as in `input.c`): P ↔ □ (128), K ↔ ✕ (32), SP ↔ △ (256), F ↔ ○ (64), Ultra ↔ L (512) or "R2" (4096), Super ↔ R (1024) or "L2" (2048), pause ↔ Start (0x10000). So the existing button map already reaches every action; the L2/R2 duplicates aren't needed on the Vita.
- **Touch overlay vs controller:** with a controller "connected" (`AndroidGCController` +1, set from our gamepad stubs), `GamePad::SetKey` switches to controller mode when any mapped button is pressed: the flag at `GamePad` +0x2E1 is set, `isShowGameController()` becomes true and `ST_GameMainDraw` draws a gamepad icon instead of the touch pad. Tapping that icon switches back to touch. "Connected" is rebuilt every frame in `AndroidGCController::updateKeyData()`: cleared when Java's `getInitFrg_GamceController()` is true (Android sets it in `onInputDeviceRemoved`, i.e. "controller unplugged"), set while any key bit is down. The loader used to answer `true`, so the pad came back as soon as the button was released; it now answers `false`. The Options › Button configuration screen only sets the touch pad's size, opacity and stick position; the game has no controller remapping.
- **Locked characters are a paid in-app purchase (2026-10-18).** With the full character data present, the extra characters still show as locked. The only code that unlocks content is `AppMain::GT_ShopChampionPackDialog` (the Store's "Champion Pack", via Google Play Billing), which saves the unlock with `ContentDataControl::ContentUnlockSave` (into the game's save data, through `LocalFileSave`). Characters don't unlock through play. The port won't fake or bypass that purchase. The legitimate route to the full roster: buy the Champion Pack in the official app on Android, then (unverified) carry your **own** unlocked save files and the downloaded `demand_assets` pack over. Unofficial "MOD" APKs checked on 2026-10-18 were arm64-only, so unusable anyway, and aren't supported.
- **Touch layout types (2026-10-18):** Options › Button configuration has TYPE A (PlayStation-style diamond), B and C (Nintendo-style 2×2 grids) and custom. They only move the touch buttons. The game stores positions, not the type: `AppMain::Set_PadSetting(type, flag)` (0–2 = A–C, 3 = custom; called by `GT_TypePanel` when TYPE is tapped) copies a preset, and `Check_PadType()` recovers the type by comparing positions. It only works inside the settings screen (it reads `GamePad::GetButton(0..3)` pointers that `PadSettingInit()` stores at `AppMain` +0x2D08), and even there the default layout shows as CUSTOM in Vita3K. So `buttons.c` hooks `Set_PadSetting`, remembers the last A/B/C pick in `save/padtype.txt`, and maps the Vita buttons to match (B: P = ✕, K = ○, SP = △, F = □; C: P = □, K = △, SP = ✕, F = ○; Nintendo letter positions). `ux0:data/sf4/controls.txt` (template written on first run) can fix the preset or remap any action. User-confirmed in Vita3K: the buttons follow Type A/B/C changes and the choice survives a restart.
- Test hooks: `-DAUTO_TAP=x,y,ms[,x,y,ms...]` (a sequence of one-off taps, timed from the first frame) and `-DAUTO_IME=text`.

## Findings 2026-10-18 (first real-hardware test)

- **Crash at start-up, before any game code:** prefetch abort with PC = 0, from `so_load_internal` calling `kuKernelMemProtect` while loading `libc++_shared.so` (core dump read with vita-parse-core and the matching ELF; the rebuilt `eboot.bin` was byte-identical to the tested one). The user's kubridge is an older version without `kuKernelMemProtect`, so the import resolved to 0. The original 2020 kubridge only has the basic calls; the bythos14 releases add `kuKernelMemProtect`, which this template needs. The loader's start-up check only tests that *a* kubridge is loaded. **To do:** check for the needed functions and show a clear "update kubridge" message. Waiting for a retest with the latest bythos14 kubridge.
- **After updating kubridge (latest bythos14 release), the game runs on a real Vita:** title, nickname entry with the Vita keyboard, menus, Arcade options. Then it crashed on the loading screen into character select. Reproduced in Vita3K. Cause: **out of memory**, `std::bad_alloc` thrown by the game's `operator new` (`abort` via `exit_soloader`; message in Vita3K's log).
  - The game allocates big temporary buffers as scenes load (`COglCore::CreateTexLoadBuffer`: 32 MB; others of 16, 10 and many of 4 MB) and frees them again. On top of that, **vitaGL puts textures in the newlib heap when its own pools are full** (`VGL_MEM_EXTERNAL` = the newlib heap; pools in Vita3K: VRAM 112 MB, RAM 74 MB, phycont 26 MB; on hardware VRAM is 96 MB). Measured with the old single 256 MB heap: at most ~174 MB in use, but the heap had grown to 251 MB from fragmentation, and the next big `new` failed. Visiting more menus first (as the user did) makes it worse.
  - Not the cause: FalsoJNI's `DeleteLocalRef` is a no-op, but the heap doesn't grow while idle (the game calls it ~3,000 times; strings are small).
  - **Fix:** `reimpl/bigalloc.c`. Game and libc++ allocations of 1 MB or more (malloc/calloc/realloc/memalign/free imports) get their own kernel memory blocks, which leave no holes when freed. Our own big conversion buffers use it too. Memory split: newlib heap 184 MB (small objects plus vitaGL's overflow) and 72 MB more left free by `vglInitExtended` for big blocks; vitaGL's pools are unchanged. A 128 MB heap was too small: vitaGL's overflow alone filled it, and frames took 4 s each while it waited for memory.
  - Result in Vita3K: the user's path (menus → Other › Options → back → Solo Play → Arcade → OK) reaches character select and a fight, with no allocation failures. Fight: heap 78 MB in use (153 MB grown), big blocks 16 MB, peak 59 MB.
  - **Resolved later the same day** (missing portraits, below): loading *into* character select stalled for about 8 s (two 4 s frames) while the heap peaks near its limit. Likely vitaGL waiting for memory; worth reducing texture memory (e.g. what the character select loads, and vitaGL's delayed freeing during long loading frames). To check on hardware.
  - Debug builds log `memory: heap <in use>/<grown to> KB, big blocks <now> KB (peak …)` every 5 s.
- **Real Vita, first end-to-end run (2026-10-18, user):** title → menus → Arcade → character select → fight all work. Issues seen (screenshots kept by the user, not in git): (1) slightly garbled audio; (2) long load times; (3) slow menu frame rate and noticeable tap delay; (4) 90+ s to load character select, with a broken background; (5) garbled world map on the VS screen (reusing other images); (6) in fights, audio and frame rate are good but the sprites are semi-transparent and broken.
  - **(4)–(6) fixed:** the 16-bit RGBA re-pack (the "vitaGL bug" of 2026-10-17) is really a **Vita3K bug**. Vita3K reads GXM `U5U5U5U1`/`U4U4U4U4` with a different bit order from the real GPU, so the re-pack is now Vita3K-only (`VITA3K_TEX16`, default with `KUBRIDGE=OFF`). User-confirmed on the Vita: sprites, character select and VS screen correct; loading faster. **Lesson: check emulator-only fixes on hardware before trusting them.**
  - Still open: (1) audio garbled outside fights and slightly in fights; (3) menu frame rate and tap delay.
  - **(1) audio, diagnosis and fix (2026-10-18; to confirm on the Vita):**
    - Checked in Vita3K: no ring underruns in `sl_vita.c`, but Cricket kept warning "Audio rendering is taking too long (6–18 ms); some sounds will not be played" and growing its task buffer ("Reallocating audio task buffer"). The garbling is therefore Cricket **dropping voices**, not the output running dry.
    - A temporary log in `bq_Enqueue` showed Cricket enqueues **960-byte buffers = 240 frames = 5 ms** at 48 kHz (stereo 16-bit); its first, silent buffer is 28,800 bytes.
    - The period comes from `CkConfig`. `_CkConfig::_CkConfig(JNIEnv*, jobject)` (0x80c444 in 1.08.03) fills in the defaults; `Java_..._initCricket` builds one on its stack and passes it unchanged to `CkInit` (exported). Fields we rely on: **+32 `audioUpdateMs` = 5.0f**, +44 max audio tasks = 500 (int), +48 `maxRenderLoad` = 0.8f. Other fields: +24 = 7, +36 = 500.0f, +40 = 100.0f, +52 = 2 (meaning not confirmed). The names follow Cricket's public `CkConfig` docs; the offsets are from the disassembly.
    - Each pass costs a fixed overhead whatever its length, and the menus play many short sounds at once, so 5 ms passes don't fit on a 444 MHz Cortex-A9. That explains why the menus sounded worse than fights.
    - **Fix:** `patch.c` hooks `CkInit`, checks the two defaults are as expected (5.0f, 500) and sets `audioUpdateMs` = 20 ms and max tasks = 1000 (Cricket grew it to 1000 by itself anyway). If the defaults differ, it logs an error and changes nothing. Vita3K: the hook fires, Cricket's warnings and task-buffer growth are gone, no underruns. The cost is about 15 ms more audio latency.
    - Also changed: the `sl_vita.c` ring went from 43 to 85 ms (4096 frames); the feeder thread runs at priority 80 and the output thread at 64, above the game's main thread (160), so a long game frame can't starve audio; per-call text logging (hundreds of lines a second in menus, written to `log.txt` on the memory card) is now behind `-DTEXT_TRACE=ON`, which may also help (3).
    - If audio is still garbled on the Vita: try 40 ms, or lower `maxRenderLoad`; check the `log.txt` lines from `sl_vita` and Cricket.
    - **User retest with the 20 ms build:** better, "still a little bit of crackling", and long loads; and the **VS screen was garbled again** (log from the device, kept by the user). The log has one Cricket "taking too long" warning and no underruns, but loading frames of 4.2–4.7 s.
  - **VS screen garbled again, and the 4 s stalls: out of texture memory (2026-10-18).**
    - From the device log: the newlib heap reached its full 184 MB during the character select (108 MB in use, the rest in holes), followed by frames of exactly 4.2–4.7 s.
    - vitaGL's allocator (`gpu_utils.c`): when VRAM, RAM, phycont and the newlib heap all fail, it runs the garbage collector and sleeps 1 s, up to `FRAME_PURGE_FREQ` = 4 times, and then **leaves the texture without storage**. That is the 4 s stall, and the garbled VS-screen textures. Whether it happens depends on how much was loaded before, which is why the VS screen looked right in one hardware run and not in the next.
    - New 5 s log line `textures: <KB> in <n> (peak …)`: level-0 sizes as uploaded, minus `glDeleteTextures` (`sf4_compat.c`). Vita3K, title → Arcade → fight: 32 MB at the title, 120 MB in the menus, **205 MB in just 58 textures at the VS screen**, 129 MB in a fight.
    - Cause: the character select loads `<char>_sel_chara.obm` (the big portrait) for all 34 characters. 28 of them are the Champion Pack's downloadable data, not in the APK. For each missing one, `COglCore::NEW_loadDirectObmData` still creates a 1024×1024 RGBA texture (4 MB) from its decode buffer, which holds the previous portrait. That is 112 MB nobody draws: locked characters show a silhouette icon from another texture. Returning a placeholder in the data buffer while `FileLoad` still returned 0 changed nothing, because the game decodes from its own buffer.
    - **Fix (`java.c`, `blank_obm`):** for a missing `*_sel_chara.obm`, `FileLoad` returns a transparent 8×8 RGBA `.obm` (8-byte header + 256 bytes). Vita3K: textures at the VS screen **205 → 90 MB**, the heap peak 173 → 87 MB, and the character select and VS screen look as before (screenshots). Only those files are faked: other missing files still return 0. Side effect: highlighting a locked character shows no big portrait, where it showed the previous character's before; tapping one still opens the store dialog. To confirm on the Vita.
    - Also noticed: the menus keep two 2208×1840 RGBA backgrounds (`help_bg_iPad.obm`, `config_bg_iPad.obm`, 16 MB each) loaded the whole time. A candidate for later savings.
    - **First hardware test of this build crashed at start-up (3 identical core dumps, 2026-10-18):** a data abort in `Cki::OpenSLES::init` right after `dlsym("SL_IID_MIDITEMPO")` returned NULL. My own mistake: removing temporary debug lines by searching for "TEMP" also deleted the `SL_IID_MIDITEMPO` table entry. Restored. Vita3K didn't fault on the NULL read, so the emulator runs passed. **Lesson:** a missing `dlsym` symbol shows up only on hardware; check `log.txt` for "dlsym: unknown symbol" after table changes.
    - **Hardware retest of the fixed build (user, 2026-10-18):** plays fairly smoothly, loads much better, **no graphical issues** (VS screen correct). Audio still a little crackly. Log: no `sl_vita` underruns and one Cricket "too long" warning (Cricket may print it only once). Pools on the Vita: VRAM 96 MB (not 112), RAM 72 MB, phycont 26 MB. At the title, 32 MB of textures already leave only 27 MB of VRAM free, so about 40 MB of VRAM goes to other things (framebuffers with 4× MSAA, vitaGL buffers). In a fight all three pools are full and textures spill into the heap (85 MB in use).
    - New 5 s log line from `sl_vita.c`: `audio: render avg/max us, ring min frames, clipped samples`. Vita3K, title → fight: 50 renders/s (20 ms each), average 0.1–0.5 ms, max 13 ms (budget 16 ms), ring never below 3,300 of 4,096 frames after start-up, occasional clipped samples (≤ 96 per 5 s). Hardware numbers pending.
    - New log line `vitaGL free KB: vram/ram/phycont` (free/total per pool). Vita3K returns nonsense for it (`sceClibMspaceMallocStats` isn't emulated); on hardware it's correct.
    - Candidates for later: turn off 4× MSAA (a 2D game; frees VRAM and GPU time); unload the 16 MB menu backgrounds.

## Findings 2026-10-19 (muffled music on hardware)

- **Symptom (user):** on the Vita, the music sounded muffled with slight crackle, through the speaker and through headphones; sound effects were clean. Vita3K was clean.
- **Not CPU or timing.** Hardware `audio:` lines: Cricket renders take 50–500 µs per 20 ms, the ring never drops below 3,616 of 4,096 frames, no underruns. The one "taking too long" warning prints a garbage negative time (18446744027136 ms), a timer glitch, not a slow render.
- **Formats:** music banks (`bgm_*.ckb`) hold ADPCM stereo at 22,050 Hz; effect banks (`system.ckb`, `chr_*.ckb`, ...) hold 8-bit PCM mono at 22,050 Hz. Cricket's output is L = R exactly, even for the music.
- **The samples were clean on hardware.** New debug option `AUDIO_DUMP=skip,seconds` records the PCM going to `sceAudioOutOutput` to `ux0:data/sf4/audio_dump.raw` (stdio `fopen`/`fwrite` left no file on the Vita, so it uses `sceIo` with checks). A dump of Cricket's buffers from the Vita plays cleanly on a PC (user). A second dump, taken at the port input after our ring buffer, analyses the same: same spectrum as a Vita3K dump, no dropouts. So the muffling happened **after** `sceAudioOutOutput`.
- **Cause and fix: the `MAIN` audio port.** An A/B build (L+R+Select stepping through output settings, since removed) tried MAIN 0 dB, MAIN −6 dB, BGM port, MAIN with ALC off, MAIN with ALC on. User, on hardware: **only the BGM port is clean**; every MAIN setting is muffled, so it's neither level nor ALC. What the system does to MAIN-port audio is unknown. `sl_vita.c` now always opens a BGM port (it accepts 48 kHz). **Verified by the user on hardware with the final build**, and Vita3K sounds good with it too.
- Clipping is real but rare: 82–89 full-scale samples in 60 s, all in loud fight moments. NEON conversion saturates (no wrap-around found). Cricket's scalar `convertF32toI16_default` would wrap (it stores the low 16 bits of a `vcvt.s32`), but it isn't used here.
- **Reading Vita core dumps:** `vita-parse-core` (xyzz) is Python 2-era. A Python 3 fixed copy is in WSL at `~/tools/vita-parse-core-py3/` (fixes in `util.py` and `elf.py`). Run it in WSL with `PATH=~/vitasdk/bin:$PATH PYTHONPATH=~/tools/pylib python3 ~/tools/vita-parse-core-py3/main.py <dump> <elf>` (pyelftools is copied to `~/tools/pylib` because WSL has no pip). It needs the unstripped ELF of the exact build. WSL's `/tmp` is wiped between calls, so copy `/tmp/loader/build/sf4_vita` out in the same command as the build.

## Findings 2026-10-20 (review: load times and splash screens)

- **User playtest:** happy with stability. Two requests: faster loads, and no "android"/vitaGL splash screens at start-up.
- **Load times, from the 2026-10-19 hardware `log.txt`:** loading frames take **230–460 ms of game work each** (2–4 fps), so the time is spent inside the frame, not in frame pacing. Each 5 s window loads only ~5–10 MB of files (~1–2 MB/s, far below what the memory card can read), so raw file I/O is probably not the bottleneck (unconfirmed until timed). Suspects:
  - zlib-packed `.obm` files decoded by the game on a 444 MHz A9 (e.g. `help_bg_iPad.obm`: 0.95 MB on disk, a 16 MB texture)
  - vitaGL copying textures into its (uncached) pools
  - VRAM full during loads (`vram` 0.4–0.5 MB free), so textures spill into the newlib heap
  - files loaded twice in a row (`ms_anime2_*`; `FileLoad`'s cache only remembers the last name)
  - In Debug builds every log line is an unbuffered `sceIoWrite` to the memory card (about 300 lines per run, so probably minor)
- **The "android" screen** is the template's placeholder `loader/extras/livearea/pic0.png`, which the Vita shows while the app starts. Replaced by the user's artwork when it exists (see "App artwork").
- **The spinning vitaGL logo** is compiled into VitaSDK's vitaGL package (r1488: `splashscreen.o`, `invoke_splashscreen` called from `vglInitWithCustomSizes`). It's cleared on the first `vglSwapBuffers` (`clear_splashscreen` in `gxm.o`), so it covers `initCricket`/`NativeInit` rather than adding time (from the disassembly, not timed). Removing it leaves a black screen for that time. Options: build vitaGL ourselves from the same revision with `NO_SPLASHSCREEN=1` (its other build flags must match the package, e.g. the shader cache), or draw our own loading screen. vitaGL's licence doesn't require the splash (`NO_SPLASHSCREEN` is an official option).
- **Hardware timings (user, 2026-10-20; `log_msaa.txt`/`log_nomsaa.txt`, same route, kept in `game/`):** in every loading window, **reading files is the biggest part, ~55–90% of game work**, at only **3–4 MB/s** (e.g. 4,736 ms for 17 MB; 2,853 ms for 10 MB). Next come zlib at ~30 MB/s of output (e.g. 1,734 ms for 51 MB) and, in the character-select/fight loads, **our palette expansion** (textures 450–935 ms per 5 s, of which only 58–100 ms is inside vitaGL). PNGs: 2.9 s for 8 files on the way into the menus. vitaGL's own upload time is small (≤ 181 ms per window), so `vglUseCachedMem` isn't worth trying yet. The Vita3K start-up pattern (mostly "other") didn't carry over: on the Vita, start-up is files 2.4 s of 3.1 s.
  - **MSAA off:** loads unchanged (as expected), fights still 20 fps, **+20 MB of free VRAM** at the title (27 → 47 MB) and in the menus. VRAM still fills up at the VS screen and in fights. Image quality: the user's verdict is still needed.
  - **Fixes (2026-10-20, to check on hardware, `sf4_vita_hw_fastload.vpk`):**
    - `java.c` reads files with `sceIoOpen`/`sceIoRead`, whole, instead of newlib's `fopen`/`fread`. The stdio path probably made many small reads; that cause isn't confirmed, but the cost is.
    - It skips the failed `assets/External/` open on every `FileLoad` when that folder doesn't exist (`extract_game.py` doesn't make it).
    - PNG premultiply without division (the Cortex-A9 has no divide instruction; the new formula was checked exact for all 65,536 inputs).
    - `depalettize` has a 16-bit fast path, a table lookup two pixels per byte instead of a run-time-size `memcpy` per pixel (checked identical on random data, odd sizes included).
    - Vita3K: the title loads and renders as before.
  - Next candidates if loads are still slow: zlib (a faster inflate such as libdeflate for `uncompress`, if one builds for the Vita), then "other".
  - **Result of the fixes (user, `log_fastload.txt`, same route):** game work in loading windows 57.5 → **49.5 s**. Files 34.9 → 23.9 s (3.2 → 5.0 MB/s); textures 3.1 → 2.4 s; zlib unchanged at 9.1 s. PNGs unchanged at 3.2 s, so their cost is libpng's decoding, not the premultiply. A fit over the 5 s windows gives **~23 ms per file opened + ~138 ms per MB (~7 MB/s)**: both slow. MSAA off approved by the user (no visible difference): now the default.
  - **Suspect: the template's FIOS2 RAM cache.** `utils/init.c` calls `fios_init(DATA_PATH)` (`lib/fios`, from gtasa_vita), which puts a `sceFiosIOFilterCache` with 64 × 128 KB blocks over all of `ux0:data/sf4/`. If `sceIo` goes through it (unconfirmed), every whole-file read is cut into 128 KB block reads and copied through the cache. It helped games that read in small pieces. Test: `-DFIOS_CACHE=OFF` skips FIOS (build `sf4_vita_hw_nofios.vpk`). Watch whether Cricket's music, which streams `.ckb` banks with small `fread`s, still plays cleanly.
  - **Result (user, `log_nofios.txt`, same route):** files 23.9 → **17.0 s**, and the raw read speed from the fit doubled (138 → 67 ms per MB, ~15 MB/s). All loading 49.5 → **42.6 s** (57.5 s before any fixes). Music clean (user), no underruns in the log. **`FIOS_CACHE` now defaults to OFF.** The user's card is an SD2Vita.
    - Still to explain: windows with many files (the stage/fight loads, 40–61 files) cost ~30 ms extra per file, while single files open fast (two save files 26 ms, an 86 KB asset 9 ms). Not the missing files: only the 28 portraits miss, all in one window.
    - zlib: the game calls only `uncompress` (6 call sites, no `inflate`; `plt_calls.py`), so a faster whole-buffer decompressor can replace all of it.
  - **libdeflate (2026-10-20, to check on hardware, `sf4_vita_hw_deflate.vpk`):** `uncompress_sf4` now uses libdeflate v1.24 (submodule `loader/lib/libdeflate`, MIT, decompression files only, always `-O2`), with zlib as the fallback on any error (logged once). Vita3K: identical output (9 calls, 18,240 KB at the title), 56–58 ms against 103–105 ms with zlib; that's only an indication. Estimate for the Vita: zlib's 9.1 s → 3.5–6 s, all loading 42.6 → ~37–39.5 s. `FileLoad` log lines now include each file's time in ms, to explain the per-file cost in the stage/fight loads.
  - **Result (user, `log_deflate.txt`, same route):** no graphical issues; "loading is quick apart from when the fight loads". zlib 9.1 → **6.8 s** (25 → 33 MB/s), less than estimated. Files 23.7 s against 17.0 s in the previous run, with the same settings, so file time varies between runs. Per-file times explain the fight load: from the VS screen to the fight the game reads **179 files (55 MB) in 15 s**. At the ~12 MB/s big files reach that would be ~5 s, so **~10 s is a fixed cost per file**. Small files cost 24–48 ms each in the menus but ~110 ms for Ryu's sprites and ~50 ms for Chun-Li's (identical 262 KB files, loaded one after the other), creeping up steadily within each run. Suspects: opens in the 1,133-file `assets/` folder; Debug logging (one unbuffered `sceIoWrite` per line to the same SD card, a line per file); something else reading at the same time (music streaming during the VS screen). Next: an I/O micro-benchmark at start-up (`IO_BENCH`).
  - **Link fix:** adding libdeflate made `vita-elf-create` fail ("Cannot allocate 5116 bytes for SCE data at end of segment 0; segment 1 overlaps"). It needs ~5 KB free after the code segment, and the data segment starts at the next 64 KB boundary, so any build whose code ends within ~5 KB of a boundary fails (roughly 1 in 13 sizes). VitaSDK's linker script reserves `__sce_headroom` bytes there; CMake now passes `-Wl,--defsym=__sce_headroom=0x4000`.
- **Shader cache:** the SDK vitaGL contains `ux0:data/shader_cache/v%d/...` paths and `vglSetShaderCachePath`, so compiled fixed-function shaders are probably cached after the first run (unconfirmed on the device). The first boot may load slower than later ones.

## App artwork (LiveArea) — options, 2026-10-18

The VPK's `sce_sys/` images come from `loader/extras/livearea/`. They are still the template's Android placeholders (robot icon, blue background, "android" splash). Requirements: exact sizes, and 8-bit palette PNGs (≤256 colours; the template's files are palette PNGs, and full-colour PNGs can make installs fail):

| File | Size | Shown as |
|---|---|---|
| `icon0.png` | 128×128 | the home-screen bubble |
| `bg0.png` | 840×500 | the LiveArea background |
| `startup.png` | 280×158 | the LiveArea "Start" gate |
| `pic0.png` | 960×544 | the start-up image (to confirm on the device) |
| `template.xml` | | LiveArea layout (`a1` style: background + gate) |

Capcom's art (the SF4 logo, characters, the app icon) can't be in the repo or a public build. The user's own APK has it: `res/mipmap-*/icon.png` 192×192, adaptive-icon layers 432×432, and in-game `title_bg.obm`/`logo.obm`, which the loader already decodes. Options:

1. **Original artwork in the repo** for every build. **The user will make this** (see Next steps).
2. **Personalised artwork from the player's own copy:** e.g. `tools/make_livearea.py <your .apkm>`, which takes the official icon and title art, resizes and palette-converts them, and puts them into the player's local VPK. Nothing copyrighted in git, like `extract_game.py`. **Done (2026-10-20):** see below.
3. **Both:** 1 as the release default, 2 as an opt-in.
4. **A loading splash in the loader:** cover the few black seconds while the libraries load, with `pic0`-style art or the title art from the player's assets at runtime. This could also replace vitaGL's logo (Findings 2026-10-20).

**Option 2, implemented (2026-10-20, user-approved preview):** `tools/make_livearea.py <apkm> [out_dir]` (default `game/livearea/`, git-ignored) reads `base.apk` only:
- `icon0`: the adaptive launcher icon layers (`res/mipmap-xxxhdpi-v4/ic_launcher_{background,foreground}.png`, 432×432), combined and cropped to the middle 288 px
- `pic0`: the title screen rebuilt from `assets/title_bg.obm`, a 1024×1024 RGBA atlas holding the ink background (0,0)–(1024,640) and the logo (9,644)–(681,1022), plus `assets/CP.png` ("© CAPCOM")
- `bg0`: the ink background, the logo, and Ryu from the icon's foreground
- `startup`: the logo on darkened ink

The build picks them up with `-DLIVEAREA_DIR=<dir>` (default: the template's placeholders in `loader/extras/livearea/`). The VPK installs and boots in Vita3K. To check on the Vita: that it installs (palette PNGs), and where the gate sits over `bg0`. The Vita may cache LiveArea images, so delete the old install first. Also seen: the iOS launch images (`Default-*.png`) left in the assets are blank, and `banner.png` is a Japanese online-mode banner, so neither was useful.

## Next steps

The remaining to-do list, in rough order (updated 2026-10-20). The game runs end to end on a real Vita and the user is happy with stability, so what's left is polish and release. Hardware setup: [loader/README.md](loader/README.md#testing-on-a-real-vita). Debug builds log to `ux0:data/sf4/log.txt`.

Working notes: in Vita3K a mouse click is a front-panel tap, and Vita3K's keyboard mapping drives the buttons. Don't tap **Delete Account**. Every unimplemented Java call logs `Java method X() called` (`loader/source/java_stubs.h`).

### Polish (before release)

- [ ] **1. Faster loads** (Findings 2026-10-20).
  - [x] Measure first (hardware results and fixes: Findings 2026-10-20): add timers for file reading (`FileLoad`, `java.c`), texture uploads including our conversions (`sf4_compat.c`), and the rest of each loading frame, to the 5 s log. One hardware run shows which stage dominates.
    - Done (2026-10-20): Debug builds log `load: files … zlib … png … textures … (in vitaGL …), other …, of … game work; longest frame …` every 5 s while anything loads (`prof_add`, game.h). The game's `uncompress`/`inflate` imports are wrapped to time decompression. In Vita3K at start-up: files 62 ms, zlib 105 ms (18 MB out), png 53 ms, textures 15 ms, **other 1,132 ms** of 1,370 ms, nearly all in one 1.1 s frame. "Other" covers the game's own code and whatever vitaGL does while drawing (e.g. compiling shaders on first use). **Waiting for hardware logs:** `sf4_vita_hw_timers.vpk` and `sf4_vita_hw_timers_nomsaa.vpk` (`-DMSAA=OFF`, item 3), same route in each.
  - [ ] Then A/B on the Vita: no MSAA (item 3); `vglUseCachedMem(GL_TRUE)` before `vglInitExtended` (faster CPU writes into vitaGL's pools; expected, not measured); a Release build; unloading the two 16 MB menu backgrounds (`help_bg_iPad.obm`, `config_bg_iPad.obm`); keeping recently read files so back-to-back reloads (`ms_anime2_*`) skip the card.
  - [ ] Check whether `ux0:data/shader_cache/` exists on the Vita after a run (whether the first boot is slower than later ones). In Vita3K vitaGL does read `ux0:data/shader_cache/v31/...` (2026-10-20).
- [ ] **2. Menu frame rate and tap delay.** Not investigated yet. Measure first (the `frames:` line in menus, and how many frames pass between a touch and the game reacting).
- [x] **3. Drop 4× MSAA** (done 2026-10-20: user saw no difference; +20 MB VRAM; `MSAA` option, default OFF) (`utils/glutil.c`): the Vita gives vitaGL only 96 MB of VRAM, ~40 MB is gone before any texture loads, and VRAM is full during loads. A 2D game shouldn't need it; it should also save GPU time. Check sprite edges on hardware afterwards.
- [ ] **4. Start-up splash screens** (Findings 2026-10-20).
  - [~] Replace the template's "android" `pic0.png`. Personal builds: done with `tools/make_livearea.py` (art from the player's own APK; "App artwork" above). Waiting for a hardware check. Public builds still use the placeholders until the user's artwork (item 6) exists.
  - [ ] vitaGL's spinning logo: draw our own loading screen instead (App artwork option 4), or link a vitaGL built with `NO_SPLASHSCREEN=1` from the same revision as the SDK package. Either way the time it covers stays; only what's on screen changes.
- [ ] **5. kubridge version check** with a clear "update kubridge" message (old kubridge lacks `kuKernelMemProtect` and crashes at start-up; Findings 2026-10-18).

### Release

- [ ] **6. (User) Original app artwork** (options in "App artwork" above): `icon0.png` 128×128 (bubble), `bg0.png` 840×500 (LiveArea background), `startup.png` 280×158 (Start gate), `pic0.png` 960×544 (start-up image). No Capcom logos or characters. Any format is fine; they get converted to 8-bit palette PNGs and placed in `loader/extras/livearea/`.
- [ ] **7. Release build** (no debug logging), tested on hardware: never run on a Vita so far.
- [ ] **8. Install instructions** in the README for players (kubridge, `libshacccg.suprx`, `extract_game.py`, VPK), from the steps in [loader/README.md](loader/README.md#testing-on-a-real-vita).
- [ ] Optional: `tools/make_livearea.py` (personalised art from the player's own APK); a smoother install (first-launch extraction of the player's `.apkm` on the device, clearer error screens, listing on VitaDB, which installs kubridge and the shader compiler for players).

### Tidy-ups (optional, each written up where it was found)

- [ ] `LanguageGet` should follow the Vita's system language
- [ ] `String(byte[], charset)` assumes UTF-8, so check Japanese text (Shift_JIS?)
- [ ] the logger mishandles `%f` (it shifts later arguments); fix it or avoid `%f`
- [ ] the `Java method deleteIndicator()` / `setPurchaseList()` stubs that are hit
- [ ] stdio `FILE` bridging (§3)
- [ ] rare clipping in loud fight moments (Findings 2026-10-19): a little headroom if it's audible
- [ ] whether the template's own vitaGL works on hardware (`SDK_VITAGL=OFF`; it fails in Vita3K). Low priority

### Extras (optional)

- [ ] the rear touch panel for Super/Ultra or pause
- [ ] a stick-driven cursor for menus (for PS TV, which has no touch screen)
- [ ] the optional `sf4_online.mp4` video (`isMovieExist` returns false for now)
- [ ] for players who bought the Champion Pack: which save file holds the unlock, whether the official app allows Android backups (to copy saves without root), and where Play stores the `demand_assets` pack. Training also opens the Store; check whether it's part of the same pack

Done: controls (button map, controller mode, layout types A/B/C, `controls.txt`), store-locked content answered (paid Champion Pack, not bypassed), real hardware (graphics, memory, audio incl. the BGM port fix).

## 0. Go / no-go blockers (answer these first)

- [x] **Which Android releases exist, and which ABIs does each ship?** Street Fighter IV Volt (2014) vs Champion Edition (2018+). Does the APK contain `lib/armeabi-v7a/`, or only `arm64-v8a`? No 32-bit ARM binary means the so_loader approach is dead and we would need a far harder route.
  - **Champion Edition (`jp.co.capcom.sf4ce`) ships `armeabi-v7a` in both 1.04.00 and 1.08.03. ✅** The main library is `libGltest2jni.so` (8.9 MB in 1.04, 8.8 MB in 1.08.03), alongside `libc++_shared.so` and Crashlytics libs (the Crashlytics ones can be ignored or stubbed). Source: our own APKMirror downloads, `unzip -l`.
  - **1.04.00** (single APK, 33 MB): code plus front-end assets only (48 files in `assets/`). The ~1,086 game assets (characters, stages, BGM, video) came from a separate Play **OBB expansion file** that we don't have. The dex contains the Google LicenseChecker/expansion downloader. On its own it is incomplete.
  - **1.08.03** (`.apkm` bundle, 389 MB): an App Bundle with an install-time asset pack, `split_install_assets.apk` (497 MB uncompressed, 1,090 files), with the base game's assets. The code lives in `base.apk` and `split_config.armeabi_v7a.apk`. **This is the version to target.** Correction (2026-10-17): it isn't fully self-contained. Fight data for all but four characters is in an on-demand `demand_assets` pack that Play downloads separately and the `.apkm` doesn't include (see Findings 2026-10-17).
  - The older Volt release (2014) was not investigated. Champion Edition 1.08.03 is enough.
  - Asset formats seen: `.ckb` (audio, likely CRI or Capcom-specific), `.obm` (textures/images), `.bmv` (video?), `.png`, and one 143 MB `sf4_online.mp4`.
- [x] **Which graphics API does it use?** GLES 2.0 is fine for vitaGL. GLES 3.x features could be a problem, and Vulkan would be a blocker.
  - **OpenGL ES 1.x fixed-function** (`libGLESv1_CM.so`, 68 GL imports, no EGL). No shaders. Textures turned out to be RGBA/RGB `.obm` data, paletted textures and some ETC1; no PVRTC upload has been seen (§4). Details: [docs/libGltest2jni.md](docs/libGltest2jni.md).
- [x] **Memory footprint.** The code itself takes ~8.7 MB once loaded (fine). Runtime heap use and asset streaming are still unknown. Size of the `.so`, size of the assets, and peak RAM on a real Android device. Does it fit in roughly 330 MB?
  - **It fits (hardware, 2026-10-18/19)**, but only just: newlib heap 184 MB + 72 MB for big blocks + vitaGL's pools (VRAM 96 MB, RAM 72 MB, phycont 26 MB). In fights all three vitaGL pools are full and textures spill into the heap. It needed `bigalloc.c` and the blank portraits for missing characters (Findings 2026-10-18).
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
  - **Answered (2026-10-15):** Cricket Audio 1.6.5 logs "Using OpenSL ES for output" once `dlopen`/`dlsym` hand it OpenSL ES symbols.
  - **Sound works (2026-10-17):** VitaSDK's `opensles` library never fired buffer-queue callbacks, so `slCreateEngine` now comes from our own `reimpl/sl_vita.c` (the `SL_IID_*` constants still come from the library). See Findings 2026-10-17.
- [~] Video: are cutscenes or FMVs played by Android MediaPlayer? (Those would need replacing or skipping.)
  - One video, `sf4_online.mp4` (143 MB), plays in its own Activity. Can be skipped or played via SceAvPlayer. Codec still to check.
- [x] Licensing/DRM, Google Play Games, IAP, ads, analytics, online play: what must be stubbed, and does the game refuse to start without them?
  - **No licence check at startup.** Netflix layer is a stub in this build. Play Games, billing, AppsFlyer, Helpshift, Firebase and HTTP all get stubbed.
- [x] Save data: format, location, and whether it goes through Java (SharedPreferences) or native code.
  - `LocalFileSave`/`LocalFileLoad` write raw blobs (≤1 MB) to app-private files through the shared buffer. Format is opaque, but we don't need to understand it.
- [ ] **stdio streams:** with `USE_SCELIBC_IO`, the template hands copies of newlib `FILE` structs (`__sF`, and our `stderr`) to SceLibc functions. Harmless if the game only logs to `stderr`; check before trusting `stdin`/`stdout`/`stderr` I/O.
- [x] Text is rendered by Android Canvas into GL texture 2234. Replaced with the Vita's system fonts through `ScePvf` (`loader/source/text.c`, §4). Japanese text is still unchecked (tidy-ups).

## 4. Graphics

- [~] GL call inventory and which extensions are queried (ETC1/ETC2/ASTC/PVRTC textures).
  - All 68 GLES1 imports are listed in [docs/imports.md](docs/imports.md). Which extensions it queries is not yet checked.
  - **vitaGL lacks `glCompressedTexSubImage2D` and `glLogicOp`.** Answered (2026-10-17): `glCompressedTexSubImage2D` is used by `COglCore::Texture_loadObmData` for paletted pieces and is now implemented for paletted and ETC1 data. `glLogicOp` is only used by the `OGLSelfHelper::GLEffectDrawStore*` sprite effects and wasn't called during several minutes of fighting; it stays a no-op.
- [x] **Why the screen was black (2026-10-15):** the template's pinned vitaGL (`lib/vitagl` @ `9c23758`) renders **nothing at all** in Vita3K. Even a bare red quad drawn by the loader, with the game never run, stayed black. VitaSDK's packaged softfp vitaGL (`r1488.g2bdbe89`) works, so the loader now links that by default (`SDK_VITAGL=ON`). Also works on hardware (2026-10-18). Ruled out on the way: face culling, the game's off-screen framebuffer, 4× MSAA.
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
  - After sitting on the title for about 15 s the screen fades to black (probably an attract/demo sequence). The menus, `PngFileLoad` and audio followed on 2026-10-17.
- [x] **`glTexParameterf` "receives garbage": false alarm.** The game passes correct floats (9728.0 `GL_NEAREST`, 9729.0 `GL_LINEAR`, 33071.0 `GL_CLAMP_TO_EDGE`; literals checked in `COglCore::Texture_setParam`). The odd "raw bits" came from our logger, which mishandles `%f` and shifts the arguments after it. Don't trust `%f` in `l_*` output (2026-10-17).
- [x] Texture formats in the assets.
  - **`.obm` format** (our own analysis): magic `"OI"`, a format byte, bits per pixel, then width and height as LE u16, then the pixel data. Seen: `01`/32 bpp = RGBA8888 (`glTexImage2D` RGBA), `00`/24 bpp = RGB888, `04`/4 bpp = 16-colour palette + indices (uploaded as `GL_PALETTE4_RGB5_A1_OES`; 8-bit ones as `GL_PALETTE8_RGB5_A1_OES`), `ff` = zlib-wrapped inner `.obm`. Non-power-of-two sizes occur (e.g. 248×54). Some sprites are uploaded as `GL_UNSIGNED_SHORT_5_5_5_1` RGBA, and a few textures are ETC1. No PVRTC upload has been seen, and no re-encoding is needed; the loader converts formats vitaGL mishandles (Findings 2026-10-17).
- [x] Shaders: shipped as GLSL source (vitaGL can translate them at runtime via `libshacccg`) or built at runtime? Count them and check their complexity.
  - The game has none (GLES1 fixed-function). vitaGL generates fixed-function shaders itself and compiles them at runtime, so `libshacccg.suprx` is still required (see §6).
- [ ] Render resolution and how it is chosen. Can it be forced to 960x544?
- [ ] Draw calls per frame and fill rate, as a rough guess at whether 60 fps is reachable on the SGX543.

## 5. Performance and timing

- [x] CPU cost on real hardware. The game draws at 20 fps by design (Findings 2026-10-17), with game work of about 1–6 ms per frame in Vita3K, so there should be headroom; to confirm on a Vita.
  - **Hardware (2026-10-19 log):** title and fights hold 20.0 fps with 1–4 ms of game work per frame, and Cricket renders take 50–500 µs per 20 ms. Loading frames are the exception (230–460 ms each; Findings 2026-10-20). Menu responsiveness is still open (Next steps).
- [ ] Threading model (render thread, game thread, audio thread) and how it maps onto the Vita's 3 cores.
- [x] Frame pacing: `onDrawFrame` waits for `s_frame_interval` ms (default 50 ms), and the loader does the same (Findings 2026-10-17).

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
- [x] Requirements on the user's Vita: kubridge, **libshacccg.suprx in `ur0:data/` (confirmed required; dumped with ShaRKF00D)**, fd_fix, and the RAM-unlock plugin if needed.
  - **Answered on hardware (2026-10-18):** a recent **bythos14 kubridge** (older ones lack `kuKernelMemProtect`) and `libshacccg.suprx`, plus "Enable Unsafe Homebrew" in HENkaku settings. fd_fix and a RAM-unlock plugin aren't needed.
- [x] Debugging workflow: the vita-parse-core coredump parser, PrincessLog/netlog, and a GDB stub if one exists.
  - In use: Debug builds write `ux0:data/sf4/log.txt` (copied off with VitaShell), and crash dumps from `ux0:data/` are read with the Python 3 copy of vita-parse-core (Findings 2026-10-19). That has been enough so far; netlog and GDB weren't needed.

## 7. Legal / distribution

- [x] Confirm the approach: distribute only our loader. Users supply their own APK. No Capcom assets or code in the repo or in releases.
  - This is the approach: `.gitignore` keeps game files out, `tools/extract_game.py` works from the player's own `.apkm`, and the paid Champion Pack isn't bypassed. Still to do for a release: original LiveArea artwork (Next steps).
- [x] Check the licenses of the template code and libraries we reuse.
  - soloader-boilerplate, so_util and FalsoJNI are MIT; vitaGL is LGPL-3.0 (we link VitaSDK's package). Credits are in [loader/README.md](loader/README.md#credits-and-licence).
