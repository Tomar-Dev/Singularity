// archs/cpu/x86_64/core/topology.c
#include "archs/cpu/x86_64/core/topology.h"
#include "archs/cpu/x86_64/core/cpuid.h"
#include "libc/stdio.h"
#include "drivers/serial/serial.h"
#include "drivers/apic/apic.h" 
cpu_topology_t cpu_topologies[32];

void detect_cpu_topology() {
    uint8_t current_id = get_apic_id();
    
    cpu_topologies[current_id].apic_id = current_id;
    cpu_topologies[current_id].package_id = 0;
    cpu_topologies[current_id].core_id = current_id;
    cpu_topologies[current_id].thread_id = 0;
    cpu_topologies[current_id].is_p_core = true;

    uint32_t eax, ebx, ecx, edx;

    if (cpu_info.vendor == VENDOR_INTEL) {
        __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) 
                         : "a"(0x0B), "c"(0));
        
        int smt_shift = eax & 0x1F; 
        
        if (smt_shift > 0) {
            cpu_topologies[current_id].thread_id = current_id & ((1 << smt_shift) - 1);
            cpu_topologies[current_id].core_id = (current_id >> smt_shift);
        }
        
        if (cpu_info.family == 6 && cpu_info.model >= 0x97) {
             __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) 
                         : "a"(0x1A), "c"(0));
             
             uint8_t core_type = (eax >> 24);
             if (core_type == 0x20) {
                 cpu_topologies[current_id].is_p_core = false;
             } else {
                 cpu_topologies[current_id].is_p_core = true;
             }
        }

    } else if (cpu_info.vendor == VENDOR_AMD || cpu_info.vendor == VENDOR_HYGON) {
        // AMD Topology Extensions (Leaf 0x8000001E)
        uint32_t max_ext;
        __asm__ volatile("cpuid" : "=a"(max_ext) : "a"(0x80000000));
        
        if (max_ext >= 0x8000001E) {
            __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) 
                             : "a"(0x8000001E));
            
            cpu_topologies[current_id].core_id = ebx & 0xFF;
            
            if (cpu_info.has_ht) {
                cpu_topologies[current_id].thread_id = current_id & 1;
                cpu_topologies[current_id].core_id = current_id >> 1;
            }
        }
    }
}

void print_topology_map() {
    printf("\n--- CPU Topology Map (Current Core) ---\n");
    uint8_t id = get_apic_id();
    
    printf("APIC ID   : %d\n", cpu_topologies[id].apic_id);
    printf("Core ID   : %d\n", cpu_topologies[id].core_id);
    printf("Thread ID : %d\n", cpu_topologies[id].thread_id);
    printf("Type      : %s\n", cpu_topologies[id].is_p_core ? "Performance Core (P-Core)" : "Efficient Core (E-Core)");
    printf("---------------------------------------\n");
}

void* get_topology_array_ptr() {
    return (void*)&cpu_topologies;
}
