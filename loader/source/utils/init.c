/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2021-2022 Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/init.h"

#include "utils/dialog.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include "utils/settings.h"

#include <reimpl/controls.h>

#include <string.h>

#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/kernel/clib.h>
#include <psp2/power.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>
#include <fios/fios.h>

// Base addresses for the Android .so files. libc++_shared.so is under 1 MB
// and the game is ~8.7 MB, so these leave plenty of room between them.
#define CXX_LOAD_ADDRESS 0x97000000
#define LOAD_ADDRESS     0x98000000

extern so_module so_mod;
extern so_module cxx_mod;

// Loads, relocates and resolves one module. Called for libc++_shared.so
// before the game, so that so_util can resolve the game's C++ imports
// against it through DT_NEEDED.
static void load_module(so_module *mod, const char *path, uintptr_t addr) {
    if (!file_exists(path)) {
        fatal_error("Looks like you haven't installed the data files for this "
                    "port, or they are in an incorrect location. Please make "
                    "sure that you have %s file exactly at that path.", path);
    }

    if (so_file_load(mod, path, addr) < 0) {
        l_fatal("%s could not be loaded.", path);
        fatal_error("Error: could not load %s.", path);
    }

    so_relocate(mod);
    resolve_imports(mod);
    l_success("%s loaded, relocated and resolved.", path);
}

void soloader_init_all() {
	// Launch `app0:configurator.bin` on `-config` init param
    sceAppUtilInit(&(SceAppUtilInitParam){}, &(SceAppUtilBootParam){});
    SceAppUtilAppEventParam eventParam;
    sceClibMemset(&eventParam, 0, sizeof(SceAppUtilAppEventParam));
    sceAppUtilReceiveAppEvent(&eventParam);
    if (eventParam.type == 0x05) {
        char buffer[2048];
        sceAppUtilAppEventParseLiveArea(&eventParam, buffer);
        if (strstr(buffer, "-config"))
            sceAppMgrLoadExec("app0:/configurator.bin", NULL, NULL);
    }

    // Set default overclock values
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

#ifdef USE_SCELIBC_IO
    if (fios_init(DATA_PATH) == 0)
        l_success("FIOS initialized.");
#endif

#ifndef NO_KUBRIDGE
    if (!module_loaded("kubridge")) {
        l_fatal("kubridge is not loaded.");
        fatal_error("Error: kubridge.skprx is not installed.");
    }
    l_success("kubridge check passed.");
#else
    l_info("Built with KUBRIDGE=OFF: so_util uses a VM domain (for Vita3K).");
#endif

    if (!file_exists("ur0:data/libshacccg.suprx")
        && !file_exists("ur0:data/external/libshacccg.suprx")) {
        // vitaGL compiles its fixed-function shaders at runtime and crashes
        // through a null pointer if the compiler is missing.
        fatal_error("Error: libshacccg.suprx is not installed. Extract it "
                    "from your console with ShaRKF00D.");
    }

    settings_load();
    l_success("Settings loaded.");

    load_module(&cxx_mod, CXX_SO_PATH, CXX_LOAD_ADDRESS);
    load_module(&so_mod, SO_PATH, LOAD_ADDRESS);

    so_patch();
    l_success("SO patched.");

    so_flush_caches(&cxx_mod);
    so_flush_caches(&so_mod);
    l_success("SO caches flushed.");

    // Dependency first, as the Android linker would.
    so_initialize(&cxx_mod);
    so_initialize(&so_mod);
    l_success("SO initialized.");

    gl_preload();
    l_success("OpenGL preloaded.");

    jni_init();
    l_success("FalsoJNI initialized.");

#ifndef NDK_PORT
    controls_init();
    l_success("Controls initialized.");
#endif
}
