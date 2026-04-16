// archs/cpu/x86_64/idt/idt.c
#include "archs/cpu/x86_64/idt/idt.h"
#include "libc/string.h"
#include "archs/cpu/x86_64/interrupts/isr.h"
#include "libc/stdio.h"
idt_entry_t idt_entries[256];
idt_ptr_t   idt_ptr;

void idt_set_entry_ist(uint8_t num, uint64_t base, uint16_t selector, uint8_t flags, uint8_t ist) {
    idt_entries[num].base_low  = (base & 0xFFFF);
    idt_entries[num].base_mid  = (base >> 16) & 0xFFFF;
    idt_entries[num].base_high = (base >> 32) & 0xFFFFFFFF;
    
    idt_entries[num].selector  = selector;
    idt_entries[num].flags     = flags;
    idt_entries[num].ist       = ist; 
    idt_entries[num].reserved  = 0;
}

void idt_set_entry(uint8_t num, uint64_t base, uint16_t selector, uint8_t flags) {
    idt_set_entry_ist(num, base, selector, flags, 0);
}

void idt_load() {
    __asm__ volatile("lidt %0" : : "m"(idt_ptr));
}

void idt_install() {
    idt_ptr.limit = sizeof(idt_entry_t) * 256 - 1;
    idt_ptr.base  = (uint64_t)&idt_entries;

    memset(&idt_entries, 0, sizeof(idt_entry_t) * 256);

    idt_set_entry(0, (uint64_t)isr0, 0x08, 0x8E);
    idt_set_entry(1, (uint64_t)isr1, 0x08, 0x8E);
    idt_set_entry(2, (uint64_t)isr2, 0x08, 0x8E);
    
    idt_set_entry(3, (uint64_t)isr3, 0x08, 0xEF);
    idt_set_entry(4, (uint64_t)isr4, 0x08, 0xEF);
    
    idt_set_entry(5, (uint64_t)isr5, 0x08, 0x8E);
    idt_set_entry(6, (uint64_t)isr6, 0x08, 0x8E);
    idt_set_entry(7, (uint64_t)isr7, 0x08, 0x8E);
    
    idt_set_entry_ist(8, (uint64_t)isr8, 0x08, 0x8E, 1);

    idt_set_entry(9, (uint64_t)isr9, 0x08, 0x8E);
    idt_set_entry(10, (uint64_t)isr10, 0x08, 0x8E);
    idt_set_entry(11, (uint64_t)isr11, 0x08, 0x8E);
    idt_set_entry(12, (uint64_t)isr12, 0x08, 0x8E);
    idt_set_entry(13, (uint64_t)isr13, 0x08, 0x8E);
    idt_set_entry(14, (uint64_t)isr14, 0x08, 0x8E);
    idt_set_entry(15, (uint64_t)isr15, 0x08, 0x8E);
    idt_set_entry(16, (uint64_t)isr16, 0x08, 0x8E);
    idt_set_entry(17, (uint64_t)isr17, 0x08, 0x8E);
    idt_set_entry(18, (uint64_t)isr18, 0x08, 0x8E);
    idt_set_entry(19, (uint64_t)isr19, 0x08, 0x8E);
    idt_set_entry(20, (uint64_t)isr20, 0x08, 0x8E);
    idt_set_entry(21, (uint64_t)isr21, 0x08, 0x8E);
    idt_set_entry(22, (uint64_t)isr22, 0x08, 0x8E);
    idt_set_entry(23, (uint64_t)isr23, 0x08, 0x8E);
    idt_set_entry(24, (uint64_t)isr24, 0x08, 0x8E);
    idt_set_entry(25, (uint64_t)isr25, 0x08, 0x8E);
    idt_set_entry(26, (uint64_t)isr26, 0x08, 0x8E);
    idt_set_entry(27, (uint64_t)isr27, 0x08, 0x8E);
    idt_set_entry(28, (uint64_t)isr28, 0x08, 0x8E);
    idt_set_entry(29, (uint64_t)isr29, 0x08, 0x8E);
    idt_set_entry(30, (uint64_t)isr30, 0x08, 0x8E);
    idt_set_entry(31, (uint64_t)isr31, 0x08, 0x8E);

    irq_install();

    idt_set_entry(32, (uint64_t)irq0, 0x08, 0x8E);
    idt_set_entry(33, (uint64_t)irq1, 0x08, 0x8E);
    idt_set_entry(48, (uint64_t)irq16, 0x08, 0x8E);

    idt_set_entry(253, (uint64_t)irq253, 0x08, 0x8E);
    idt_set_entry(254, (uint64_t)irq254, 0x08, 0x8E);
    idt_set_entry(255, (uint64_t)irq255, 0x08, 0x8E);

    idt_load();
}