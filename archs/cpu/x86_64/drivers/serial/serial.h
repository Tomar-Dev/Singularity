// archs/cpu/x86_64/drivers/serial/serial.h
#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>
#include <stdbool.h>
#include "archs/cpu/x86_64/interrupts/isr.h" 

#ifdef __cplusplus
extern "C" {
#endif

#define COM1 0x3F8

void init_serial_early();
void init_serial_late();

void serial_write(const char* str);
void serial_putc(char c);
void serial_printf(const char* format, ...);
void serial_irq_handler(registers_t* regs);

void serial_enable_direct_mode();
void serial_write_atomic(const char* str);

#ifdef __cplusplus
}
#endif

#endif