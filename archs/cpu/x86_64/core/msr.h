// archs/cpu/x86_64/core/msr.h
#ifndef MSR_H
#define MSR_H

#include <stdint.h>

#define MSR_IA32_TSC            0x10
#define MSR_IA32_APIC_BASE      0x1B
#define MSR_IA32_MPERF          0xE7  
#define MSR_IA32_APERF          0xE8
#define MSR_IA32_MTRR_CAP       0xFE
#define MSR_IA32_PAT            0x277 
#define MSR_IA32_THERM_STATUS   0x19C
#define MSR_IA32_MISC_ENABLE    0x1A0
#define MSR_IA32_POWER_CTL      0x1FC 

#define PAT_TYPE_UC  0x00
#define PAT_TYPE_WC  0x01
#define PAT_TYPE_WT  0x04
#define PAT_TYPE_WP  0x05
#define PAT_TYPE_WB  0x06
#define PAT_TYPE_UC_ 0x07

#define MSR_TURBO_RATIO_LIMIT   0x1AD
#define MSR_PLATFORM_INFO       0xCE
#define MSR_CONFIG_TDP_CONTROL  0x648
#define MSR_TURBO_ACTIVATION_RATIO 0x64C

#define MSR_PM_ENABLE           0x770  
#define MSR_HWP_CAPABILITIES    0x771  
#define MSR_HWP_REQUEST_PKG     0x772
#define MSR_HWP_INTERRUPT       0x773
#define MSR_HWP_REQUEST         0x774  
#define MSR_HWP_STATUS          0x777

#define HWP_EPP_PERFORMANCE     0x00
#define HWP_EPP_BALANCE_PERF    0x80
#define HWP_EPP_BALANCE_POWERSAVE 0xC0
#define HWP_EPP_POWERSAVE       0xFF

#define MSR_RAPL_POWER_UNIT     0x606
#define MSR_PKG_ENERGY_STATUS   0x611  

#define MSR_AMD_CPPC_CAP1       0xC00102B0 
#define MSR_AMD_CPPC_ENABLE     0xC00102B1 
#define MSR_AMD_CPPC_CAP2       0xC00102B2
#define MSR_AMD_CPPC_REQ        0xC00102B3 
#define MSR_AMD_CPPC_STATUS     0xC00102B4
#define MSR_AMD_PSTATE_STATUS   0xC0010063 
#define MSR_AMD_PSTATE_CTL      0xC0010062
#define MSR_K7_HWCR             0xC0010015

#define AMD_CPPC_EPP_PERF       0x00
#define AMD_CPPC_EPP_BALANCE    0x80
#define AMD_CPPC_EPP_POWERSAVE  0xFF

#define HWP_HIGHEST_PERF(x)     (((x) >> 24) & 0xFF)
#define HWP_GUARANTEED_PERF(x)  (((x) >> 8) & 0xFF)
#define HWP_MOSTEFFICIENT_PERF(x) (((x) >> 16) & 0xFF)
#define HWP_LOWEST_PERF(x)      ((x) & 0xFF)

#define AMD_CPPC_HIGHEST_PERF(x)    (((x) >> 24) & 0xFF)
#define AMD_CPPC_NOMINAL_PERF(x)    (((x) >> 16) & 0xFF)
#define AMD_CPPC_LOWEST_PERF(x)     (((x) >> 0) & 0xFF)

#define MSR_EFER                0xC0000080 
#define MSR_STAR                0xC0000081
#define MSR_LSTAR               0xC0000082
#define MSR_SFMASK              0xC0000084
#define MSR_FS_BASE             0xC0000100
#define MSR_GS_BASE             0xC0000101 
#define MSR_KERNEL_GS_BASE      0xC0000102 

#define MSR_EFER_NXE            (1 << 11)
#define MSR_EFER_LME            (1 << 8)
#define MSR_EFER_SCE            (1 << 0)

#define CR4_SMEP                (1 << 20)
#define CR4_SMAP                (1 << 21)

__attribute__((no_stack_protector))
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

__attribute__((no_stack_protector))
static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    __asm__ volatile("wrmsr" : : "a"(low), "d"(high), "c"(msr));
}

#endif
