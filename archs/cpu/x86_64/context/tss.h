// archs/cpu/x86_64/context/tss.h
#ifndef TSS_H
#define TSS_H

#include <stdint.h>

struct tss_entry_struct {
    uint32_t reserved1;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved2;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved3;
    uint16_t reserved4;
    uint16_t iomap_base;
} __attribute__((packed));

typedef struct tss_entry_struct tss_entry_t;

void tss_install(uint8_t cpu_id, uint64_t kernel_stack);

void set_kernel_stack(uint8_t cpu_id, uint64_t stack);

extern void tss_flush();

#endif
