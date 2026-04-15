// archs/cpu/x86_64/core/cpuid.h
#ifndef CPUID_H
#define CPUID_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VENDOR_UNKNOWN = 0,
    VENDOR_INTEL,
    VENDOR_AMD,
    VENDOR_VIA,
    VENDOR_HYGON,
    VENDOR_VMWARE,
    VENDOR_QEMU,
    VENDOR_VIRTUALBOX,
    VENDOR_GENERIC
} cpu_vendor_t;

typedef enum {
    CPU_POLICY_PERFORMANCE = 0,
    CPU_POLICY_BALANCED    = 1,
    CPU_POLICY_POWERSAVE   = 2
} cpu_power_policy_t;

typedef struct {
    char vendor_string[13];
    char brand_string[65];
    cpu_vendor_t vendor;

    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    uint32_t ext_family;
    uint32_t ext_model;

    char codename[32];
    char socket[32];
    char lithography[16];

    uint32_t physical_cores;
    uint32_t logical_cores;

    bool has_fpu; bool has_apic; bool has_msr; bool has_longmode;

    bool has_mmx;
    bool has_sse; bool has_sse2; bool has_sse3; bool has_ssse3;
    bool has_sse4_1; bool has_sse4_2; bool has_sse4a;
    bool has_avx; bool has_avx2; bool has_avx512; bool has_fma3;
    bool has_aes; bool has_sha;

    bool has_rdrand; bool has_xsave;
    bool has_vmx; bool has_svm; bool has_ht;
    bool has_dts; bool has_pdcm;
    bool has_xsaveopt;
    bool has_hwp;
    bool has_cppc;
    
    bool has_cet_ss;
    bool has_cet_ibt;

    bool is_hypervisor;

    uint32_t l1_cache_size;
    uint32_t l2_cache_size;
    uint32_t l3_cache_size;

    bool has_padlock;
} cpu_info_t;

typedef struct {
    const char* driver_name;
    void (*init)(void);
    int  (*get_temp)(void);
    void (*set_policy)(cpu_power_policy_t policy);
    uint64_t (*get_power_usage)(void);
    uint64_t (*get_current_freq)(void);
} cpu_driver_t;

extern cpu_info_t cpu_info;
extern cpu_driver_t* current_cpu_driver;

void detect_cpu();
void print_cpu_z_info();

#ifdef __cplusplus
}
#endif

#endif
