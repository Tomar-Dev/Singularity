// archs/cpu/x86_64/hal_cpu_x86_64.c
#include "archs/cpu/cpu_hal.h"
#include "archs/cpu/x86_64/core/ports.h"
#include "archs/cpu/x86_64/smp/smp.h"
#include "archs/cpu/x86_64/core/topology.h"
#include "archs/cpu/x86_64/core/cpuid.h"
#include "kernel/debug.h"

extern void sleep_ms(uint64_t ms);
extern void sleep_ns(uint64_t ns);
extern uint64_t timer_get_ticks();
extern uint64_t get_uptime_ns();
extern void invlpg_range_asm_fast(uint64_t start, size_t count);
extern int get_cpu_numa_node(uint8_t apic_id);
extern uint8_t get_apic_id(); 

uint32_t hal_cpu_get_id(void) {
    return (uint32_t)get_apic_id();
}

uint32_t hal_cpu_get_count(void) {
    extern uint8_t num_cpus;
    return (uint32_t)num_cpus;
}

int hal_cpu_get_numa_node(uint32_t cpu_id) {
    if (cpu_id >= MAX_CPUS) {
        panic_at("HAL", 0, KERR_OUT_OF_BOUNDS, "Invalid CPU ID requested for NUMA node");
    } else {
        if (per_cpu_data[cpu_id]) {
            return get_cpu_numa_node(cpus[cpu_id].lapic_id);
        } else {
            return 0; 
        }
    }
}

uint64_t hal_timer_get_ticks(void) {
    return timer_get_ticks();
}

uint64_t hal_timer_get_uptime_ms(void) {
    return get_uptime_ns() / 1000000ULL;
}

uint64_t hal_timer_get_uptime_us(void) {
    return get_uptime_ns() / 1000ULL;
}

void hal_timer_delay_ms(uint32_t ms) {
    sleep_ms(ms);
}

void hal_timer_delay_ns(uint64_t ns) {
    sleep_ns(ns);
}

void hal_cpu_relax(void) {
    __asm__ volatile("pause" ::: "memory");
}

void hal_cpu_halt(void) {
    __asm__ volatile("hlt" ::: "memory");
}

void hal_interrupts_disable(void) {
    __asm__ volatile("cli" ::: "memory");
}

void hal_interrupts_enable(void) {
    __asm__ volatile("sti" ::: "memory");
}

bool hal_interrupts_are_enabled(void) {
    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0" : "=r"(rflags));
    return (rflags & 0x200) != 0;
}

void hal_tlb_flush_single(uint64_t virt_addr) {
    invlpg_range_asm_fast(virt_addr, 1);
}

void hal_tlb_flush_all(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

void hal_memory_barrier_acquire(void) {
    __asm__ volatile("lfence" ::: "memory");
}

void hal_memory_barrier_release(void) {
    __asm__ volatile("sfence" ::: "memory");
}

void hal_memory_barrier_full(void) {
    __asm__ volatile("mfence" ::: "memory");
}

void hal_cache_flush_line(const void* addr) {
    __asm__ volatile("clflush (%0)" :: "r"(addr) : "memory");
}

#define HAL_PORT_CHECK(p) \
    if (__builtin_expect((p) > 0xFFFF, 0)) { \
        panic_at(__FILE__, __LINE__, KERR_OUT_OF_BOUNDS, "HAL I/O: Port address exceeds 0xFFFF!"); \
    } else { /* Address is within safe 16-bit range */ }

void hal_io_outb(uint32_t port, uint8_t val)  { HAL_PORT_CHECK(port); outb((uint16_t)port, val); }
void hal_io_outw(uint32_t port, uint16_t val) { HAL_PORT_CHECK(port); outw((uint16_t)port, val); }
void hal_io_outl(uint32_t port, uint32_t val) { HAL_PORT_CHECK(port); outl((uint16_t)port, val); }

uint8_t  hal_io_inb(uint32_t port) { HAL_PORT_CHECK(port); return inb((uint16_t)port); }
uint16_t hal_io_inw(uint32_t port) { HAL_PORT_CHECK(port); return inw((uint16_t)port); }
uint32_t hal_io_inl(uint32_t port) { HAL_PORT_CHECK(port); return inl((uint16_t)port); }

void hal_io_insw(uint32_t port, void* buffer, uint32_t count)        { HAL_PORT_CHECK(port); insw((uint16_t)port, buffer, count); }
void hal_io_outsw(uint32_t port, const void* buffer, uint32_t count) { HAL_PORT_CHECK(port); outsw((uint16_t)port, buffer, count); }

uint32_t hal_crc32_u64(uint32_t crc, uint64_t data) {
    if (__builtin_expect(cpu_info.has_sse4_2, 1)) {
        __asm__ volatile("crc32q %1, %0" : "+r"(crc) : "rm"(data));
        return crc;
    } else {
        uint64_t temp_data = data;
        uint8_t* p = (uint8_t*)&temp_data;
        uint32_t c = crc; 
        for (int i = 0; i < 8; i++) {
            c ^= p[i];
            for (int b = 0; b < 8; b++) {
                if (c & 1) { c = (c >> 1) ^ 0x82F63B78UL; }
                else { c >>= 1; }
            }
        }
        return c;
    }
}