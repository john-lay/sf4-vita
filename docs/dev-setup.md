# Development setup

This is the setup the project is developed on: Windows 10, building in WSL 2, testing in Vita3K and on a real Vita. Verified 2026-10-15.

## Build toolchain (WSL 2, Ubuntu 24.04)

| Tool | Version | Notes |
|---|---|---|
| WSL | 2, distro `Ubuntu-24.04` (glibc 2.39) | The old `Ubuntu-18.04` WSL 1 distro is still the *default* distro on the dev machine, so always pass `-d Ubuntu-24.04` (or run `wsl --set-default Ubuntu-24.04`) |
| VitaSDK | **softfp** world, `sdk-vita_softfp-snapshot-20260828.667.1` (GCC 15.2.0), in `~/vitasdk` | Must be softfp, see below |
| Packages | vitaGL, vitaShaRK, SceShaccCgExt, libmathneon, kubridge, zlib, opensles (+ libsndfile & codecs), taihen, freetype | From the `vita_softfp` repo, `packages-snapshot-20261014.52.1` |
| CMake / ninja / make | 3.28.3 / apt / apt | `sudo apt install build-essential cmake ninja-build git curl python3 unzip` |

### Why softfp

`libGltest2jni.so` uses the softfp calling convention: `e_flags` 0x5000200 has `EF_ARM_ABI_FLOAT_SOFT`, so float arguments go in integer registers. VitaSDK's default world is hard-float. A hard-float loader would need a wrapper on every float-passing call in both directions (`glTranslatef`, `sinf`, JNI callbacks, …). VitaSDK publishes a separate softfp world for exactly this ("for ports that link Android .so files"). Every library the loader links must come from that world.

### Installing

Run [`tools/setup-vitasdk-softfp.sh`](../tools/setup-vitasdk-softfp.sh) inside the distro. It installs the toolchain and packages into `~/vitasdk` and adds this to `~/.bashrc`:

```sh
export VITASDK=$HOME/vitasdk
export PATH=$VITASDK/bin:$PATH
export CMAKE_GENERATOR=Ninja
```

The script exists because the normal route, `vdpm refresh nightly-softfp`, fails with **vdpm 0.1.4** (current as of 2026-10-15):

- vdpm only parses schema-1 channel manifests, and the `nightly-softfp` manifest is schema 2 (`unsupported manifest schema version`).
- Its refresh script hardcodes the hard-float `[vita]` repo, but the softfp database is `vita_softfp.db`.

The script does the same steps by hand. It checks the bootstrap tarball's sha256 and verifies the channel manifest's signature with vdpm's own `vdpm-channel verify` and the public key from the bootstrap. It checks both databases against the manifest's sha256s, then writes `$VITASDK/etc/pacman.conf`. After that, `vdpm install/search/list` work normally. If a newer vdpm learns schema 2, plain `vdpm refresh nightly-softfp` should work again.

Dead ends, so nobody retries them: Ubuntu 18.04 can't run vdpm (its `pacman` needs glibc ≥ 2.28). The `x86_64-linux-musl` tarballs aren't static (they need `/lib/ld-musl-x86_64.so.1`).

### Building

```sh
cmake -S . -B build
cmake --build build      # produces eboot.bin and <name>.vpk
```

To link vitaGL, use this library list (from the GLES1 smoke test): `vitaGL vitashark SceShaccCgExt mathneon taihen_stub SceShaccCg_stub SceGxm_stub SceDisplay_stub SceCtrl_stub SceSysmodule_stub SceAppMgr_stub SceCommonDialog_stub SceKernelDmacMgr_stub SceIme_stub SceLibKernel_stub stdc++ pthread m`. vitaGL contains C++, so `stdc++` is required. This vitaGL has no `vglEnd()`.

From Windows, [`tools/wsl-build.sh`](../tools/wsl-build.sh) builds a project in one command. It copies the project to `/tmp` inside WSL, builds there, prints only errors and undefined references, and copies the `.vpk` back:

```sh
MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu-24.04 -e bash /mnt/d/Repo/sf4-vita/tools/wsl-build.sh /mnt/d/path/to/project
```

`MSYS_NO_PATHCONV=1` stops Git Bash rewriting `/mnt/...` paths. Avoid long inline `bash -c '…'` strings, which break on nested quoting; use a script file with LF line endings (`.gitattributes` enforces LF for `*.sh`). WSL 2 console output is clean; the old WSL 1 distro garbled it.

## Testing

### Vita3K (emulator)

- Portable Windows build from [vita3k.org](https://vita3k.org). Firmware and the font package come from Sony's update site and are installed through Vita3K's first-run wizard.
- The emulated Vita filesystem is at `%APPDATA%\Vita3K\Vita3K\` (`ux0\`, `ur0\`, …). Installed apps live in `ux0\app\<TITLEID>\`. Game data for the loader will go in `ux0\data\sf4\`.
- Install a build with File → Install `.vpk`. The program is at `D:\vita-3k\Vita3K.exe` on the dev machine, and its log is `D:\vita-3k\vita3k.log` (rewritten each run).
- **vitaGL needs `libshacccg.suprx`** (Sony's runtime shader compiler) at `%APPDATA%\Vita3K\Vita3K\ur0\data\libshacccg.suprx`. Without it, the first GLES1 draw crashes Vita3K with `Read violation at address 0x0`: vitaGL calls the missing compiler through a null pointer. It can't be redistributed. Dump it from a real Vita with ShaRKF00D and copy it over.
- Use it for quick smoke tests only. It isn't timing-accurate, and its support for so_loader techniques (kubridge, executable memory, runtime code patching) may lag behind real hardware. Treat the real Vita as the reference for anything from loading the game's `.so` onwards, and for all performance work.

### Real hardware

Not yet set up. Planned: VitaShell for installing VPKs, and the VitaCompanion plugin for pushing eboots over FTP. Plugins the loader will need on the device (kubridge, `libshacccg.suprx`, etc.) are tracked in RESEARCH.md §6.

## Analysis tools (Windows host)

- No binutils on the Windows host. Use Python 3.11 + `pyelftools` (`tools/list_imports.py`), or the SDK's `arm-vita-eabi-readelf`/`objdump` in WSL. They handle the Android ARM `.so` (headers, dynamic symbols, disassembly).
- System Java is 8. jadx 1.5.1 needs JDK 11+, so we use a portable Temurin 17 kept outside the repo.
