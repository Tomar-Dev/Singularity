// archs/cpu/x86_64/core/cpuid.c
#include "archs/cpu/x86_64/core/cpuid.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "drivers/serial/serial.h"
#include "archs/cpu/x86_64/core/msr.h"
extern uint64_t timer_get_ticks();

extern cpu_driver_t cpu_driver_intel;
extern cpu_driver_t cpu_driver_amd;
extern cpu_driver_t cpu_driver_via;
extern cpu_driver_t cpu_driver_hygon;
extern cpu_driver_t cpu_driver_generic;

cpu_info_t cpu_info;
cpu_driver_t* current_cpu_driver = &cpu_driver_generic;

static inline void cpuid(uint32_t code, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(code));
}

static inline void cpuid_count(uint32_t code, uint32_t count, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(code), "c"(count));
}

static void guess_microarchitecture() {
    strncpy(cpu_info.codename, "Unknown", sizeof(cpu_info.codename)-1);
    strncpy(cpu_info.socket, "Unknown", sizeof(cpu_info.socket)-1);
    strncpy(cpu_info.lithography, "Unknown", sizeof(cpu_info.lithography)-1);

    if (cpu_info.vendor == VENDOR_INTEL) {
        if (cpu_info.family == 6) {
            switch (cpu_info.model) {
                case 0x4E: case 0x5E:
                    strncpy(cpu_info.codename, "Skylake", sizeof(cpu_info.codename)-1);
                    strncpy(cpu_info.lithography, "14 nm", sizeof(cpu_info.lithography)-1);
                    strncpy(cpu_info.socket, "LGA 1151", sizeof(cpu_info.socket)-1);
                    break;
                case 0x8E: case 0x9E:
                    strncpy(cpu_info.codename, "Kaby/Coffee Lake", sizeof(cpu_info.codename)-1);
                    strncpy(cpu_info.lithography, "14 nm", sizeof(cpu_info.lithography)-1);
                    strncpy(cpu_info.socket, "LGA 1151", sizeof(cpu_info.socket)-1);
                    break;
                case 0xA5: case 0xA6:
                    strncpy(cpu_info.codename, "Comet Lake", sizeof(cpu_info.codename)-1);
                    strncpy(cpu_info.lithography, "14 nm", sizeof(cpu_info.lithography)-1);
                    strncpy(cpu_info.socket, "LGA 1200", sizeof(cpu_info.socket)-1);
                    break;
                case 0x7D: case 0x7E: case 0x9D:
                    strncpy(cpu_info.codename, "Ice Lake", sizeof(cpu_info.codename)-1);
                    strncpy(cpu_info.lithography, "10 nm", sizeof(cpu_info.lithography)-1);
                    strncpy(cpu_info.socket, "LGA 1200 / BGA", sizeof(cpu_info.socket)-1);
                    break;
                case 0x8C: case 0x8D:
                    strncpy(cpu_info.codename, "Tiger Lake", sizeof(cpu_info.codename)-1);
                    strncpy(cpu_info.lithography, "10 nm (SuperFin)", sizeof(cpu_info.lithography)-1);
                    strncpy(cpu_info.socket, "BGA", sizeof(cpu_info.socket)-1);
                    break;
                case 0x97: case 0x9A:
                    strncpy(cpu_info.codename, "Alder Lake", sizeof(cpu_info.codename)-1);
                    strncpy(cpu_info.lithography, "Intel 7 (10 nm)", sizeof(cpu_info.lithography)-1);
                    strncpy(cpu_info.socket, "LGA 1700", sizeof(cpu_info.socket)-1);
                    break;
                case 0xB7: case 0xBA:
                    strncpy(cpu_info.codename, "Raptor Lake", sizeof(cpu_info.codename)-1);
                    strncpy(cpu_info.lithography, "Intel 7 (10 nm)", sizeof(cpu_info.lithography)-1);
                    strncpy(cpu_info.socket, "LGA 1700", sizeof(cpu_info.socket)-1);
                    break;
                case 0xAA: case 0xAC:
                    strncpy(cpu_info.codename, "Meteor Lake", sizeof(cpu_info.codename)-1);
                    strncpy(cpu_info.lithography, "Intel 4 (7 nm)", sizeof(cpu_info.lithography)-1);
                    strncpy(cpu_info.socket, "BGA", sizeof(cpu_info.socket)-1);
                    break;
                case 0x8F:
                    strncpy(cpu_info.codename, "Sapphire Rapids", sizeof(cpu_info.codename)-1);
                    strncpy(cpu_info.lithography, "Intel 7", sizeof(cpu_info.lithography)-1);
                    strncpy(cpu_info.socket, "LGA 4677", sizeof(cpu_info.socket)-1);
                    break;
                default:
                    strncpy(cpu_info.codename, "Intel Core (Unknown Model)", sizeof(cpu_info.codename)-1);
                    serial_printf("[CPUID] Notice: Unknown Intel Model %x detected.\n", cpu_info.model);
                    break;
            }
        } else {
            strncpy(cpu_info.codename, "Intel (Unknown Family)", sizeof(cpu_info.codename)-1);
            serial_printf("[CPUID] Notice: Unknown Intel Family %x detected.\n", cpu_info.family);
        }
    } else if (cpu_info.vendor == VENDOR_AMD) {
        if (cpu_info.family == 0x17) {
            if (cpu_info.model < 0x30) {
                strncpy(cpu_info.codename, "Zen / Zen+", sizeof(cpu_info.codename)-1);
                strncpy(cpu_info.lithography, "14/12 nm", sizeof(cpu_info.lithography)-1);
                strncpy(cpu_info.socket, "AM4", sizeof(cpu_info.socket)-1);
            } else {
                strncpy(cpu_info.codename, "Zen 2", sizeof(cpu_info.codename)-1);
                strncpy(cpu_info.lithography, "7 nm", sizeof(cpu_info.lithography)-1);
                strncpy(cpu_info.socket, "AM4 / SP3", sizeof(cpu_info.socket)-1);
            }
        } else if (cpu_info.family == 0x19) {
            if (cpu_info.model < 0x10) {
                strncpy(cpu_info.codename, "Zen 3", sizeof(cpu_info.codename)-1);
                strncpy(cpu_info.lithography, "7 nm", sizeof(cpu_info.lithography)-1);
                strncpy(cpu_info.socket, "AM4 / SP3", sizeof(cpu_info.socket)-1);
            } else if (cpu_info.model >= 0x60 && cpu_info.model < 0x70) {
                strncpy(cpu_info.codename, "Zen 4", sizeof(cpu_info.codename)-1);
                strncpy(cpu_info.lithography, "5 nm", sizeof(cpu_info.lithography)-1);
                strncpy(cpu_info.socket, "AM5 (LGA1718) / SP5", sizeof(cpu_info.socket)-1);
            } else {
                strncpy(cpu_info.codename, "Zen 4/5 (Unknown Model)", sizeof(cpu_info.codename)-1);
                serial_printf("[CPUID] Notice: Unknown AMD Family 0x19 Model %x detected.\n", cpu_info.model);
            }
        } else if (cpu_info.family == 0x1A) {
            strncpy(cpu_info.codename, "Zen 5", sizeof(cpu_info.codename)-1);
            strncpy(cpu_info.lithography, "4 nm", sizeof(cpu_info.lithography)-1);
            strncpy(cpu_info.socket, "AM5 / SP5", sizeof(cpu_info.socket)-1);
        } else {
            strncpy(cpu_info.codename, "AMD (Unknown Family)", sizeof(cpu_info.codename)-1);
            serial_printf("[CPUID] Notice: Unknown AMD Family %x detected.\n", cpu_info.family);
        }
    } else {
        strncpy(cpu_info.codename, "Generic Processor", sizeof(cpu_info.codename)-1);
    }

    if (cpu_info.is_hypervisor) {
        strncpy(cpu_info.codename, "Virtual CPU", sizeof(cpu_info.codename)-1);
        strncpy(cpu_info.socket, "Virtual Socket", sizeof(cpu_info.socket)-1);
        strncpy(cpu_info.lithography, "N/A", sizeof(cpu_info.lithography)-1);
    } else {}
    
    cpu_info.codename[sizeof(cpu_info.codename)-1] = '\0';
    cpu_info.socket[sizeof(cpu_info.socket)-1] = '\0';
    cpu_info.lithography[sizeof(cpu_info.lithography)-1] = '\0';
}

