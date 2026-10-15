# Imports the Vita loader must provide

Generated with `python tools/list_imports.py libGltest2jni.so libc++_shared.so` against 1.08.03 (`split_config.armeabi_v7a.apk`). Re-run it if the target version changes.

| Lib | External imports | Notes |
|---|---|---|
| `libGltest2jni.so` | 245 | Plus 96 C++ symbols that `libc++_shared.so` provides (341 total, matching [libGltest2jni.md](libGltest2jni.md)) |
| `libc++_shared.so` | 161 | Mostly libc, wide-char and `*_l` locale functions |

> **Status (2026-10-15):** all imports now resolve in the loader. The template covers most of them; the remaining 66 live in [`loader/source/reimpl/sf4_compat.c`](../loader/source/reimpl/sf4_compat.c). Confirmed in Vita3K: 0 unresolved.

## Decision: load `libc++_shared.so` as a second module

The game's 96 C++ imports are libc++ symbols in the `std::__ndk1` namespace. The Vita toolchain ships GNU libstdc++, which mangles differently and has a different object layout, so those symbols can't be resolved against it. The loader therefore loads `libc++_shared.so` first, resolves the game's C++ imports against its exports, and stubs the Android-only parts it needs (locale, `syslog`, `android_set_abort_message`).

## Imports that need a wrapper (bionic ≠ newlib ABI)

Most libc/libm imports map directly onto newlib. These don't, and every port that skips them hits subtle crashes:

| Import(s) | Problem | Wrapper |
|---|---|---|
| `__sF`, and every stdio call that takes a `FILE*` | bionic's `stdin/stdout/stderr` are entries in a `__sF[3]` array with bionic's `FILE` layout | Export a dummy `__sF` array. In `fprintf`/`fputs`/`fwrite`/`fflush`/… map `&__sF[n]` to newlib's std streams |
| `stat`, `lstat`, `fstat` | `struct stat` layout differs | Call newlib, then copy fields into a bionic-layout struct |
| `opendir`/`readdir`/`closedir` | `struct dirent` layout differs (bionic `d_name` sits at offset 19) | Return a bionic-layout `dirent` from a static buffer per `DIR` |
| `open`, `__open_2`, `openat` | `O_*` flag values differ (e.g. `O_CREAT` is `0100` in bionic and `0x200` in newlib) | Translate the flags, then redirect paths to `ux0:data/sf4/` |
| `pthread_mutex_*`, `pthread_cond_*` | bionic's 32-bit `pthread_mutex_t`/`pthread_cond_t` are 4 bytes. Static initialisers are `0`, or `0x4000` for recursive | Store a pointer to a heap-allocated real object in the 4-byte slot. Initialise lazily on first use so static initialisers work |
| `pthread_create`, `pthread_attr_*` | The Vita default thread stack is small | Force a larger stack (e.g. 1 MB). Check which threads exist (RESEARCH §2) |
| `__errno` | errno *values* differ between Linux and newlib | Return `&errno`. Translate the values only if the game turns out to compare them |
| `clock_gettime` | `CLOCK_MONOTONIC` id differs (1 in bionic, 4 in upstream newlib; check VitaSDK's headers) | Translate the clock id |
| `getaddrinfo`, `freeaddrinfo`, sockets, `select`, `__FD_*_chk` | `struct addrinfo`/`fd_set` differ; online play is out of scope | Stub to fail (`EAI_FAIL`, `-1`). The game must handle being offline |
| `dlopen`/`dlsym`/`dlclose` | Used to load `libOpenSLES.so` for Cricket Audio | `dlopen("libOpenSLES.so")` returns a fake handle; `dlsym` looks the name up in our OpenSL ES shim table |
| `dl_unwind_find_exidx` (libc++) | The game throws C++ exceptions; the unwinder asks which module's `.ARM.exidx` covers a PC | Implement it: return the `.ARM.exidx` range of whichever loaded module contains the PC. **Unwinding fails without this** |
| `__stack_chk_guard`, `__stack_chk_fail` | Data symbol plus abort | Provide a guard variable and an abort |
| `getauxval` | Probably used for NEON detection via `AT_HWCAP` (unconfirmed) | Return `HWCAP_NEON \| HWCAP_VFPv3` for `AT_HWCAP` and 0 otherwise |
| `*_chk` FORTIFY variants | Not in newlib | Forward to the plain function, ignoring the size argument |
| `__cxa_atexit`, `__cxa_finalize`, `__cxa_thread_atexit_impl` | The loader runs module init/fini itself | Record and ignore (the game never exits cleanly on Vita anyway) |
| Locale (`newlocale`, `uselocale`, `*_l`, `setlocale`) | newlib's support is partial | Stub to the "C" locale; forward `*_l` to the non-`_l` variant |

Plain stubs that return 0 are enough for: `__android_log_*` (route to a debug log), `gettid`, `setpriority`, `signal`, `__register_atfork`, `ioctl`, `syslog`/`openlog`/`closelog`, `android_set_abort_message`, `syscall`, and `sendfile`.

## GLES1 (68)

These go straight to vitaGL, except four the template stubs to `ret0`. `glLightf` and `glLightModelf` are now forwarded to vitaGL's `*fv` forms. **`glCompressedTexSubImage2D` and `glLogicOp` are still no-ops**, because vitaGL has neither. The ones to verify against vitaGL's GLES1 coverage are listed in [libGltest2jni.md](libGltest2jni.md). The `*OES` framebuffer entry points may need aliasing to vitaGL's core names.

## zlib (3)

`compress`, `uncompress`, `crc32`: link Vita zlib (`-lz`).
