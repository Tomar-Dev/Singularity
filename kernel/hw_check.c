// kernel/hw_check.c
#include "archs/cpu/x86_64/core/cpuid.h"
#include "archs/cpu/cpu_hal.h"
#include "libc/stdio.h"
#include "kernel/debug.h"
#include "archs/cpu/x86_64/core/msr.h"
void hardware_integrity_check() {
    serial_write("[BOOT] Starting Hardware Integrity Check...\n");
    
    if (cpu_info.vendor == VENDOR_UNKNOWN) {
        serial_write("[BOOT] WARNING: Unknown CPU Vendor. Stability not guaranteed.\n");
    } else {
        serial_printf("[BOOT] CPU Vendor Verified: %s\n", cpu_info.vendor_string);
    }

    if (!cpu_info.has_longmode) {
        PANIC("CPU does not support Long Mode (x86_64)! Impossible state.");
    }

    if (!cpu_info.has_sse || !cpu_info.has_sse2) {
        PANIC("CPU does not support SSE/SSE2. Kernel relies on SSE optimizations.");
    }

    uint64_t efer = rdmsr(MSR_EFER);
    if (!(efer & MSR_EFER_NXE)) {
        serial_write("[BOOT] WARNING: NX (No-Execute) Bit is NOT enabled in EFER!\n");
    }

    if (!cpu_info.has_apic) {
        PANIC("No Local APIC detected. SMP requires APIC.");
    }
    
    if (cpu_info.has_cet_ss || cpu_info.has_cet_ibt) {
        serial_printf("[BOOT] Hardware CET (Control-flow Enforcement) is ACTIVE. (SS: %d, IBT: %d)\n", 
                      cpu_info.has_cet_ss, cpu_info.has_cet_ibt);
    } else {
        serial_write("[BOOT] Note: CPU lacks hardware CET. Compiler fallback (NOP) active.\n");
    }

    serial_write("[BOOT] Hardware Integrity Check PASSED.\n");
}