static void detect_cache_topology() {
    uint32_t a, b, c, d;
    cpu_info.l1_cache_size = 0;
    cpu_info.l2_cache_size = 0;
    cpu_info.l3_cache_size = 0;

    if (cpu_info.vendor == VENDOR_INTEL) {
        for (int i = 0; i < 10; i++) {
            cpuid_count(4, i, &a, &b, &c, &d);
            int type = a & 0x1F;
            if (type == 0) break;

            int level = (a >> 5) & 0x7;
            int sets = c + 1;
            int partitions = ((b >> 12) & 0x3FF) + 1;
            int ways = ((b >> 22) & 0x3FF) + 1;
            int line_size = (b & 0xFFF) + 1;

            uint32_t size_kb = (sets * partitions * ways * line_size) / 1024;

            if (level == 1) cpu_info.l1_cache_size += size_kb;
            else if (level == 2) cpu_info.l2_cache_size = size_kb;
            else if (level == 3) cpu_info.l3_cache_size = size_kb;
            else {}
        }
    } else if (cpu_info.vendor == VENDOR_AMD) {
        cpuid(0x80000005, &a, &b, &c, &d);
        cpu_info.l1_cache_size = ((c >> 24) & 0xFF) + ((d >> 24) & 0xFF);

        cpuid(0x80000006, &a, &b, &c, &d);
        cpu_info.l2_cache_size = (c >> 16) & 0xFFFF;
        cpu_info.l3_cache_size = (d >> 18) * 512;
    } else {
    }
}

