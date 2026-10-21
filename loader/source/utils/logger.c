/*
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/logger.h"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <stdbool.h>
#include <stdatomic.h>

#define COLOR_RED    "\x1B[38;5;196m"
#define COLOR_PINK   "\x1B[38;5;212m"
#define COLOR_ORANGE "\x1B[38;5;202m"
#define COLOR_BLUE   "\x1B[38;5;32m"
#define COLOR_GREEN  "\x1B[32m"
#define COLOR_CYAN   "\x1B[36m"

#define COLOR_END    "\033[0m"

static SceKernelLwMutexWork _log_mutex;
static atomic_bool _log_mutex_ready = ATOMIC_VAR_INIT(false);

// Buffer A is used to adjust the format string.
static char buffer_a[2048];
// Buffer B is used to compile the final log using the updated format string.
static char buffer_b[2048];

// sf4-vita: Debug builds also write the log, without colour codes, to
// DATA_PATH "log.txt", so a real Vita's log can be copied off with VitaShell.
// Lines are collected in RAM and written out by log_flush() (every 5 s with
// the stats, game.c), when the buffer fills, and at once for errors. A write
// per line cost ~4 ms on the Vita and slowed the next file open, so Debug
// builds loaded slower than Release ones. A crash without an error message
// loses up to the last 5 s.
#ifdef DATA_PATH
static SceUID log_fd = -1;
static char log_buf[64 * 1024];
static int log_len;

static SceUInt64 log_written;  // when log_write_out last ran

static void log_write_out(void) {
    if (log_fd >= 0 && log_len > 0)
        sceIoWrite(log_fd, log_buf, log_len);
    log_len = 0;
    log_written = sceKernelGetProcessTimeWide();
}

static void log_to_file(const char *line, int now) {
    if (log_fd == -1) {
        sceIoRemove(DATA_PATH "log.txt");  // SCE_O_TRUNC alone left old tails in Vita3K
        log_fd = sceIoOpen(DATA_PATH "log.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    }
    if (log_fd < 0)
        return;
    if (log_len + (int)sizeof(buffer_b) > (int)sizeof(log_buf))
        log_write_out();
    for (const char *c = line; *c; c++) {
        if (*c == '\x1B') {  // skip ANSI colour sequences: ESC [ ... m
            while (*c && *c != 'm') c++;
            if (!*c) break;
            continue;
        }
        log_buf[log_len++] = *c;
    }
    // Also when the stats stop (a hang or a long load), so the lines before it
    // still reach the file.
    if (now || sceKernelGetProcessTimeWide() - log_written > 5000000)
        log_write_out();
}
#endif

void log_flush(void) {
#ifdef DATA_PATH
    if (!atomic_load_explicit(&_log_mutex_ready, memory_order_relaxed))
        return;
    sceKernelLockLwMutex(&_log_mutex, 1, NULL);
    log_write_out();
    sceKernelUnlockLwMutex(&_log_mutex, 1);
#endif
}

void _log_print(int t, const char* fmt, ...) {
    if (!atomic_load_explicit(&_log_mutex_ready, memory_order_relaxed)) {
        int ret = sceKernelCreateLwMutex(&_log_mutex, "log_lock", 0, 0, NULL);
        if (ret < 0) {
            sceClibPrintf("Error: failed to create log mutex: 0x%x\n", ret);
            return;
        }
        atomic_store_explicit(&_log_mutex_ready, true, memory_order_relaxed);
    }
    sceKernelLockLwMutex(&_log_mutex, 1, NULL);

    switch (t) {
        case LT_DEBUG:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s• debug%s    %s\n",
                            COLOR_PINK, COLOR_END, fmt); break;
        case LT_INFO:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %sℹ info%s     %s\n",
                            COLOR_BLUE, COLOR_END, fmt); break;
        case LT_WARN:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s⚠ warning%s  %s\n",
                            COLOR_ORANGE, COLOR_END, fmt); break;
        case LT_ERROR:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s⨯ error%s    %s\n",
                            COLOR_RED, COLOR_END, fmt); break;
        case LT_FATAL:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s! fatal%s    %s\n",
                            COLOR_RED, COLOR_END, fmt); break;
        case LT_SUCCESS:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s! success%s  %s\n",
                            COLOR_GREEN, COLOR_END, fmt); break;
        case LT_WAIT:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s… waiting%s  %s\n",
                            COLOR_CYAN, COLOR_END, fmt); break;
        default:
            if (atomic_load_explicit(&_log_mutex_ready, memory_order_relaxed)) {
                sceKernelUnlockLwMutex(&_log_mutex, 1);
            }
            return;
    }

    va_list list;
    va_start(list, fmt);
    sceClibVsnprintf(buffer_b, sizeof(buffer_b), buffer_a, list);
    va_end(list);
    sceClibPrintf(buffer_b);
#ifdef DATA_PATH
    log_to_file(buffer_b, t == LT_ERROR || t == LT_FATAL);
#endif

    if (atomic_load_explicit(&_log_mutex_ready, memory_order_relaxed)) {
        sceKernelUnlockLwMutex(&_log_mutex, 1);
    }
}
