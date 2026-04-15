// archs/cpu/x86_64/vendors/intel.c
#include "archs/cpu/x86_64/core/cpuid.h"
#include "archs/cpu/x86_64/core/msr.h"
#include "libc/stdio.h"
#include "drivers/serial/serial.h"
extern uint64_t timer_get_ticks();

#define INTEL_PSTATE_CORE_SCALING 100000
#define HYBRID_SCALING_FACTOR_ADL 78741
#define HYBRID_SCALING_FACTOR_MTL 80000
#define HYBRID_SCALING_FACTOR_LNL 86957

static bool has_hwp = false;
static bool hwp_is_hybrid = false;
static int hybrid_scaling_factor = 0;
static bool turbo_disabled_by_bios = false;

static uint32_t max_non_turbo_ratio = 0;
static uint32_t max_turbo_ratio = 0; 
static uint32_t min_ratio = 0;

static bool has_rapl = false;
static uint32_t rapl_energy_unit = 0;
static uint64_t last_energy_status = 0;
static uint64_t last_energy_time = 0;

static bool is_hybrid_cpu() {
    uint32_t a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(7), "c"(0));
    return (d & (1 << 15));
}

static int get_cpu_type() {
    uint32_t a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x1A), "c"(0));
    return (a >> 24);
}

static void apply_kabylake_quirk() {
    if (cpu_info.family == 6 && (cpu_info.model == 0x8E || cpu_info.model == 0x9E)) {
        uint64_t power_ctl = rdmsr(MSR_IA32_POWER_CTL);
        if (power_ctl & (1 << 1)) { 
            power_ctl &= ~(1 << 1); 
            wrmsr(MSR_IA32_POWER_CTL, power_ctl);
            serial_write("[INTEL] Quirk Applied: Disabled broken Energy Efficiency on Kaby Lake.\n");
        }
    }
}

static void intel_get_turbo_limits() {
    uint64_t plat_info = rdmsr(MSR_PLATFORM_INFO);
    max_non_turbo_ratio = (plat_info >> 8) & 0xFF;
    min_ratio = (plat_info >> 40) & 0xFF; 

    uint64_t turbo_limits = rdmsr(MSR_TURBO_RATIO_LIMIT);
    max_turbo_ratio = turbo_limits & 0xFF; 

    serial_printf("[INTEL] Ratios: Base=%d, Turbo=%d, Min=%d\n", 
        max_non_turbo_ratio, max_turbo_ratio, min_ratio);
}

static uint64_t intel_get_power_usage() {
    if (!has_rapl) return 0;

    uint64_t current_energy = rdmsr(MSR_PKG_ENERGY_STATUS) & 0xFFFFFFFF;
    uint64_t current_time = timer_get_ticks(); 
    
    if (last_energy_time == 0) {
        last_energy_time = current_time;
        last_energy_status = current_energy;
        return 0;
    }

    uint64_t delta_time = current_time - last_energy_time;
    if (delta_time < 10) return 0; 

    uint64_t delta_energy;
    if (current_energy >= last_energy_status) delta_energy = current_energy - last_energy_status;
    else delta_energy = (0xFFFFFFFF - last_energy_status) + current_energy; 

    uint64_t joules_x_1000 = (delta_energy * 1000) >> rapl_energy_unit;
    uint64_t time_ms = delta_time * 10;
    
    uint64_t power_mw = (joules_x_1000 * 1000) / time_ms;

    last_energy_time = current_time;
    last_energy_status = current_energy;

    return power_mw;
}

