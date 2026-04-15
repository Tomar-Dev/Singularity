// archs/cpu/x86_64/memory/mtrr.h
#ifndef MTRR_H
#define MTRR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void mtrr_set_wc(uint64_t base, uint64_t length);

#ifdef __cplusplus
}
#endif

#endif
