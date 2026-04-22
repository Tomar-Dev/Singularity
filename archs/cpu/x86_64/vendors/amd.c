// archs/cpu/x86_64/vendors/amd.c
#include "archs/cpu/x86_64/core/cpuid.h"
#include "archs/cpu/x86_64/core/msr.h"
#include "libc/stdio.h"
#include "archs/cpu/cpu_hal.h"
static bool has_cppc = false;
static bool boost_supported = false;

static uint8_t cpu_highest_perf = 0;
static uint8_t cpu_nominal_perf = 0;
static uint8_t cpu_lowest_perf = 0;

static void apply_amd_quirks() {
    if (cpu_info.family == 0x17 && cpu_nominal_perf == 0) {
        serial_write("[AMD] Quirk Applied: Fixed missing nominal perf values (EPYC 7K62 style).\n");
        cpu_nominal_perf = 0x64;
        cpu_lowest_perf = 0x10;
    }
}

static void amd_init() {
    serial_write("[CPU] Initializing AMD Driver (CPPC/Zen)...\n");

    // 1. Check CPB (Core Performance Boost) - CPUID 0x80000007
    uint64_t hwcr = rdmsr(MSR_K7_HWCR);
    if (!(hwcr & (1 << 25))) {
        boost_supported = true;
        serial_write("[AMD] Core Performance Boost (Turbo) is Active.\n");
    } else {
        serial_write("[AMD] Warning: CPB Disabled by BIOS.\n");
    }

    uint32_t a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000008));
    if (b & (1 << 27)) {
        has_cppc = true;
        cpu_info.has_cppc = true;
        
        wrmsr(MSR_AMD_CPPC_ENABLE, 1);
        
        // Read Capabilities (MSR 0xC00102B0)
        uint64_t cap = rdmsr(MSR_AMD_CPPC_CAP1);
        cpu_highest_perf = AMD_CPPC_HIGHEST_PERF(cap);
        cpu_nominal_perf = AMD_CPPC_NOMINAL_PERF(cap);
        cpu_lowest_perf  = AMD_CPPC_LOWEST_PERF(cap);
        
        apply_amd_quirks();
        
        serial_printf("[AMD] CPPC Active. Caps: High=%d Nom=%d Low=%d\n", 
            cpu_highest_perf, cpu_nominal_perf, cpu_lowest_perf);
            
        if (cpu_highest_perf > 166) { 
             serial_write("[AMD] High Performance Core Detected (Preferred Core Candidate).\n");
        }
    } else {
        serial_write("[AMD] CPPC Not Supported. Using Legacy P-States.\n");
    }
}

static void amd_set_policy(cpu_power_policy_t policy) {
    if (!has_cppc) {
        // Legacy P-State Fallback (MSR 0xC0010062)
        uint64_t ctrl = rdmsr(MSR_AMD_PSTATE_CTL);
        ctrl &= ~0x7;
        if (policy == CPU_POLICY_PERFORMANCE) ctrl |= 0;
        else if (policy == CPU_POLICY_POWERSAVE) ctrl |= 2;
        else ctrl |= 1;
        wrmsr(MSR_AMD_PSTATE_CTL, ctrl);
        return;
    }

    uint8_t min_perf = cpu_lowest_perf;
    uint8_t max_perf = boost_supported ? cpu_highest_perf : cpu_nominal_perf;
    uint8_t des_perf = 0;
    uint8_t epp = AMD_CPPC_EPP_BALANCE;

    if (policy == CPU_POLICY_PERFORMANCE) {
        epp = AMD_CPPC_EPP_PERF;
        min_perf = cpu_nominal_perf;
    } else if (policy == CPU_POLICY_POWERSAVE) {
        epp = AMD_CPPC_EPP_POWERSAVE;
        max_perf = cpu_nominal_perf;
    }

    uint64_t req = 0;
    req |= ((uint64_t)epp << 24);
    req |= ((uint64_t)des_perf << 16);
    req |= ((uint64_t)min_perf << 8);
    req |= ((uint64_t)max_perf);

    wrmsr(MSR_AMD_CPPC_REQ, req);
    serial_printf("[AMD] Policy Set: EPP=%d Range=%d-%d\n", epp, min_perf, max_perf);
}

static uint64_t amd_get_freq() {
    uint64_t status = rdmsr(MSR_AMD_PSTATE_STATUS);
    uint64_t fid = status & 0xFF;
    return fid * 25; 
}

static int amd_get_temp() {
    return 0;
}

cpu_driver_t cpu_driver_amd = {
    .driver_name = "AMD Enhanced (Zen CPPC)",
    .init = amd_init,
    .get_temp = amd_get_temp,
    .set_policy = amd_set_policy,
    .get_power_usage = NULL,
    .get_current_freq = amd_get_freq
};
