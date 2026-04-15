// archs/cpu/x86_64/gdt/gdt.c
#include "archs/cpu/x86_64/gdt/gdt.h"
#include "archs/cpu/x86_64/context/tss.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "archs/cpu/x86_64/smp/smp.h"
#define GDT_ENTRIES 7

static gdt_entry_t gdt_entries[MAX_CPUS][GDT_ENTRIES];
static gdt_ptr_t   gdt_ptrs[MAX_CPUS];

static void gdt_set_gate(uint8_t cpu_id, int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    if (cpu_id >= MAX_CPUS) return;

    gdt_entries[cpu_id][num].base_low    = (base & 0xFFFF);
    gdt_entries[cpu_id][num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[cpu_id][num].base_high   = (base >> 24) & 0xFF;

    gdt_entries[cpu_id][num].limit_low   = (limit & 0xFFFF);
    gdt_entries[cpu_id][num].granularity = (limit >> 16) & 0x0F;

    gdt_entries[cpu_id][num].granularity |= gran & 0xF0;
    gdt_entries[cpu_id][num].access      = access;
}

void gdt_set_tss_gate(uint8_t cpu_id, int num, uint64_t base, uint32_t limit) {
    if (cpu_id >= MAX_CPUS) return;

    gdt_system_entry_t* tss_desc = (gdt_system_entry_t*)&gdt_entries[cpu_id][num];

    tss_desc->limit_low   = limit & 0xFFFF;
    tss_desc->base_low    = base & 0xFFFF;
    tss_desc->base_middle = (base >> 16) & 0xFF;
    tss_desc->access      = 0x89;
    tss_desc->granularity = 0x00; 
    tss_desc->base_high   = (base >> 24) & 0xFF;
    tss_desc->base_upper  = (base >> 32) & 0xFFFFFFFF;
    tss_desc->reserved    = 0;
}

static void gdt_setup_entries(uint8_t cpu_id) {
    gdt_ptrs[cpu_id].limit = (sizeof(gdt_entry_t) * GDT_ENTRIES) - 1;
    gdt_ptrs[cpu_id].base  = (uint64_t)&gdt_entries[cpu_id];

    gdt_set_gate(cpu_id, 0, 0, 0, 0, 0);
    gdt_set_gate(cpu_id, 1, 0, 0, 0x9A, 0x20);
    gdt_set_gate(cpu_id, 2, 0, 0, 0x92, 0x00);
    gdt_set_gate(cpu_id, 3, 0, 0, 0xF2, 0x00);
    gdt_set_gate(cpu_id, 4, 0, 0, 0xFA, 0x20);
    
    memset(&gdt_entries[cpu_id][5], 0, sizeof(gdt_system_entry_t));
}

void gdt_install() {
    gdt_setup_entries(0);
    gdt_flush((uint64_t)&gdt_ptrs[0]);
}

void gdt_init_cpu(uint8_t cpu_id) {
    if (cpu_id >= MAX_CPUS) return;
    gdt_setup_entries(cpu_id);
    gdt_flush((uint64_t)&gdt_ptrs[cpu_id]);
}
