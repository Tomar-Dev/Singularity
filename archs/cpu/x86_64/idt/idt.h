// archs/cpu/x86_64/idt/idt.h
#ifndef IDT_H
#define IDT_H

#include <stdint.h>

struct idt_entry_struct {
    uint16_t base_low;       
    uint16_t selector;       
    uint8_t  ist;            
    uint8_t  flags;          
    uint16_t base_mid;       
    uint32_t base_high;      
    uint32_t reserved;       
} __attribute__((packed));

typedef struct idt_entry_struct idt_entry_t;

struct idt_ptr_struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

typedef struct idt_ptr_struct idt_ptr_t;

void idt_install();
void idt_set_entry(uint8_t num, uint64_t base, uint16_t selector, uint8_t flags);
void idt_set_entry_ist(uint8_t num, uint64_t base, uint16_t selector, uint8_t flags, uint8_t ist);
void idt_load();

#endif
