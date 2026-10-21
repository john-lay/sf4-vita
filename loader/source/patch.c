/*
 * Copyright (C) 2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  patch.c
 * @brief Patching some of the .so internal functions or bridging them to native
 *        for better compatibility.
 */

#include <kubridge.h>
#include <so_util/so_util.h>
#include <stdio.h>
#include <string.h>
#include <vitasdk.h>

#ifdef __cplusplus
extern "C"
{
#endif
	extern so_module so_mod;
#ifdef __cplusplus
};
#endif

#define SCE_KERNEL_MEMBLOCK_TYPE_USER_RX (0x0C20D050)

#include "utils/logger.h"
#include "utils/dialog.h"
#include "reimpl/sys.h"
#include <stdbool.h>

void __kuser_memory_barrier(void) {
	__sync_synchronize();
}

void kuser_patch(void) {
	SceKernelAllocMemBlockKernelOpt opt;
	memset(&opt, 0, sizeof(SceKernelAllocMemBlockKernelOpt));
	opt.size = sizeof(SceKernelAllocMemBlockKernelOpt);
	opt.attr = 0x1;
	opt.field_C = (SceUInt32)0x9A000000;
	if (kuKernelAllocMemBlock("atomic", SCE_KERNEL_MEMBLOCK_TYPE_USER_RX, 0x1000, &opt) < 0)
		fatal_error("Error could not allocate atomic block.");
	kuKernelMemProtect((void *)0x9A000000, (SceSize)0x1000, KU_KERNEL_PROT_EXEC | KU_KERNEL_PROT_READ | KU_KERNEL_PROT_WRITE);

	hook_addr(0x9A000FA0, (uintptr_t)__kuser_memory_barrier);
	hook_addr(0x9A000FC0, (uintptr_t)__atomic_cmpxchg);

	uint32_t patched_addr;
	for (uint32_t addr = so_mod.text_base; addr < so_mod.text_base + so_mod.text_size; addr += 4) {
		uint32_t *a = (uint32_t *)addr;
		if (*a == 0xFFFF0FC0) {
			l_debug("Patching 0x%x -> __kuser_cmpxchg", a);
			patched_addr = 0x9A000FC0;
			kuKernelCpuUnrestrictedMemcpy((void *)(addr), &patched_addr, sizeof(uint32_t));
		}
		else if (*a == 0xFFFF0FA0) {
			l_debug("Patching 0x%x -> __kuser_memory_barrier", a);
			patched_addr = 0x9A000FA0;
			kuKernelCpuUnrestrictedMemcpy((void *)(addr), &patched_addr, sizeof(uint32_t));
		}
	}
}

/* ---- sf4-vita patches ------------------------------------------------------- */

// Overwrites one ARM instruction, but only if it still holds the expected word,
// so a different game build fails safely instead of being corrupted.
static int patch_insn(uintptr_t addr, uint32_t expect, uint32_t value) {
	if (*(volatile uint32_t *)addr != expect) {
		l_error("patch at 0x%08x: expected 0x%08x, found 0x%08x; skipped", addr, expect, *(uint32_t *)addr);
		return 0;
	}
#ifdef NO_KUBRIDGE
	*(volatile uint32_t *)addr = value;  // VM-domain memory is RWX
#else
	kuKernelCpuUnrestrictedMemcpy((void *)addr, &value, sizeof(value));
#endif
	return 1;
}

// Offline boot. AppMain::ST_logoSystem() is a state machine whose state 10 checks
// IsNetworkEnable(): online it goes to state 3 (first-run licence web view, then
// an online login in states 4-6) and offline it shows "Upload failed" and retries
// for ever. Make state 10 continue to state 7, the logo timer that leads to the
// title screen. Offsets are for 1.08.03 (docs/libGltest2jni.md).
static void patch_offline_boot(void) {
	uintptr_t f = so_symbol(&so_mod, "_ZN7AppMain13ST_logoSystemEv");
	if (!f) {
		l_error("ST_logoSystem not found; offline boot patch skipped");
		return;
	}
	int ok = patch_insn(f + 0xd8, 0x0A000004, 0xE320F000)   // beq <network error> -> nop
	       & patch_insn(f + 0xdc, 0xE3000003, 0xE3000007);  // next state 3 -> 7
	if (ok)
		l_success("Offline boot patch applied.");
}

// Google Play Games can't work on the Vita (and achievements are stubbed). With
// it "enabled", AppMain::checkLoginGameCenter() starts a login and waits for
// ever behind a spinner on the Capcom logo. Make Lib_GameCenter::isEnabled()
// return false: the game then skips sign-in and continues to the title.
static void patch_disable_game_center(void) {
	uintptr_t f = so_symbol(&so_mod, "_ZN14Lib_GameCenter9isEnabledEv");
	if (!f) {
		l_error("Lib_GameCenter::isEnabled not found; patch skipped");
		return;
	}
	int ok = patch_insn(f + 0, 0xE24DD004, 0xE3A00000)   // sub sp, sp, #4 -> mov r0, #0
	       & patch_insn(f + 4, 0xE58D0000, 0xE12FFF1E);  // str r0, [sp]   -> bx lr
	if (ok)
		l_success("Google Play Games disabled.");
}