static void intel_init() {
    serial_write("[CPU] Initializing Intel Driver (v2.5 Enterprise)...\n");

    uint64_t misc = rdmsr(MSR_IA32_MISC_ENABLE);
    if (misc & (1ULL << 38)) {
        turbo_disabled_by_bios = true;
        serial_write("[INTEL] Warning: Turbo Boost disabled by BIOS.\n");
    }

    intel_get_turbo_limits();

    uint32_t a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(6));
    if (a & (1 << 7)) {
        has_hwp = true;
        uint64_t pm_en = rdmsr(MSR_PM_ENABLE);
        if (!(pm_en & 1)) {
            wrmsr(MSR_PM_ENABLE, 1);
            serial_write("[INTEL] HWP (Speed Shift) Enabled.\n");
        }
        
        uint64_t caps = rdmsr(MSR_HWP_CAPABILITIES);
        serial_printf("[INTEL] HWP Caps: High=%d, Guar=%d, Eff=%d, Low=%d\n",
            HWP_HIGHEST_PERF(caps), HWP_GUARANTEED_PERF(caps),
            HWP_MOSTEFFICIENT_PERF(caps), HWP_LOWEST_PERF(caps));
    }

    if (is_hybrid_cpu()) {
        hwp_is_hybrid = true;
        if (cpu_info.model == 0x97 || cpu_info.model == 0x9A) {
            hybrid_scaling_factor = HYBRID_SCALING_FACTOR_ADL;
        } else if (cpu_info.model == 0xAA || cpu_info.model == 0xAC) {
            hybrid_scaling_factor = HYBRID_SCALING_FACTOR_MTL;
        } else {
            hybrid_scaling_factor = HYBRID_SCALING_FACTOR_ADL; 
        }
        serial_printf("[INTEL] Hybrid CPU Detected. Scaling Factor: %d\n", hybrid_scaling_factor);
    }

    if (cpu_info.family == 6 && cpu_info.model >= 0x2A) {
        has_rapl = true;
        uint64_t units = rdmsr(MSR_RAPL_POWER_UNIT);
        rapl_energy_unit = (units >> 8) & 0x1F;
        serial_write("[INTEL] RAPL Power Monitoring Active.\n");
    }

    apply_kabylake_quirk();
}

static void intel_set_policy(cpu_power_policy_t policy) {
    if (!has_hwp) return;

    uint8_t epp = 0x80; 
    uint8_t min_perf = 1;
    uint8_t max_perf = 255;

    uint64_t cap = rdmsr(MSR_HWP_CAPABILITIES);
    uint8_t hw_highest = HWP_HIGHEST_PERF(cap);
    uint8_t hw_guaranteed = HWP_GUARANTEED_PERF(cap);
    uint8_t hw_lowest = HWP_LOWEST_PERF(cap);

    if (policy == CPU_POLICY_PERFORMANCE) {
        epp = 0x00;
        min_perf = hw_guaranteed; 
        max_perf = hw_highest;
    } else if (policy == CPU_POLICY_POWERSAVE) {
        epp = 0xFF;
        min_perf = hw_lowest;
        max_perf = hw_guaranteed; 
    }

    if (turbo_disabled_by_bios && max_perf > hw_guaranteed) {
        max_perf = hw_guaranteed;
    }

    uint64_t req = rdmsr(MSR_HWP_REQUEST);
    req &= ~0xFFFFFFFFULL; 
    
    req |= (uint64_t)min_perf;
    req |= ((uint64_t)max_perf << 8);
    req |= ((uint64_t)epp << 24);
    
    wrmsr(MSR_HWP_REQUEST, req);
    serial_printf("[INTEL] Policy: %d (EPP: %d, Range: %d-%d)\n", policy, epp, min_perf, max_perf);
}

static uint64_t intel_get_freq() {
    if (hwp_is_hybrid) {
        uint64_t msr = rdmsr(0x198); 
        uint16_t ratio = (msr >> 8) & 0xFF;
        int type = get_cpu_type();
        
        if (type == 0x20) { 
             return ratio * 100; 
        }
        return ratio * 100;
    }
    
    uint64_t msr = rdmsr(0x198);
    return ((msr >> 8) & 0xFF) * 100;
}

static int intel_get_temp() {
    if (!cpu_info.has_msr) return 0;
    uint64_t msr = rdmsr(MSR_IA32_THERM_STATUS);
    if (msr & (1ULL << 31)) {
        return 100 - ((msr >> 16) & 0x7F);
    }
    return 0;
}

cpu_driver_t cpu_driver_intel = {
    .driver_name = "Intel Enhanced (Hybrid Aware)",
    .init = intel_init,
    .get_temp = intel_get_temp,
    .set_policy = intel_set_policy,
    .get_power_usage = intel_get_power_usage,
    .get_current_freq = intel_get_freq
};
