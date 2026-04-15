// kernel/debug.h
#ifndef DEBUG_H
#define DEBUG_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include "archs/cpu/x86_64/interrupts/isr.h"
#include "archs/cpu/cpu_hal.h"
#include "kernel/fastops.h" 

#ifdef __cplusplus
extern "C" {
#endif

extern void vga_set_color(uint8_t fg, uint8_t bg);

#define KERNEL_SPACE_BASE 0xFFFF800000000000ULL

#define KHEAP_DEBUG_MODE 1
#define DEBUG_ENABLE_ASSERTS 1
#define DEBUG_SERIAL_PORT 0x3F8

typedef enum {
    KERR_SUCCESS            = 0x00,
    KERR_UNKNOWN            = 0x01,
    
    KERR_CPU_EX             = 0x10, 
    KERR_GP_FAULT           = 0x11, 
    KERR_PAGE_FAULT         = 0x12, 
    KERR_DOUBLE_FAULT       = 0x13, 
    KERR_DIV_ZERO           = 0x14,
    KERR_UNHANDLED_IRQ      = 0x15,
    
    KERR_MEM_OOM            = 0x20, 
    KERR_MEM_CORRUPT        = 0x21, 
    KERR_HEAP_OVERFLOW      = 0x22, 
    KERR_HEAP_DOUBLE_FREE   = 0x23, 
    KERR_HEAP_USE_AFTER_FREE= 0x24, 
    KERR_SLAB_CORRUPTION    = 0x25,
    KERR_NULL_DEREFERENCE   = 0x26,
    KERR_OUT_OF_BOUNDS      = 0x27,
    
    KERR_STACK_SMASH        = 0x30, 
    KERR_ASSERT_FAIL        = 0x31, 
    KERR_UBSAN              = 0x32,
    KERR_DEADLOCK_DETECTED  = 0x33,
    KERR_LOCK_ORDER         = 0x34,
    
    KERR_DRIVER_FAIL        = 0x40,
    KERR_FS_IO              = 0x41,
    KERR_ACPI_ERROR         = 0x42,
    KERR_DMA_CORRUPTION     = 0x43,
    KERR_FS_CORRUPTION      = 0x44,

    KERR_RUST_PANIC         = 0x50,
    KERR_RUST_OOM           = 0x51,
    KERR_RUST_SAFE_MEM      = 0x52,

    KERR_TEST               = 0x99  
} kernel_error_t;

typedef enum {
    LOG_INFO, LOG_WARN, LOG_ERROR, LOG_DEBUG, LOG_SUCCESS, LOG_CRITICAL
} log_level_t;

static inline void dbg_putc(char c) {
    while ((hal_io_inb(DEBUG_SERIAL_PORT + 5) & 0x20) == 0);
    hal_io_outb(DEBUG_SERIAL_PORT, c);
}

static inline void dbg_puts(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) dbg_putc(str[i]);
}

void debug_force_unlock();
void dbg_direct(const char* msg);

__attribute__((noreturn)) void panic_at(const char* file, int line, kernel_error_t code, const char* msg);

void kernel_warning(const char* file, int line, const char* msg);

void set_panic_registers(registers_t* regs); 
void klog(log_level_t level, const char* fmt, ...);
void klog_hex(const void* ptr, size_t size); 

void print_stack_trace();
void print_stack_trace_from(uint64_t rbp, uint64_t rip);
void klog_buffer_dump(); 
void klog_buffer_dump_serial();
void export_crashdump_to_nvram();

#define PANIC(msg) panic_at(__FILE__, __LINE__, KERR_UNKNOWN, msg)
#define WARN(msg) kernel_warning(__FILE__, __LINE__, msg)

#if DEBUG_ENABLE_ASSERTS
    #define ASSERT(cond) \
        if (__builtin_expect(!(cond), 0)) { \
            panic_at(__FILE__, __LINE__, KERR_ASSERT_FAIL, "Assertion Failed: " #cond); \
        }
#else
    #define ASSERT(cond) ((void)0)
#endif

static inline uint64_t rdtsc_profile() {
    return rdtsc_ordered();
}

#define CPU_FREQ_ROUGH_MHZ 2000 

#define BOOT_TIME_START() uint64_t _bt_start = rdtsc_profile(); (void)_bt_start;

#define BOOT_TIME_END(name) do { \
    uint64_t _bt_diff = rdtsc_profile() - _bt_start; \
    vga_set_color(15, 0); \
    printf("[%s] %s: %d ms (Cycles: %lu)\n", "TIME", name, \
           (int)(_bt_diff / (CPU_FREQ_ROUGH_MHZ * 1000)), _bt_diff); \
} while(0)

#ifdef __cplusplus
}
#endif

#endif