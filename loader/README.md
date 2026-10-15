# sf4-vita loader

The Vita program that loads Street Fighter IV CE's Android libraries and runs them. It's based on [soloader-boilerplate](https://github.com/v-atamanenko/soloader-boilerplate) ("solobop") by Volodymyr Atamanenko, copied from commit `0a382f0e` (2026-07-22). See that project's README for how the template works in general.

## Status

**Milestone 1 reached (2026-10-15, Vita3K):** `libc++_shared.so` and `libGltest2jni.so` load, relocate and link with no unresolved imports. The game's static constructors run, and `JNI_OnLoad` returns `JNI_VERSION_1_6`. Nothing is drawn yet; the next step is `GlTest2Renderer.NativeInit` and the frame loop.

## Data files

Put these from your own copy of the game (1.08.03, `split_config.armeabi_v7a.apk` → `lib/armeabi-v7a/`) in `ux0:data/sf4/`:

```
ux0:data/sf4/libGltest2jni.so
ux0:data/sf4/libc++_shared.so
```

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

To test in Vita3K: `powershell -File tools\vita3k-run.ps1 <vpk> SFIV00001` installs the VPK, runs it, and prints the log.

## Changes from the template

- **Two modules:** `libc++_shared.so` is loaded at `0x97000000` before the game at `0x98000000`. so_util resolves the game's C++ imports against it through `DT_NEEDED` ([source/utils/init.c](source/utils/init.c)).
- **[source/reimpl/sf4_compat.c](source/reimpl/sf4_compat.c):** the 66 bionic imports the template didn't cover (FORTIFY `_chk` variants, `*_l` locale functions, `getauxval`, `stderr`, …), plus a real `dl_unwind_find_exidx` so C++ exceptions can unwind. It also forwards `glLightf`/`glLightModelf` to vitaGL's vector forms. Its table is placed *before* the template's, so its entries take priority.
- **`kuser_patch` is disabled:** neither library uses the Linux kuser helpers.
- **Startup checks:** the loader refuses to start without `libshacccg.suprx`, instead of crashing inside vitaGL.
- **Build fixes:**
  - `_getentropy_r` is provided (the softfp newlib lacks it).
  - `lib/libc_bridge` declares its stub as a CMake `BYPRODUCTS`, so the Ninja generator works.
  - The `KUBRIDGE` option compiles a patched copy of so_util; the submodule itself is unchanged.

## Credits and licence

This code started as soloader-boilerplate, with contributions from TheFloW (original so-loader, gtasa_vita), Rinnegatamante (vitaGL, so_util, many ports), Volodymyr Atamanenko (solobop, FalsoJNI) and fgsfds. The template code is MIT licensed ([LICENSE](LICENSE)). The submodules keep their own licences: so_util (MIT), FalsoJNI (MIT), vitaGL (LGPL-3.0).
