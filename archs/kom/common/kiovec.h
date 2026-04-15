// archs/kom/common/kiovec.h
#ifndef KIOVEC_H
#define KIOVEC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t phys_addr;
    void* virt_addr;
    size_t size;
} kiovec_t;

#ifdef __cplusplus
}
#endif

#endif
