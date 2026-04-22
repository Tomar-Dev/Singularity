// archs/cpu/x86_64/memory/pat.c
#include "archs/cpu/x86_64/core/msr.h"
#include "libc/stdio.h"
#include "archs/cpu/cpu_hal.h"
#include "archs/cpu/x86_64/core/cpuid.h"
void init_pat() {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    
    if (!(edx & (1 << 16))) {
        serial_write("[CPU] PAT not supported. Graphics performance may be low.\n");
        return;
    }
    
    uint64_t new_pat = 0x0007010600070106ULL;
    wrmsr(MSR_IA32_PAT, new_pat);
}