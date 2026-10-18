/*
 * sf4-vita: keep large allocations out of the newlib heap.
 *
 * The game allocates big temporary buffers (COglCore::CreateTexLoadBuffer:
 * 32 MB; others of 16 MB, 10 MB and many of 4 MB) as scenes load and frees
 * them again. In one 256 MB newlib heap that left ~75 MB of holes: at the
 * character select the heap had grown to 251 MB with 148-174 MB in use, and
 * the next big `new` threw std::bad_alloc (RESEARCH.md, Findings 2026-10-18).
 *
 * Here every allocation of BIG_MIN bytes or more made by the game or libc++
 * (malloc, calloc, realloc, memalign, posix_memalign and free are routed
 * here) gets its own memory block from the kernel. Blocks are mapped page by
 * page, so freeing one returns all of it, and the newlib heap only holds
 * small objects. The newlib heap is smaller to make room (main.c: 184 MB),
 * and vitaGL leaves that room free (glutil.c: 72 MB more), so vitaGL's own
 * pools don't change. vitaGL also overflows textures into the newlib heap
 * when its pools are full, which is why the heap keeps most of the memory.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include <errno.h>
#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "utils/logger.h"

#define BIG_MIN   (1024 * 1024)
#define BIG_SLOTS 512
#define PAGE      4096

static struct { void *ptr; SceUID uid; size_t size; } big[BIG_SLOTS];
static SceKernelLwMutexWork lock;
static int lock_ready;
static size_t big_bytes, big_peak;

static void lock_take(void) {
    if (!lock_ready) {  // first call happens on the main thread, before any game thread
        sceKernelCreateLwMutex(&lock, "bigalloc", 0, 0, NULL);
        lock_ready = 1;
    }
    sceKernelLockLwMutex(&lock, 1, NULL);
}
static void lock_give(void) { sceKernelUnlockLwMutex(&lock, 1); }

// Index of a big block, or -1 if ptr came from the newlib heap. Caller holds the lock.
static int find(void *ptr) {
    for (int i = 0; i < BIG_SLOTS; i++)
        if (big[i].ptr == ptr && ptr)
            return i;
    return -1;
}

static void *big_alloc(size_t size) {
    size_t rounded = (size + PAGE - 1) & ~(size_t)(PAGE - 1);
    SceUID uid = sceKernelAllocMemBlock("sf4_big", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, rounded, NULL);
    if (uid < 0) {
        l_error("bigalloc: %u KB failed: 0x%08x (in big blocks: %u KB)", (unsigned)(size / 1024), uid,
                (unsigned)(big_bytes / 1024));
        return NULL;
    }
    void *ptr;
    sceKernelGetMemBlockBase(uid, &ptr);
    lock_take();
    int i;
    for (i = 0; i < BIG_SLOTS && big[i].ptr; i++) {}
    if (i == BIG_SLOTS) {
        lock_give();
        sceKernelFreeMemBlock(uid);
        l_error("bigalloc: more than %d big blocks", BIG_SLOTS);
        return NULL;
    }
    big[i].ptr = ptr;
    big[i].uid = uid;
    big[i].size = size;
    big_bytes += rounded;
    if (big_bytes > big_peak)
        big_peak = big_bytes;
    lock_give();
    return ptr;
}

// Frees ptr if it is a big block; returns 0 if it isn't one.
static int big_free(void *ptr) {
    if (!ptr || !lock_ready)
        return 0;
    lock_take();
    int i = find(ptr);
    if (i < 0) {
        lock_give();
        return 0;
    }
    SceUID uid = big[i].uid;
    big_bytes -= (big[i].size + PAGE - 1) & ~(size_t)(PAGE - 1);
    big[i].ptr = NULL;
    lock_give();
    sceKernelFreeMemBlock(uid);
    return 1;
}

static size_t big_size(void *ptr) {  // size of a big block, 0 if not one
    if (!ptr || !lock_ready)
        return 0;
    lock_take();
    int i = find(ptr);
    size_t s = i < 0 ? 0 : big[i].size;
    lock_give();
    return s;
}

void *malloc_sf4(size_t size) {
    return size >= BIG_MIN ? big_alloc(size) : malloc(size);
}

void *calloc_sf4(size_t n, size_t size) {
    size_t total = n * size;
    if (size && total / size != n)
        return NULL;
    if (total < BIG_MIN)
        return calloc(n, size);
    void *p = big_alloc(total);
    if (p)
        memset(p, 0, total);
    return p;
}

void free_sf4(void *ptr) {
    if (!big_free(ptr))
        free(ptr);
}

void *realloc_sf4(void *ptr, size_t size) {
    size_t old_big = big_size(ptr);
    if (!old_big && size < BIG_MIN)
        return realloc(ptr, size);  // small -> small: newlib as before
    if (old_big && size >= BIG_MIN && size <= ((old_big + PAGE - 1) & ~(size_t)(PAGE - 1))) {
        lock_take();
        big[find(ptr)].size = size;  // still fits in the same pages
        lock_give();
        return ptr;
    }
    void *p = malloc_sf4(size);
    if (!p)
        return NULL;
    if (ptr) {
        size_t old = old_big ? old_big : malloc_usable_size(ptr);
        memcpy(p, ptr, old < size ? old : size);
        free_sf4(ptr);
    }
    return p;
}

void *memalign_sf4(size_t align, size_t size) {
    // Big blocks are page aligned, which covers any alignment up to a page.
    if (size >= BIG_MIN && align <= PAGE)
        return big_alloc(size);
    return memalign(align, size);
}

int posix_memalign_sf4(void **p, size_t align, size_t size) {
    *p = memalign_sf4(align, size);
    return *p ? 0 : ENOMEM;
}

// For the 5 s frame log: KB in big blocks now and at most so far.
void bigalloc_stats(unsigned *now_kb, unsigned *peak_kb) {
    *now_kb = (unsigned)(big_bytes / 1024);
    *peak_kb = (unsigned)(big_peak / 1024);
}
