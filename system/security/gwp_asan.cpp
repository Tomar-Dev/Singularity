// system/security/gwp_asan.cpp
#include "system/security/gwp_asan.h"
#include "memory/kheap.h"
#include "memory/paging.h"
#include "memory/pmm.h"
#include "kernel/debug.h"
#include "libc/string.h"
#include "libc/stdio.h"
#include "drivers/serial/serial.h"
#include "archs/cpu/x86_64/sync/spinlock.h"
#define GWP_VIRT_BASE   0xFFFFB00000000000ULL
#define GWP_VIRT_LIMIT  (GWP_VIRT_BASE + (GWP_ASAN_SLOTS * 4ULL * PAGE_SIZE))

#define SLOT_STRIDE     (3 * PAGE_SIZE)

static GwpSlot   g_slots[GWP_ASAN_SLOTS];
static spinlock_t g_lock;
static bool       g_initialized = false;

static volatile uint32_t g_sample_counter = 0;

static volatile uint64_t g_stat_sampled   = 0;
static volatile uint64_t g_stat_overflows = 0;
static volatile uint64_t g_stat_uaf       = 0;

static inline uint64_t slot_base(int idx) {
    return GWP_VIRT_BASE + (uint64_t)idx * SLOT_STRIDE;
}

static inline uint64_t slot_data_page(int idx) {
    return slot_base(idx) + PAGE_SIZE;
}

void gwp_asan_init(void) {
    spinlock_init(&g_lock);
    memset(g_slots, 0, sizeof(g_slots));
    g_initialized = true;
}

bool gwp_asan_should_sample(void) {
    if (!g_initialized) { return false; }
    uint32_t c = __atomic_fetch_add(&g_sample_counter, 1, __ATOMIC_RELAXED);
    return (c % GWP_ASAN_SAMPLE_RATE) == 0;
}

bool gwp_asan_is_managed(void* ptr) {
    uint64_t addr = (uint64_t)ptr;
    return (addr >= GWP_VIRT_BASE && addr < GWP_VIRT_LIMIT);
}

void* gwp_asan_malloc(size_t size, uint64_t caller) {
    if (!g_initialized || size == 0 || size >= PAGE_SIZE) { return nullptr; }

    uint64_t flags = spinlock_acquire(&g_lock);

    int slot = -1;
    for (int i = 0; i < GWP_ASAN_SLOTS; i++) {
        if (g_slots[i].state == GWP_SLOT_FREE) { slot = i; break; }
    }

    if (slot == -1) {
        spinlock_release(&g_lock, flags);
        return nullptr;
    }

    uint64_t data_virt = slot_data_page(slot);

    void* phys = pmm_alloc_frame();
    if (!phys) {
        spinlock_release(&g_lock, flags);
        return nullptr;
    }

    map_page(data_virt, (uint64_t)phys, PAGE_PRESENT | PAGE_WRITE | PAGE_NX);

    memset((void*)data_virt, 0, PAGE_SIZE);

    uint64_t user_virt = data_virt + PAGE_SIZE - size;
    user_virt &= ~0x7ULL;
    if (user_virt < data_virt) { user_virt = data_virt; }

    g_slots[slot].user_ptr    = (void*)user_virt;
    g_slots[slot].page_base   = (void*)slot_base(slot);
    g_slots[slot].user_size   = size;
    g_slots[slot].alloc_caller = caller;
    g_slots[slot].free_caller  = 0;
    g_slots[slot].state        = GWP_SLOT_ACTIVE;
    g_slots[slot].alignment    = GWP_ALIGN_RIGHT;

    __atomic_fetch_add(&g_stat_sampled, 1, __ATOMIC_RELAXED);

    spinlock_release(&g_lock, flags);
    return (void*)user_virt;
}

