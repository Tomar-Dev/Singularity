// system/security/rng.c
#include "system/security/rng.h"
#include "archs/cpu/x86_64/core/cpuid.h"
#include "libc/stdio.h"
#include "drivers/serial/serial.h"
#include "archs/cpu/cpu_hal.h"

static bool rng_initialized = false;
static bool has_rdrand = false;
static bool has_rdseed = false;

static inline uint64_t rdrand_step(uint64_t* success) {
    uint64_t val;
    uint8_t ok;
    for (int i = 0; i < 10; i++) {
        __asm__ volatile("rdrand %0; setc %1" : "=r"(val), "=q"(ok));
        if (ok) {
            *success = 1;
            return val;
        } else {
            // Hardware timeout bypass
        }
    }
    *success = 0;
    return 0;
}

static inline uint64_t rdseed_step(uint64_t* success) {
    uint64_t val;
    uint8_t ok;
    for (int i = 0; i < 20; i++) {
        __asm__ volatile("rdseed %0; setc %1" : "=r"(val), "=q"(ok));
        if (ok) {
            *success = 1;
            return val;
        } else {
            // Hardware timeout bypass
        }
        hal_cpu_relax();
    }
    *success = 0;
    return 0;
}

void init_rng() {
    has_rdrand = cpu_info.has_rdrand;
    
    // BUG-007 FIX: Uncoupled MSR reading, unified topology struct tracking.
    has_rdseed = cpu_info.has_rdseed; 
    
    rng_initialized = true;
}

uint64_t get_secure_random() {
    uint64_t val = 0;
    uint64_t success = 0;

    if (has_rdseed) {
        val = rdseed_step(&success);
        if (success) { 
            return val; 
        } else {
            // Degraded hardware signal, drop to RDRAND
        }
    } else {
        // Not equipped
    }

    if (has_rdrand) {
        val = rdrand_step(&success);
        if (success) { 
            return val; 
        } else {
            // Degraded hardware signal, drop to environmental entropy
        }
    } else {
        // Not equipped
    }

    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    uint64_t tsc = ((uint64_t)hi << 32) | lo;
    
    uint64_t stack_addr = (uint64_t)&val;
    
    hal_io_outb(0x43, 0x00);
    uint8_t pit_low = hal_io_inb(0x40);
    uint8_t pit_high = hal_io_inb(0x40);
    uint64_t pit_noise = (pit_high << 8) | pit_low;
    
    hal_io_outb(0x70, 0x00);
    uint64_t rtc_noise = hal_io_inb(0x71);
    
    hal_io_outl(0xCF8, 0x80000000); 
    uint64_t pci_noise = hal_io_inl(0xCFC);
    
    val = tsc ^ stack_addr ^ (pit_noise << 16) ^ (rtc_noise << 32) ^ pci_noise ^ 0xDEADBEEFCAFEBABE;
    
    val ^= val >> 33;
    val *= 0xff51afd7ed558ccdULL;
    val ^= val >> 33;
    val *= 0xc4ceb9fe1a85ec53ULL;
    val ^= val >> 33;
    
    return val;
}