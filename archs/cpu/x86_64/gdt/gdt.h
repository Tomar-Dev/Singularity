// archs/cpu/x86_64/gdt/gdt.h
#ifndef GDT_H
#define GDT_H

#include <stdint.h>
#include "archs/cpu/x86_64/smp/smp.h"
struct gdt_entry_struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

typedef struct gdt_entry_struct gdt_entry_t;

struct gdt_system_entry_struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
    uint32_t base_upper; 
    uint32_t reserved;   
} __attribute__((packed));

typedef struct gdt_system_entry_struct gdt_system_entry_t;

struct gdt_ptr_struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

typedef struct gdt_ptr_struct gdt_ptr_t;

void gdt_install();

void gdt_init_cpu(uint8_t cpu_id);

void gdt_set_tss_gate(uint8_t cpu_id, int num, uint64_t base, uint32_t limit);

extern void gdt_flush(uint64_t);

#endif
