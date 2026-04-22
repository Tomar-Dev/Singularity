// archs/cpu/x86_64/interrupts/isr.h
#ifndef ISR_H
#define ISR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no;
    uint64_t err_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed)) registers_t;

void isr_handler(registers_t* regs);
void irq_handler(registers_t* regs);

typedef void (*isr_t)(registers_t*);
void register_interrupt_handler(uint8_t n, isr_t handler);
void irq_install();
void pic_disable(); 

void register_fpu_exception_handlers(void);

extern void isr0();  extern void isr1();  extern void isr2();  extern void isr3();
extern void isr4();  extern void isr5();  extern void isr6();  extern void isr7();
extern void isr8();  extern void isr9();  extern void isr10(); extern void isr11();
extern void isr12(); extern void isr13(); extern void isr14(); extern void isr15();
extern void isr16(); extern void isr17(); extern void isr18(); extern void isr19();
extern void isr20(); extern void isr21(); extern void isr22(); extern void isr23();
extern void isr24(); extern void isr25(); extern void isr26(); extern void isr27();
extern void isr28(); extern void isr29(); extern void isr30(); extern void isr31();

extern void irq0();  extern void irq1();  extern void irq2();  extern void irq3();
extern void irq4();  extern void irq5();  extern void irq6();  extern void irq7();
extern void irq8();  extern void irq9();  extern void irq10(); extern void irq11();
extern void irq12(); extern void irq13(); extern void irq14(); extern void irq15();
extern void irq16();

extern void irq253();
extern void irq254();
extern void irq255();

#ifdef __cplusplus
}
#endif

#include "archs/cpu/cpu_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline void disable_interrupts() { hal_interrupts_disable(); }
static inline void enable_interrupts() { hal_interrupts_enable(); }
static inline void halt_cpu() { hal_cpu_halt(); }

#ifdef __cplusplus
}
#endif

#endif