static void detect_core_topology() {
    uint32_t a, b, c, d;

    cpu_info.physical_cores = 1;
    cpu_info.logical_cores  = 1;

    if (cpu_info.vendor == VENDOR_INTEL) {
        uint32_t max_leaf;
        cpuid(0, &max_leaf, &b, &c, &d);

        if (max_leaf >= 0xB) {
            uint32_t logical_per_core = 0;
            uint32_t logical_total    = 0;

            cpuid_count(0xB, 0, &a, &b, &c, &d);
            if ((c & 0xFF00) >> 8 == 1) { 
                logical_per_core = b & 0xFFFF;
            } else {}

            cpuid_count(0xB, 1, &a, &b, &c, &d);
            if ((c & 0xFF00) >> 8 == 2) { 
                logical_total = b & 0xFFFF;
            } else {}

            if (logical_per_core > 0 && logical_total > 0) {
                cpu_info.logical_cores  = logical_total;
                cpu_info.physical_cores = logical_total / logical_per_core;
                if (cpu_info.physical_cores == 0) cpu_info.physical_cores = 1; else {}
            } else {
                cpuid(1, &a, &b, &c, &d);
                uint32_t max_logical = (b >> 16) & 0xFF;
                cpu_info.logical_cores  = (max_logical > 0) ? max_logical : 1;
                cpu_info.physical_cores = cpu_info.has_ht ? (cpu_info.logical_cores / 2) : cpu_info.logical_cores;
                if (cpu_info.physical_cores == 0) cpu_info.physical_cores = 1; else {}
            }
        } else {
            cpuid(1, &a, &b, &c, &d);
            uint32_t max_logical = (b >> 16) & 0xFF;
            cpu_info.logical_cores  = (max_logical > 0) ? max_logical : 1;
            cpu_info.physical_cores = cpu_info.has_ht ? (cpu_info.logical_cores / 2) : cpu_info.logical_cores;
            if (cpu_info.physical_cores == 0) cpu_info.physical_cores = 1; else {}
        }

    } else if (cpu_info.vendor == VENDOR_AMD || cpu_info.vendor == VENDOR_HYGON) {
        uint32_t max_ext;
        cpuid(0x80000000, &max_ext, &b, &c, &d);

        if (max_ext >= 0x80000008) {
            cpuid(0x80000008, &a, &b, &c, &d);
            uint32_t nc = (c & 0xFF) + 1; 

            uint32_t threads_per_core = 1;
            if (max_ext >= 0x8000001E) {
                cpuid(0x8000001E, &a, &b, &c, &d);
                threads_per_core = ((b >> 8) & 0xFF) + 1;
            } else {}

            cpu_info.physical_cores = nc;
            cpu_info.logical_cores  = nc * threads_per_core;
        } else {}
    } else {
    }
}

