// drivers/acpi/numa.h
#ifndef NUMA_H
#define NUMA_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_NUMA_NODES 8
#define MAX_MEM_REGIONS 32

struct acpi_srat_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
    uint32_t reserved1;
    uint64_t reserved2;
} __attribute__((packed));

struct srat_subtable_header {
    uint8_t type;
    uint8_t length;
} __attribute__((packed));

struct srat_processor_affinity {
    struct srat_subtable_header header;
    uint8_t proximity_domain_low;
    uint8_t apic_id;
    uint32_t flags;
    uint8_t local_sapic_eid;
    uint8_t proximity_domain_high[3];
    uint32_t clock_domain;
} __attribute__((packed));

struct srat_memory_affinity {
    struct srat_subtable_header header;
    uint32_t proximity_domain;
    uint16_t reserved1;
    uint32_t base_addr_low;
    uint32_t base_addr_high;
    uint32_t length_low;
    uint32_t length_high;
    uint32_t reserved2;
    uint32_t flags;
    uint64_t reserved3;
} __attribute__((packed));

typedef struct {
    uint32_t node_id;
    uint64_t mem_base;
    uint64_t mem_length;
    uint32_t cpu_count;
    uint8_t apic_ids[32];
} numa_node_t;

extern numa_node_t numa_nodes[MAX_NUMA_NODES];
extern int numa_node_count;

void init_numa();
void numa_print_topology();
int get_cpu_numa_node(uint8_t apic_id);

#ifdef __cplusplus
}
#endif

#endif
