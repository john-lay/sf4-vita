# sf4-vita

Goal: run the Android release of Street Fighter IV (Champion Edition, or the older Volt release) on the PS Vita. The plan is a native wrapper ("so_loader" style) like the existing Vita ports of Android games (GTA SA, Max Payne, Bully, etc.). The wrapper loads the game's ARMv7 `.so` from the APK into Vita memory, resolves its imports against Vita-side stubs (vitaGL, newlib, a fake JNI, an OpenSL ES/audio shim) and runs it.

Status: **Arcade fights are playable in Vita3K (menus, nickname entry, sound, correct speed and sprite colours). Next: store-locked content, real hardware.** See [RESEARCH.md](RESEARCH.md): the "Next steps" section is the task list, so start there.

## Key facts (don't re-derive; details in docs/)

- Target: SF4 Champion Edition **1.08.03** `.apkm` (App Bundle: `base.apk` + `split_config.armeabi_v7a.apk` + `split_install_assets.apk`). 1.04.00 is useless without its OBB.
- Native lib: `libGltest2jni.so`, armv7 softfp, GLES1 fixed-function, RGBA/paletted/ETC1 textures (no PVRTC seen in use), Cricket Audio, ~10k named exports. See [docs/libGltest2jni.md](docs/libGltest2jni.md).
- Java glue: `jp.gltest2.android.GlTest2Renderer`. Assets load through `FileLoad(name)` into a 32 MB direct buffer. Text is drawn with Android Canvas into GL texture 2234. Touch comes from a `short[40]` table; the gamepad is a bitmask. No licence check; Netflix layer is a stub. See [docs/java-side.md](docs/java-side.md).

## Ground rules

- **Never commit copyrighted game content.** No APKs, OBBs, `.so` files, assets, or decompiled Capcom code. The user supplies their own legally obtained copy at runtime. `.gitignore` enforces this. Keep those files in `game/`, which is git-ignored.
- Reverse-engineering notes (symbol names, offsets, function behaviour written up in our own words) are fine. Large dumps of decompiled code are not.
- Record findings in `RESEARCH.md` as they come in: what was checked, the source, and the conclusion. Mark anything unconfirmed as such.

## Target hardware (PS Vita)

- CPU: 4x ARM Cortex-A9 (ARMv7-A, NEON, VFPv3), 3 cores usable by homebrew, up to 444 MHz
- RAM: 512 MB main + 128 MB CDRAM (VRAM); homebrew typically gets ~330 MB+ with tweaks
- GPU: PowerVR SGX543MP4+, used via vitaGL (OpenGL ES 1/2 compatible layer) or GXM directly
- Screen: 960x544. Physical buttons and touch front/back.
- **Hard requirement:** the game binary must be 32-bit ARM (`armeabi-v7a`). An `arm64-v8a` binary alone cannot be loaded.

## Expected toolchain

- VitaSDK **softfp** world (arm-vita-eabi GCC 15, CMake), in WSL 2. See Environment notes
- vitaGL + vitaShaRK. Shaders are compiled at runtime, so `libshacccg.suprx` is required on the device and in Vita3K
- so_loader / falso_jni / kubridge from existing Android→Vita port templates
- Analysis: Ghidra or IDA; `arm-vita-eabi-readelf`/`objdump` in WSL (they read the Android `.so` fine) or pyelftools on Windows; jadx for the Java side

## Repo layout

```
CLAUDE.md        this file
README.md        public-facing overview
RESEARCH.md      research checklist and findings
docs/            write-ups: libGltest2jni.md, java-side.md, imports.md, dev-setup.md
tools/           list_imports.py, setup-vitasdk-softfp.sh, wsl-build.sh, vita3k-run.ps1, vita3k-shot.ps1,
                 extract_game.py, plt_calls.py, jni_refs.py, gen_java_stubs.py
loader/          Vita loader (from soloader-boilerplate; see loader/README.md). lib/so_util, lib/falso_jni, lib/vitagl are submodules
game/            user's own APK, test VPKs, libshacccg.suprx (git-ignored)
```

## Environment notes

