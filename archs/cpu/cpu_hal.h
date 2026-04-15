// archs/cpu/cpu_hal.h
#ifndef ARCHS_CPU_HAL_H
#define ARCHS_CPU_HAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// CPU Tanımlama ve Topoloji
uint32_t hal_cpu_get_id(void);
uint32_t hal_cpu_get_count(void);
int      hal_cpu_get_numa_node(uint32_t cpu_id);

// Zamanlama ve Senkronizasyon
uint64_t hal_timer_get_ticks(void);
void     hal_timer_delay_ms(uint32_t ms);
void     hal_timer_delay_ns(uint64_t ns);
void     hal_cpu_relax(void);
void     hal_cpu_halt(void);

// Kritik Çekirdek İşlemleri
void     hal_interrupts_disable(void);
void     hal_interrupts_enable(void);
bool     hal_interrupts_are_enabled(void);
void     hal_tlb_flush_single(uint64_t virt_addr);
void     hal_tlb_flush_all(void);

// Evrensel G/Ç
void     hal_io_outb(uint32_t port, uint8_t val);
void     hal_io_outw(uint32_t port, uint16_t val);
void     hal_io_outl(uint32_t port, uint32_t val);
uint8_t  hal_io_inb(uint32_t port);
uint16_t hal_io_inw(uint32_t port);
uint32_t hal_io_inl(uint32_t port);

void     hal_io_insw(uint32_t port, void* buffer, uint32_t count);
void     hal_io_outsw(uint32_t port, const void* buffer, uint32_t count);

// OPTİMİZASYON YAMASI: Donanım Hızlandırmalı CRC32 (SSE4.2)
static inline uint32_t hal_crc32_u64(uint32_t crc, uint64_t data) {
    __asm__ volatile("crc32q %1, %0" : "+r"(crc) : "rm"(data));
    return crc;
}

#ifdef __cplusplus
}
#endif

#endif