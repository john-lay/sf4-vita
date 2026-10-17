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

void so_patch(void) {
	patch_offline_boot();
	patch_disable_game_center();
	patch_offline_nickname();

	// kuser_patch() is not needed: neither libGltest2jni.so nor libc++_shared.so
	// references the Linux kuser helpers (0xFFFF0FC0/0xFFFF0FA0), checked 2026-10-15.
	// Sample hook with symbol name
	// hook_addr((uintptr_t)so_symbol(&so_mod, "_ZN6glitch2os7Printer5printEPKcz"), (uintptr_t)&hookedFunction);
	// Or with offset
	// hook_addr((uintptr_t)so_mod.text_base + 0xdeadbabe, (uintptr_t)&hookedFunction);
	// If you use SO_CONTINUE, define a so_hook before the function and assign to it
	// function_hook = hook_addr(...);
}