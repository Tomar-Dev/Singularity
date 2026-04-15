// kernel/profiler.h
#ifndef PROFILER_H
#define PROFILER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void profiler_init();
void profiler_enable(bool enable);
void profiler_tick(uint64_t rip);
void profiler_print_report();

#ifdef __cplusplus
}
#endif

#endif
