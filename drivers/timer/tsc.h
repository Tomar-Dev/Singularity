// drivers/timer/tsc.h
#ifndef TSC_H
#define TSC_H

#include <stdint.h>
#include "system/device/device.h"
#ifdef __cplusplus
class TSCDriver : public Device {
public:
    TSCDriver();
    int init() override;
    
    int read(char* buffer, int size) override { (void)buffer; (void)size; return 0; }
    int write(const char* buffer, int size) override { (void)buffer; (void)size; return 0; }
};
#endif

#ifdef __cplusplus
extern "C" {
#endif

static inline uint64_t tsc_read_asm() {
    uint32_t lo, hi;
    __asm__ volatile("lfence; rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

void init_tsc();
void timer_sleep(uint64_t ticks);
uint64_t timer_get_ticks();

void sleep_ns(uint64_t ns);
void sleep_ms(uint64_t ms);
void tsc_delay_ms(uint32_t ms);

uint64_t get_tsc_freq();

#ifdef __cplusplus
}
#endif

#endif
