# sf4-vita

Goal: run the Android release of Street Fighter IV (Champion Edition, or the older Volt release) on the PS Vita. The plan is a native wrapper ("so_loader" style) like the existing Vita ports of Android games (GTA SA, Max Payne, Bully, etc.). The wrapper loads the game's ARMv7 `.so` from the APK into Vita memory, resolves its imports against Vita-side stubs (vitaGL, newlib, a fake JNI, an OpenSL ES/audio shim) and runs it.

Status: **research phase**. See [RESEARCH.md](RESEARCH.md) for open questions and findings.

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

- VitaSDK (arm-vita-eabi GCC, CMake toolchain), built on Windows via WSL/MSYS2 or Linux
- vitaGL (+ vitaShaRK or runtime shader compilation via `libshacccg.suprx`)
- so_loader / falso_jni / kubridge from existing Android→Vita port templates
- Analysis: Ghidra or IDA, `readelf`/`objdump` (arm-linux-androideabi), apktool/jadx for the Java side

## Repo layout (planned)

```
CLAUDE.md        this file
RESEARCH.md      research checklist and findings
docs/            longer write-ups (symbol maps, JNI call list, etc.)
loader/          Vita loader source (later)
game/            user's own APK/OBB/extracted files (git-ignored)
```

## Environment notes

- Host: Windows 10, PowerShell + Git Bash available.
