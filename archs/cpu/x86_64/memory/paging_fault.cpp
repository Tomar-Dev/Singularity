// archs/cpu/x86_64/memory/paging_fault.cpp
#include "memory/paging.h"
#include "memory/pmm.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "drivers/serial/serial.h"
#include "kernel/debug.h"
#include "archs/cpu/x86_64/interrupts/isr.h"
#include "system/security/gwp_asan.h" 

extern "C" uint64_t cr3_read_pcid(void);

extern "C" void tlb_flush_handler(registers_t* regs) {
    (void)regs;
    if (g_Paging && g_Paging->isPcidEnabled()) {
        uint64_t v = cr3_read_pcid() & ~(1ULL << 63);
        __asm__ volatile("mov %0, %%cr3" :: "r"(v) : "memory");
    } else {
        uint64_t cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        __asm__ volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
    }
}

extern volatile bool global_panic_active;

extern "C" void page_fault_handler(registers_t* regs) {
    if (global_panic_active) {
        dbg_direct("\n[FATAL] Page Fault during panic (Double Panic Prevention)\n");
        for (;;) { __asm__ volatile("cli; hlt" ::: "memory"); }
    }

    uint64_t addr;
    __asm__ volatile("mov %%cr2, %0" : "=r"(addr));

    bool present  = (regs->err_code & 0x01) != 0;
    bool write    = (regs->err_code & 0x02) != 0;
    bool user     = (regs->err_code & 0x04) != 0;
    bool reserved = (regs->err_code & 0x08) != 0;
    bool nx_fetch = (regs->err_code & 0x10) != 0;

    if (present && write && !user && !nx_fetch)
        if (g_Paging && g_Paging->handleCowFault(addr)) return;

    // GÜVENLİK YAMASI: GWP-ASAN Hook! Donanım tabanlı bellek zafiyeti yakalama.
    // Eğer taşma Kernel modunda gerçekleştiyse, GWP-ASAN donanımsal adresi
    // inceleyip Buffer Overflow veya Use-After-Free teşhisini koyar.
    if (!user && gwp_asan_is_managed((void*)addr)) {
        gwp_asan_check_fault(addr);
    }

    set_panic_registers(regs);
    const char* type =
        nx_fetch  ? "NX violation"        :
        reserved  ? "Reserved bit in PTE" :
        !present  ? "Not-present"         :
        write     ? "Write to RO page"    :
                    "Unknown";

    char buf[320];
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    snprintf(buf, sizeof(buf),
             "Fault addr : 0x%016lx\n"
             "RIP        : 0x%016lx\n"
             "Error      : 0x%lx[P:%d W:%d U:%d RSV:%d NX:%d]\n"
             "Type       : %s",
             addr, regs->rip, regs->err_code,
             (int)present, (int)write, (int)user,
             (int)reserved, (int)nx_fetch, type);
    panic_at("PAGING", 0, KERR_PAGE_FAULT, buf);
}

extern "C" void paging_enable_write_protect(void) {
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= (1u << 16);
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));
}

extern "C" void paging_protect_kernel(void) {
    if (!g_Paging) return;

    extern char __text_start[], __text_end[];
    extern char __rodata_start[], __rodata_end[];
    extern char __data_start[], __bss_end[];

    uint64_t ts = (uint64_t)__text_start, te = (uint64_t)__text_end;
    if (!ts || !te || te <= ts) {
        serial_write("[PAGING] W^X: bad linker symbols\n"); return;
    }

    uint64_t rs = (uint64_t)__rodata_start, re = (uint64_t)__rodata_end;
    uint64_t ds = (uint64_t)__data_start,   de = (uint64_t)__bss_end;
    const uint64_t A2 = ~0x1FFFFFULL, A4 = ~0xFFFULL;

    for (uint64_t a = ts & A2; a < ((de + 0x1FFFFF) & A2); a += PAGE_SIZE_LARGE)
        g_Paging->splitHugePage(a);

    for (uint64_t a = ts & A4; a < te; a += PAGE_SIZE)
        g_Paging->updatePageFlags(a, 0, PAGE_WRITE | PAGE_NX);

    if (rs && re > rs)
        for (uint64_t a = rs & A4; a < re; a += PAGE_SIZE)
            g_Paging->updatePageFlags(a, PAGE_NX, PAGE_WRITE);

    if (ds && de > ds)
        for (uint64_t a = ds & A4; a < de; a += PAGE_SIZE)
            g_Paging->updatePageFlags(a, PAGE_NX, 0);
}