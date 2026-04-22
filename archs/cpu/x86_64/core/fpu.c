// archs/cpu/x86_64/core/fpu.c
#include "archs/cpu/x86_64/core/fpu.h"
#include "archs/cpu/x86_64/core/cpuid.h"
#include "drivers/serial/serial.h"
#include "kernel/config.h"
#include <stdint.h>
#include "libc/stdio.h"
#include "kernel/fastops.h" 

uint32_t fpu_default_mxcsr = FPU_DEFAULT_MXCSR;
uint32_t g_fpu_mode   = 0;
uint64_t g_xsave_mask = 0;

extern uint64_t fpu_init_native_asm();
extern void     fpu_load_mxcsr_asm(uint32_t val);

__attribute__((no_stack_protector))
void init_fpu() {
    if (!cpu_info.has_sse2) {
        if (get_cpu_id_fast() == 0) {
            serial_write("[FPU] CRITICAL: SSE2 not detected! FPU init skipped.\n");
        } else {
            // Suppress duplicate logs on APs
        }
        return;
    } else {
        // SSE2 is available, proceed with initialization
    }

    g_xsave_mask = fpu_init_native_asm();

    if (g_xsave_mask > 0 && g_xsave_mask != 0xFFFFFFFFFFFFFFFFULL) {
        bool has_avx = (g_xsave_mask & (1ULL << 2)) != 0;

        if (!has_avx) {
            g_fpu_mode = 0;
            cpu_info.has_xsave  = false;
            cpu_info.has_avx    = false;
            cpu_info.has_avx2   = false;
            cpu_info.has_avx512 = false;
        } else {
            g_fpu_mode = 1; 

            if (cpu_info.max_std_func >= 0xD) {
                uint32_t a, b, c, d;
                __asm__ volatile("cpuid"
                    : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                    : "a"(0xD), "c"(1));

                if (a & (1 << 0)) {
                    g_fpu_mode = 2;
                } else {
                    // XSAVEOPT not supported, stick to standard XSAVE
                }
            } else {
                // CPUID leaf 0xD not supported
            }

            cpu_info.has_xsave = true;
            cpu_info.has_avx   = true;

            if (g_xsave_mask & (1ULL << 4)) { cpu_info.has_avx2 = true; } else { /* AVX2 absent */ }
            if (g_xsave_mask & (1ULL << 5)) { cpu_info.has_avx512 = true; } else { /* AVX512 absent */ }
        }
    } else {
        // Fallback to basic FXSAVE/FXRSTOR if XSAVE is unsupported or returned garbage
        g_fpu_mode = 0;
        g_xsave_mask = 0;
        cpu_info.has_xsave  = false;
        cpu_info.has_avx    = false;
        cpu_info.has_avx2   = false;
        cpu_info.has_avx512 = false;
    }

    fpu_load_mxcsr_asm(fpu_default_mxcsr);
}