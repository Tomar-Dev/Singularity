// drivers/acpi/numa.c
#include "drivers/acpi/numa.h"
#include "drivers/acpi/acpi.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "drivers/serial/serial.h"
#include "archs/cpu/x86_64/memory/paging.h" 

extern void pmm_register_region(uint32_t node_id, uint64_t base, uint64_t length);

numa_node_t numa_nodes[MAX_NUMA_NODES];
int numa_node_count = 0;

static numa_node_t* get_or_create_node(uint32_t domain) {
    for (int i = 0; i < numa_node_count; i++) {
        if (numa_nodes[i].node_id == domain) {
            return &numa_nodes[i];
        }
    }
    
    if (numa_node_count >= MAX_NUMA_NODES) {
        serial_write("[NUMA] Error: Maximum node count reached!\n");
        return NULL;
    }
    
    numa_node_t* node = &numa_nodes[numa_node_count++];
    memset(node, 0, sizeof(numa_node_t));
    node->node_id = domain;
    return node;
}

void init_numa() {
    serial_write("[NUMA] Searching for SRAT table...\n");
    
    struct acpi_srat_header* srat = (struct acpi_srat_header*)acpi_find_table("SRAT");
    if (!srat) {
        serial_write("[NUMA] SRAT not found. System is UMA (Uniform Memory Access).\n");
        return;
    }
    
    serial_write("[NUMA] SRAT found. Parsing topology...\n");
    
    uint8_t* ptr = (uint8_t*)srat + sizeof(struct acpi_srat_header);
    uint8_t* end = (uint8_t*)srat + srat->length;
    
    while (ptr < end) {
        struct srat_subtable_header* header = (struct srat_subtable_header*)ptr;
        
        if (header->type == 0) {
            struct srat_processor_affinity* p = (struct srat_processor_affinity*)ptr;
            if (p->flags & 1) {
                uint32_t domain = p->proximity_domain_low | 
                                  ((uint32_t)p->proximity_domain_high[0] << 8) |
                                  ((uint32_t)p->proximity_domain_high[1] << 16) |
                                  ((uint32_t)p->proximity_domain_high[2] << 24);
                
                numa_node_t* node = get_or_create_node(domain);
                if (node && node->cpu_count < 32) {
                    node->apic_ids[node->cpu_count++] = p->apic_id;
                } else {
                    serial_write("[NUMA] Warning: CPU limit exceeded for node.\n");
                }
            }
        }
        else if (header->type == 1) {
            struct srat_memory_affinity* m = (struct srat_memory_affinity*)ptr;
            if (m->flags & 1) {
                numa_node_t* node = get_or_create_node(m->proximity_domain);
                if (node) {
                    uint64_t base = ((uint64_t)m->base_addr_high << 32) | m->base_addr_low;
                    uint64_t len = ((uint64_t)m->length_high << 32) | m->length_low;
                    
                    if (len > node->mem_length) {
                        node->mem_base = base;
                        node->mem_length = len;
                    }
                    
                    pmm_register_region(node->node_id, base, len);
                } else {
                    serial_write("[NUMA] Error: Node allocation failed for memory affinity.\n");
                }
            }
        } else {
            // Defansif Programlama: Bilinmeyen tipleri yoksay
            // serial_write("[NUMA] Ignored unknown SRAT entry type.\n");
        }
        
        ptr += header->length;
    }
    
    char buf[128];
    sprintf(buf, "[NUMA] Detected %d Nodes.\n", numa_node_count);
    serial_write(buf);
}

void numa_print_topology() {
    if (numa_node_count == 0) {
        printf("NUMA Topology: UMA (Single Node / No SRAT)\n");
        return;
    }
    
    printf("\n--- NUMA Topology (%d Nodes) ---\n", numa_node_count);
    for (int i = 0; i < numa_node_count; i++) {
        numa_node_t* n = &numa_nodes[i];
        printf("Node %d:\n", n->node_id);
        printf("  RAM : %d MB @ 0x%lx\n", (int)(n->mem_length / 1024 / 1024), n->mem_base);
        printf("  CPUs: ");
        for (uint32_t c = 0; c < n->cpu_count; c++) {
            printf("%d ", n->apic_ids[c]);
        }
        printf("\n");
    }
    printf("--------------------------------\n");
}

int get_cpu_numa_node(uint8_t apic_id) {
    for (int i = 0; i < numa_node_count; i++) {
        for (uint32_t c = 0; c < numa_nodes[i].cpu_count; c++) {
            if (numa_nodes[i].apic_ids[c] == apic_id) {
                return numa_nodes[i].node_id;
            }
        }
    }
    return 0;
}