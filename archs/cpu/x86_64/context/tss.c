// archs/cpu/x86_64/context/tss.c
#include "archs/cpu/x86_64/context/tss.h"
#include "archs/cpu/x86_64/gdt/gdt.h"
#include "libc/string.h"
#include "memory/kheap.h"
#include "memory/paging.h" 
#include "libc/stdio.h"
#include "kernel/debug.h"
#include "archs/cpu/x86_64/smp/smp.h"

extern void print_status(const char* prefix, const char* msg, const char* status);

static tss_entry_t tss_entries[MAX_CPUS];

void tss_install(uint8_t cpu_id, uint64_t kernel_stack) {
    if (cpu_id >= MAX_CPUS) {
        panic_at("TSS", 0, KERR_UNKNOWN, "Invalid CPU ID for TSS install");
    }

    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    memset(&tss_entries[cpu_id], 0, sizeof(tss_entry_t));
    
    tss_entries[cpu_id].rsp0 = kernel_stack;
    
    // GÜVENLİK YAMASI: IST (Double Fault & NMI) yığınları artık kmalloc ile değil,
    // VMM (vmm_alloc_stack) kullanılarak sonlarında 'Not Present' Guard Page ile 
    // birlikte tahsis ediliyor. Böylece #DF handler'ın kendisi de taşarsa
    // Kernel Heap Metadata'sı bozulmayıp sistem Triple Fault yemeden güvenlice yakalanacak.
    void* ist1_ptr = vmm_alloc_stack(1);
    if (ist1_ptr) {
        tss_entries[cpu_id].ist1 = (uint64_t)ist1_ptr + 4096;
    } else {
        panic_at("TSS", 0, KERR_MEM_OOM, "CRITICAL: Failed to allocate IST1 Stack!");
    }

    void* ist2_ptr = vmm_alloc_stack(1);
    if (ist2_ptr) {
        tss_entries[cpu_id].ist2 = (uint64_t)ist2_ptr + 4096;
    } else {
        panic_at("TSS", 0, KERR_MEM_OOM, "CRITICAL: Failed to allocate IST2 Stack!");
    }
    
    tss_entries[cpu_id].iomap_base = sizeof(tss_entry_t);

    gdt_set_tss_gate(cpu_id, 5, (uint64_t)&tss_entries[cpu_id], sizeof(tss_entry_t) - 1);

    tss_flush(); 
    
    if (cpu_id == 0) {
        print_status("[ TSS  ]", "TSS Installed for BSP (IST Active)", "INFO");
    }
}

void set_kernel_stack(uint8_t cpu_id, uint64_t stack) {
    if (cpu_id < MAX_CPUS) {
        tss_entries[cpu_id].rsp0 = stack;
    }
}