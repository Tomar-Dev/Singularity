// kernel/ksyms.h
#ifndef KSYMS_H
#define KSYMS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct kernel_symbol {
    uint64_t addr;
    const char* name;
};

const char* ksyms_resolve_symbol(uint64_t addr, uint64_t* offset);

#ifdef __cplusplus
}
#endif

#endif
