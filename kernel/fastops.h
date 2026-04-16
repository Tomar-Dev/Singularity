// kernel/fastops.h
#ifndef FASTOPS_H
#define FASTOPS_H

#include <stdint.h>
#include <stddef.h>
#include "archs/cpu/cpu_hal.h" 

#ifdef __cplusplus
extern "C" {
#endif

#ifndef likely
#define likely(x)      __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
#define unlikely(x)    __builtin_expect(!!(x), 0)
#endif

static inline uint32_t get_cpu_id_fast() {
    uint32_t cpu_id;
    __asm__ volatile("movl %%gs:0x1C, %0" : "=r"(cpu_id));
    return cpu_id;
}

static inline void* get_current_thread_fast() {
    void* thread;
    __asm__ volatile("movq %%gs:0x20, %0" : "=r"(thread));
    return thread;
}

static inline void barrier() {
    __asm__ volatile("" ::: "memory");
}

static inline void cpu_relax() {
    hal_cpu_relax();
}

static inline void invlpg_range_asm(uint64_t start, size_t count) {
    for (size_t i = 0; i < count; i++) {
        __asm__ volatile("invlpg (%0)" : : "r"(start + (i * 4096)) : "memory");
    }
}

static inline uint64_t rdtsc_ordered() {
    uint32_t lo, hi;
    __asm__ volatile("lfence; rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static inline void prefetch_stack(void* ptr) {
    __asm__ volatile("prefetcht0 (%0)" : : "r"(ptr));
    __asm__ volatile("prefetcht0 -64(%0)" : : "r"(ptr));
}

// GÜVENLİK YAMASI: SMAP/SMEP User-Copy Koruması
static inline void stac() { __asm__ volatile("stac" ::: "memory"); }
static inline void clac() { __asm__ volatile("clac" ::: "memory"); }

#ifdef __cplusplus
}
#endif

#endif