void detect_cpu() {
    uint32_t eax, ebx, ecx, edx;

    cpuid(0, &eax, &ebx, &ecx, &edx);
    uint32_t max_std_func = eax;

    memcpy(&cpu_info.vendor_string[0], &ebx, 4);
    memcpy(&cpu_info.vendor_string[4], &edx, 4);
    memcpy(&cpu_info.vendor_string[8], &ecx, 4);
    cpu_info.vendor_string[12] = '\0';

    if (strcmp(cpu_info.vendor_string, "GenuineIntel") == 0) { cpu_info.vendor = VENDOR_INTEL; }
    else if (strcmp(cpu_info.vendor_string, "AuthenticAMD") == 0) { cpu_info.vendor = VENDOR_AMD; }
    else if (strcmp(cpu_info.vendor_string, "HygonGenuine") == 0) { cpu_info.vendor = VENDOR_HYGON; }
    else if (strcmp(cpu_info.vendor_string, "CentaurHauls") == 0) { cpu_info.vendor = VENDOR_VIA; }
    else if (strstr(cpu_info.vendor_string, "VMware")) { cpu_info.vendor = VENDOR_VMWARE; }
    else if (strcmp(cpu_info.vendor_string, "TCGTCGTCGTCG") == 0) { cpu_info.vendor = VENDOR_QEMU; }
    else if (strcmp(cpu_info.vendor_string, "KVMKVMKVM") == 0) { cpu_info.vendor = VENDOR_QEMU; }
    else if (strcmp(cpu_info.vendor_string, "VBoxVBoxVBox") == 0) { cpu_info.vendor = VENDOR_VIRTUALBOX; }
    else { cpu_info.vendor = VENDOR_UNKNOWN; }

    if (max_std_func >= 7) {
        cpuid_count(7, 0, &eax, &ebx, &ecx, &edx);
        cpu_info.has_smep = (ebx & (1 << 7));  // GÜVENLİK YAMASI
        cpu_info.has_smap = (ebx & (1 << 20)); // GÜVENLİK YAMASI
    } else {}

    if (max_std_func >= 1) {
        cpuid(1, &eax, &ebx, &ecx, &edx);

        uint32_t stepping = eax & 0xF;
        uint32_t family   = (eax >> 8) & 0xF;
        uint32_t model    = (eax >> 4) & 0xF;

        if (family == 15) {
            family += (eax >> 20) & 0xFF;
            model  += ((eax >> 16) & 0xF) << 4;
        } else if (family == 6) {
            model += ((eax >> 16) & 0xF) << 4;
        } else {}

        cpu_info.family   = family;
        cpu_info.model    = model;
        cpu_info.stepping = stepping;
        cpu_info.ext_family = (eax >> 20) & 0xFF;
        cpu_info.ext_model  = (eax >> 16) & 0xF;

        cpu_info.has_fpu  = (edx & (1 << 0));
        cpu_info.has_msr  = (edx & (1 << 5));
        cpu_info.has_apic = (edx & (1 << 9));
        cpu_info.has_mmx  = (edx & (1 << 23));
        cpu_info.has_sse  = (edx & (1 << 25));
        cpu_info.has_sse2 = (edx & (1 << 26));
        cpu_info.has_ht   = (edx & (1 << 28));

        cpu_info.has_sse3   = (ecx & (1 << 0));
        cpu_info.has_vmx    = (ecx & (1 << 5));
        cpu_info.has_ssse3  = (ecx & (1 << 9));
        cpu_info.has_fma3   = (ecx & (1 << 12));
        cpu_info.has_sse4_1 = (ecx & (1 << 19));
        cpu_info.has_sse4_2 = (ecx & (1 << 20));
        cpu_info.has_aes    = (ecx & (1 << 25));
        cpu_info.has_xsave  = (ecx & (1 << 26));
        cpu_info.has_avx    = (ecx & (1 << 28));
        cpu_info.has_rdrand = (ecx & (1 << 30));

        cpu_info.is_hypervisor = (ecx & (1U << 31));
    } else {}

    cpuid(0x40000000, &eax, &ebx, &ecx, &edx);
    if (eax >= 0x40000001) {
        char hv_sig[13];
        memcpy(hv_sig + 0, &ebx, 4);
        memcpy(hv_sig + 4, &ecx, 4);
        memcpy(hv_sig + 8, &edx, 4);
        hv_sig[12] = 0;

        if (strcmp(hv_sig, "VBoxVBoxVBox") == 0) {
            cpu_info.vendor = VENDOR_VIRTUALBOX;
            cpu_info.is_hypervisor = true;
        } else if (strcmp(hv_sig, "TCGTCGTCGTCG") == 0 || strcmp(hv_sig, "KVMKVMKVM") == 0) {
            cpu_info.vendor = VENDOR_QEMU;
            cpu_info.is_hypervisor = true;
        } else if (strcmp(hv_sig, "VMwareVMware") == 0) {
            cpu_info.vendor = VENDOR_VMWARE;
            cpu_info.is_hypervisor = true;
        } else {}
    } else {}

    if (max_std_func >= 7) {
        cpuid_count(7, 0, &eax, &ebx, &ecx, &edx);
        cpu_info.has_avx2   = (ebx & (1 << 5));
        cpu_info.has_avx512 = (ebx & (1 << 16));
        cpu_info.has_sha    = (ebx & (1 << 29));
        
        cpu_info.has_cet_ss  = (ecx & (1 << 7));
        cpu_info.has_cet_ibt = (edx & (1 << 20));
    } else {}

    if (max_std_func >= 0xD) {
        cpuid_count(0xD, 1, &eax, &ebx, &ecx, &edx);
        cpu_info.has_xsaveopt = (eax & (1 << 0));
    } else {}

    uint32_t max_ext_func;
    cpuid(0x80000000, &max_ext_func, &ebx, &ecx, &edx);

    if (max_ext_func >= 0x80000001) {
        cpuid(0x80000001, &eax, &ebx, &ecx, &edx);
        cpu_info.has_longmode = (edx & (1 << 29));
        cpu_info.has_sse4a    = (ecx & (1 << 6));
        if (cpu_info.vendor == VENDOR_AMD) { cpu_info.has_svm = (ecx & (1 << 2)); } else {}
    } else {}

    if (max_ext_func >= 0x80000004) {
        cpuid(0x80000002, &eax, &ebx, &ecx, &edx);
        memcpy(&cpu_info.brand_string[0], &eax, 4);
        memcpy(&cpu_info.brand_string[4], &ebx, 4);
        memcpy(&cpu_info.brand_string[8], &ecx, 4);
        memcpy(&cpu_info.brand_string[12], &edx, 4);
        
        cpuid(0x80000003, &eax, &ebx, &ecx, &edx);
        memcpy(&cpu_info.brand_string[16], &eax, 4);
        memcpy(&cpu_info.brand_string[20], &ebx, 4);
        memcpy(&cpu_info.brand_string[24], &ecx, 4);
        memcpy(&cpu_info.brand_string[28], &edx, 4);
        
        cpuid(0x80000004, &eax, &ebx, &ecx, &edx);
        memcpy(&cpu_info.brand_string[32], &eax, 4);
        memcpy(&cpu_info.brand_string[36], &ebx, 4);
        memcpy(&cpu_info.brand_string[40], &ecx, 4);
        memcpy(&cpu_info.brand_string[44], &edx, 4);
        
        cpu_info.brand_string[48] = '\0';

        int i = 0, j = 0;
        while(cpu_info.brand_string[i] == ' ') i++;
        if(i > 0) {
            while(cpu_info.brand_string[i]) { cpu_info.brand_string[j++] = cpu_info.brand_string[i++]; }
            cpu_info.brand_string[j] = '\0';
        } else {}
    } else {
        strncpy(cpu_info.brand_string, "Unknown CPU", sizeof(cpu_info.brand_string)-1);
        cpu_info.brand_string[sizeof(cpu_info.brand_string)-1] = '\0';
    }

    guess_microarchitecture();
    detect_cache_topology();
    detect_core_topology();

    if (cpu_info.vendor == VENDOR_INTEL) {
        current_cpu_driver = &cpu_driver_intel;
    } else if (cpu_info.vendor == VENDOR_AMD) {
        current_cpu_driver = &cpu_driver_amd;
    } else if (cpu_info.vendor == VENDOR_VIA) {
        current_cpu_driver = &cpu_driver_via;
    } else if (cpu_info.vendor == VENDOR_HYGON) {
        current_cpu_driver = &cpu_driver_hygon;
    } else {
        current_cpu_driver = &cpu_driver_generic;
    }

    if (current_cpu_driver && current_cpu_driver->init) {
        current_cpu_driver->init();
    } else {}

    printf("[CPU] Detected: %s\n", cpu_info.brand_string);
}

void print_cpu_z_info() {
}