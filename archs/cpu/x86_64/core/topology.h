// archs/cpu/x86_64/core/topology.h
#ifndef TOPOLOGY_H
#define TOPOLOGY_H

#include <stdint.h>
#include <stdbool.h> 

typedef struct {
    uint32_t apic_id;
    uint32_t package_id;
    uint32_t core_id;
    uint32_t thread_id;
    bool is_p_core;
} cpu_topology_t;

extern cpu_topology_t cpu_topologies[32];

void detect_cpu_topology();
void print_topology_map();

#ifdef __cplusplus
extern "C" {
#endif
    void* get_topology_array_ptr();
#ifdef __cplusplus
}
#endif

#endif
