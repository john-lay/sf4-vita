# Development setup

This is the setup the project is developed on: Windows 10, building in WSL 1, testing in Vita3K and on a real Vita. Verified 2026-10-15 by building a minimal framebuffer-fill VPK and running it in Vita3K.

## Build toolchain (WSL 1, Ubuntu 18.04)

WSL 2 isn't required. On the dev machine the distro runs as WSL **1**, because the Windows "Virtual Machine Platform" feature is off. WSL 1 builds more slowly but works fine.

| Tool | Version | Location | Notes |
|---|---|---|---|
| VitaSDK | `sdk-snapshot-20261015.850.1` (GCC 15.2.0) | `~/vitasdk` | Use the **`x86_64-linux-gnu`** tarball from [vitasdk/autobuilds](https://github.com/vitasdk/autobuilds/releases). It runs on Ubuntu 18.04's glibc 2.27. The `linux-musl` tarball isn't static: it needs `/lib/ld-musl-x86_64.so.1`, which needs `apt install musl` |
| CMake | 3.31.6 | `~/.local/cmake` (symlink to the Kitware tarball) | Ubuntu 18.04's apt CMake (3.10) is too old |
| ninja | 1.12.1 | `~/.local/bin/ninja` | The distro has no `make`. CMake uses ninja via `CMAKE_GENERATOR` |

Everything is installed per-user, so nothing needs sudo. The end of `~/.bashrc` contains:

```sh
# VitaSDK (added for sf4-vita)
export VITASDK=$HOME/vitasdk
export PATH=$VITASDK/bin:$HOME/.local/cmake/bin:$HOME/.local/bin:$PATH
export CMAKE_GENERATOR=Ninja
```

To build a CMake project that uses `$VITASDK/share/vita.cmake`:

```sh
cmake -S . -B build
cmake --build build      # produces eboot.bin and <name>.vpk
```

Build in the WSL filesystem (e.g. `/tmp` or `~`), not on `/mnt/c` or `/mnt/d`. File I/O across the Windows mount is very slow on WSL 1.

### Running WSL from Git Bash or scripts

- Set `MSYS_NO_PATHCONV=1` before calling `wsl.exe`, or Git Bash rewrites `/mnt/c/...` into `C:/Program Files/Git/mnt/c/...`.
- Inline `bash -c '...'` strings get mangled by PowerShell quoting. Put the commands in a script file with **LF** line endings and run `wsl.exe -e bash /mnt/c/path/to/script.sh`.
- WSL 1 console output often arrives garbled when captured. Redirect it inside the script (`exec > /tmp/x.log 2>&1`), then `wsl.exe -e cat /tmp/x.log`.

## Testing

### Vita3K (emulator)

- Portable Windows build from [vita3k.org](https://vita3k.org). Firmware and the font package come from Sony's update site and are installed through Vita3K's first-run wizard.
- The emulated Vita filesystem is at `%APPDATA%\Vita3K\Vita3K\` (`ux0\`, `ur0\`, …). Installed apps live in `ux0\app\<TITLEID>\`. Game data for the loader will go in `ux0\data\sf4\`.
- Install a build with File → Install `.vpk`.
- Use it for quick smoke tests only. It isn't timing-accurate, and its support for so_loader techniques (kubridge, executable memory, runtime code patching) may lag behind real hardware. Treat the real Vita as the reference for anything from loading the game's `.so` onwards, and for all performance work.

### Real hardware

Not yet set up. Planned: VitaShell for installing VPKs, and the VitaCompanion plugin for pushing eboots over FTP. Plugins the loader will need on the device (kubridge, `libshacccg.suprx`, etc.) are tracked in RESEARCH.md §6.

## Analysis tools (Windows host)

- No binutils on the host. Use Python 3.11 + `pyelftools` (`tools/list_imports.py`).
- System Java is 8. jadx 1.5.1 needs JDK 11+, so we use a portable Temurin 17 kept outside the repo.