- Host: Windows 10, PowerShell + Git Bash available.
- No binutils on the Windows host itself. Use Python + `pyelftools` there, or the VitaSDK's `arm-vita-eabi-readelf`/`objdump` inside WSL.
- System Java is 8. jadx needs a JDK 11+ (a portable Temurin 17 works). Keep decompiled output and extracted libs outside the repo.
- Full setup: [docs/dev-setup.md](docs/dev-setup.md). In short:
- Build env: **WSL 2, distro `Ubuntu-24.04`**. Always pass `-d Ubuntu-24.04`, because the old WSL 1 `Ubuntu-18.04` is still the default distro and is a dead end (it can't run vdpm). The **softfp** VitaSDK is in `~/vitasdk`, set up by `tools/setup-vitasdk-softfp.sh` because vdpm 0.1.4 can't refresh the softfp channel itself. sudo needs the user's password, so ask them to run apt commands.
- Build from Git Bash: `MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu-24.04 -e bash /mnt/d/Repo/sf4-vita/tools/wsl-build.sh /mnt/d/path/to/project`. Use script files, not long inline `bash -c` strings.
- The loader **must** be built softfp (the game `.so` is softfp). Never mix in hard-float libraries.
- Loader builds: `wsl-build.sh /mnt/d/Repo/sf4-vita/loader -DCMAKE_BUILD_TYPE=Debug [-DKUBRIDGE=OFF]`. Use `KUBRIDGE=OFF` for Vita3K (it can't load kubridge) and the default for hardware. The VPK lands in `loader/`; move it to `game/` (`*.vpk` is git-ignored). Run in Vita3K with `powershell -File tools/vita3k-run.ps1 game/<vpk> SFIV00001`. The game's `.so` files go in Vita3K's `ux0/data/sf4/` (under `%APPDATA%/Vita3K/Vita3K/`).
- Our loader code: `loader/source/game.c` (drives the game, frame pacing, offline HTTP, patches FalsoJNI), `java.c` (+ generated `java_stubs.h`), `text.c`, `input.c`, `buttons.c` (Vita button mapping, layout types, `controls.txt`), `ime.c` (text entry), `patch.c` (offline boot / Google Play / nickname patches), `reimpl/sf4_compat.c` (imports, incl. texture-format fixes for vitaGL), `reimpl/sl_vita.c` (our own OpenSL ES for Cricket Audio) and `reimpl/bigalloc.c` (big allocations in their own memory blocks; heap 184 MB + 72 MB, see RESEARCH.md), plus small marked edits in `init.c`/`main.c`/`dynlib.c`/`reimpl/io.h`. Don't modify the submodules.
- vitaGL quirks we work around (details in RESEARCH.md, Findings 2026-10-17): Vita3K (not the real GPU) reads 16-bit RGBA textures B,G,R,A from the low bits, so Vita3K builds re-pack 5551/4444 data (`VITA3K_TEX16`); hardware builds pass it through; paletted textures are expanded by us; `glCompressedTexSubImage2D`/`glLogicOp` don't exist. VitaSDK's libOpenSLES never fires buffer-queue callbacks, hence `sl_vita.c`.
- Test hooks: `-DAUTO_TAP=x,y,ms,...` and `-DAUTO_IME=text` drive the game unattended; `tools/vita3k-shot.ps1 -Count N` takes screenshot bursts. The logger mishandles `%f`.
- Game data for Vita3K: `python tools/extract_game.py game/<apkm> <Vita3K ux0/data/sf4>` (already done on this machine).
- jadx 1.5.1 + JDK 17 now live in WSL at `~/tools/` (persistent). Decompiled base.apk is in `~/sf4-decomp/out/` (outside the repo).
- Reverse engineering: disassembly is at `~/sf4-decomp/dis.txt` in WSL (`arm-vita-eabi-objdump -d -C`). Resolve PLT calls with `python tools/plt_calls.py <so> <dis.txt>` on **Windows** Python (WSL's Python has no pyelftools). Exported names make call graphs easy.
- Screenshots: `powershell -File tools/vita3k-shot.ps1 SFIV00001 <scratchpad>/x.png -Seconds N` captures only the Vita3K window via PrintWindow. Never use full-screen captures (they include the user's desktop). `glReadPixels` returns zeros in Vita3K, so verify rendering with screenshots.
- The loader links **VitaSDK's vitaGL** (`SDK_VITAGL=ON`); the template's `lib/vitagl` submodule build renders nothing in Vita3K. GL tracing: `-DGL_TRACE=ON`.
- **Vita3K** is installed and configured. The emulated filesystem is at `%APPDATA%\Vita3K\Vita3K\` (`ux0\app\`, `ux0\data\`). Use it for smoke tests; real hardware is the reference.
