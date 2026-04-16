// archs/cpu/x86_64/memory/mtrr.c
#include "archs/cpu/x86_64/memory/mtrr.h"
#include "archs/cpu/x86_64/core/msr.h"
#include "libc/stdio.h"
#include "drivers/serial/serial.h"
#include "archs/cpu/x86_64/core/cpuid.h"

#define MSR_MTRR_CAP        0xFE
#define MSR_MTRR_DEF_TYPE   0x2FF
#define MSR_MTRR_PHYS_BASE0 0x200
#define MSR_MTRR_PHYS_MASK0 0x201

#define MTRR_TYPE_UC 0x00
#define MTRR_TYPE_WC 0x01
#define MTRR_TYPE_WT 0x04
#define MTRR_TYPE_WP 0x05
#define MTRR_TYPE_WB 0x06

static uint64_t calculate_mask(uint64_t length, uint8_t phys_addr_bits) {
    uint64_t mask = ~(length - 1);
    uint64_t phys_mask = (1ULL << phys_addr_bits) - 1;
    return mask & phys_mask;
}

static uint8_t get_phys_addr_bits() {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000008));
    return eax & 0xFF;
}

void mtrr_set_wc(uint64_t base, uint64_t length) {
    if (!cpu_info.has_msr) return;
    
    uint64_t cap = rdmsr(MSR_MTRR_CAP);
    if (!(cap & (1 << 8))) {
        serial_write("[MTRR] Write-Combining not supported by CPU.\n");
        return;
    }
    
    uint8_t vcnt = cap & 0xFF;
    uint8_t phys_bits = get_phys_addr_bits();
    
    uint64_t aligned_len = 1ULL << (63 - __builtin_clzll(length));
    
    if (base % aligned_len != 0) {
        serial_write("[MTRR] Warning: Base address not aligned to length. Skipping.\n");
        return;
    }

    int free_slot = -1;
    for (int i = 0; i < vcnt; i++) {
        uint64_t mask_msr = rdmsr(MSR_MTRR_PHYS_MASK0 + (i * 2));
        if (!(mask_msr & (1 << 11))) {
            free_slot = i;
            break;
        }
        
        uint64_t base_msr = rdmsr(MSR_MTRR_PHYS_BASE0 + (i * 2));
        if ((base_msr & ~0xFFF) == base && (base_msr & 0xFF) == MTRR_TYPE_WC) {
            return;
        }
    }

    if (free_slot == -1) {
        serial_write("[MTRR] No free MTRR slots available.\n");
        return;
    }

    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0; cli; wbinvd" : "=r"(rflags) :: "memory");
    
    uint64_t deftype = rdmsr(MSR_MTRR_DEF_TYPE);
    wrmsr(MSR_MTRR_DEF_TYPE, deftype & ~(1 << 11));

    wrmsr(MSR_MTRR_PHYS_BASE0 + (free_slot * 2), base | MTRR_TYPE_WC);
    
    uint64_t mask = calculate_mask(aligned_len, phys_bits);
    wrmsr(MSR_MTRR_PHYS_MASK0 + (free_slot * 2), mask | (1 << 11));

    wrmsr(MSR_MTRR_DEF_TYPE, deftype | (1 << 11));
    
    __asm__ volatile("wbinvd" ::: "memory");
    if (rflags & 0x200) {
        __asm__ volatile("sti" ::: "memory");
    }
}