// Nickname registration. When the player confirms a name, AppMain::GT_NameEntryDialog
// calls ST_NetworkConnectStartBase(11, on_success, on_error) to upload it, which
// offline only shows "Upload failed". on_success (a local function just after
// the dialog, 0x5c59d8) sets the dialog's result to 2, after which the dialog
// stores the name with SetPlayerName/SaveDataWrite/SaveMyCardData. Call it
// directly instead of going online. Offsets are for 1.08.03.
static void patch_offline_nickname(void) {
	uintptr_t f = so_symbol(&so_mod, "_ZN7AppMain18GT_NameEntryDialogEP12GENERAL_TASK");
	if (!f) {
		l_error("GT_NameEntryDialog not found; nickname patch skipped");
		return;
	}
	// bl ST_NetworkConnectStartBase -> bl on_success (+0xe30): offset (0xe30 - 0xba4) / 4
	if (patch_insn(f + 0xb9c, 0xEB09B989, 0xEB0000A3))
		l_success("Offline nickname registration patch applied.");
}

// Cricket Audio mixes audioUpdateMs of sound per pass and drops sounds when a
// pass takes longer than its share of that ("Audio rendering is taking too
// long; some sounds will not be played"). Its default, 5 ms (240 frames at
// 48 kHz), is too short for the Vita's CPU: audio was garbled, worst in the
// menus. initCricket passes CkConfig's defaults straight to CkInit, so adjust
// them there: CkConfig +32 audioUpdateMs (5.0f), +44 maxAudioTasks (500,
// which Cricket had to grow to 1000 by itself). Layout from
// _CkConfig::_CkConfig in 1.08.03.
static so_hook ck_init;

static int CkInit_hook(void *config) {
	float *update_ms = (float *)((char *)config + 32);
	int *max_tasks = (int *)((char *)config + 44);
	if (*update_ms == 5.0f && *max_tasks == 500) {
		*update_ms = 20.0f;
		*max_tasks = 1000;
		l_success("Cricket: audio update period 20 ms, 1000 audio tasks.");
	} else {
		l_error("Cricket: unexpected CkConfig (%d ms?, %d tasks?); left as is", (int)*update_ms, *max_tasks);
	}
	return SO_CONTINUE(int, ck_init, config);
}

static void patch_cricket_config(void) {
	uintptr_t f = so_symbol(&so_mod, "CkInit");
	if (f)
		ck_init = hook_addr(f, (uintptr_t)CkInit_hook);
	else
		l_error("CkInit not found; Cricket keeps its 5 ms update period");
}

#ifndef MENU_REPLAY
// The mode select screen's TVs play a replay: one of the player's saved
// replays, or with none, a demo replay from the assets (rpd_*.bin). The game
// loads that whole fight on top of the menus, and with the Champion Pack's
// characters that ran vitaGL out of memory (4 s frames). Report that there's
// no replay to show: the TVs then show static. This is the function's own
// early exit (when the replay data isn't ready): AppMain +0x2c80 and +0x2c88
// cleared, +0x2c7c (replay set) false, return false. Offsets are for 1.08.03.
static int checkInitModeSelectReplayData_hook(char *app) {
	*(int *)(app + 0x2c80) = 0;
	*(int *)(app + 0x2c88) = 0;
	app[0x2c7c] = 0;
	return 0;
}

static void patch_no_menu_replay(void) {
	uintptr_t f = so_symbol(&so_mod, "_ZN7AppMain29checkInitModeSelectReplayDataEv");
	if (!f) {
		l_error("checkInitModeSelectReplayData not found; the menu replay stays on");
		return;
	}
	hook_addr(f, (uintptr_t)checkInitModeSelectReplayData_hook);
	l_success("Mode select replay disabled.");
}
#endif

void so_patch(void) {
	patch_offline_boot();
	patch_disable_game_center();
	patch_offline_nickname();
	patch_cricket_config();
#ifndef MENU_REPLAY
	patch_no_menu_replay();
#endif

	// kuser_patch() is not needed: neither libGltest2jni.so nor libc++_shared.so
	// references the Linux kuser helpers (0xFFFF0FC0/0xFFFF0FA0), checked 2026-10-15.
	// Sample hook with symbol name
	// hook_addr((uintptr_t)so_symbol(&so_mod, "_ZN6glitch2os7Printer5printEPKcz"), (uintptr_t)&hookedFunction);
	// Or with offset
	// hook_addr((uintptr_t)so_mod.text_base + 0xdeadbabe, (uintptr_t)&hookedFunction);
	// If you use SO_CONTINUE, define a so_hook before the function and assign to it
	// function_hook = hook_addr(...);
}