bool gwp_asan_free(void* ptr, uint64_t caller) {
    if (!gwp_asan_is_managed(ptr)) { return false; }

    uint64_t flags = spinlock_acquire(&g_lock);

    int slot = -1;
    for (int i = 0; i < GWP_ASAN_SLOTS; i++) {
        if (g_slots[i].state == GWP_SLOT_ACTIVE &&
            g_slots[i].user_ptr == ptr)
        {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        spinlock_release(&g_lock, flags);
        __atomic_fetch_add(&g_stat_uaf, 1, __ATOMIC_RELAXED);
        panic_at("GWP-ASAN", 0, KERR_HEAP_USE_AFTER_FREE,
                 "GWP-ASAN: Free of unknown/already-freed GWP pointer!");
        __builtin_unreachable();
    }

    uint64_t data_virt = slot_data_page(slot);
    memset((void*)data_virt, 0xDE, PAGE_SIZE);

    uint64_t phys = get_physical_address(data_virt);
    unmap_page(data_virt);
    if (phys) { pmm_free_frame((void*)phys); }

    g_slots[slot].free_caller = caller;
    g_slots[slot].state       = GWP_SLOT_FREED;

    spinlock_release(&g_lock, flags);
    return true;
}

extern "C" bool gwp_asan_check_fault(uint64_t fault_addr) {
    if (!g_initialized) { return false; }
    if (fault_addr < GWP_VIRT_BASE || fault_addr >= GWP_VIRT_LIMIT) {
        return false;
    }

    uint64_t offset = fault_addr - GWP_VIRT_BASE;
    int slot        = (int)(offset / SLOT_STRIDE);
    uint64_t in_slot = offset % SLOT_STRIDE;

    if (slot >= GWP_ASAN_SLOTS) { return false; }

    char buf[320];
    GwpSlot* s = &g_slots[slot];

    if (s->state == GWP_SLOT_FREED) {
        __atomic_fetch_add(&g_stat_uaf, 1, __ATOMIC_RELAXED);
        snprintf(buf, sizeof(buf),
                 "GWP-ASAN: Use-After-Free!\n"
                 "  Fault Addr : 0x%016lx\n"
                 "  Object     : 0x%016lx (size=%lu)\n"
                 "  Alloc by   : 0x%016lx\n"
                 "  Free  by   : 0x%016lx",
                 (unsigned long)fault_addr,
                 (unsigned long)(uintptr_t)s->user_ptr,
                 (unsigned long)s->user_size,
                 (unsigned long)s->alloc_caller,
                 (unsigned long)s->free_caller);
        panic_at("GWP-ASAN", 0, KERR_HEAP_USE_AFTER_FREE, buf);
    } else if (s->state == GWP_SLOT_ACTIVE) {
        bool is_overflow  = (in_slot >= 2 * PAGE_SIZE);
        bool is_underflow = (in_slot <  PAGE_SIZE);
        const char* kind  = is_overflow ? "Buffer Overflow" :
                            is_underflow ? "Buffer Underflow" : "Guard Page Violation";

        __atomic_fetch_add(&g_stat_overflows, 1, __ATOMIC_RELAXED);
        snprintf(buf, sizeof(buf),
                 "GWP-ASAN: %s!\n"
                 "  Fault Addr : 0x%016lx\n"
                 "  Object     : 0x%016lx (size=%lu)\n"
                 "  Alloc by   : 0x%016lx",
                 kind,
                 (unsigned long)fault_addr,
                 (unsigned long)(uintptr_t)s->user_ptr,
                 (unsigned long)s->user_size,
                 (unsigned long)s->alloc_caller);
        panic_at("GWP-ASAN", 0, KERR_HEAP_OVERFLOW, buf);
    } else {
        snprintf(buf, sizeof(buf),
                 "GWP-ASAN: Access to unallocated GWP slot %d (addr=0x%lx)",
                 slot, (unsigned long)fault_addr);
        panic_at("GWP-ASAN", 0, KERR_MEM_CORRUPT, buf);
    }

    __builtin_unreachable();
}

void gwp_asan_print_stats(void) {
    char buf[256];
    snprintf(buf, sizeof(buf),
             "[GWP-ASAN] Sampled: %lu | Overflows: %lu | UAF: %lu\n",
             (unsigned long)g_stat_sampled,
             (unsigned long)g_stat_overflows,
             (unsigned long)g_stat_uaf);
    serial_write(buf);
    printf("%s", buf);
}