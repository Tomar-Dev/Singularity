// archs/storage/storage_hal.h
#ifndef ARCHS_STORAGE_HAL_H
#define ARCHS_STORAGE_HAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t phys_addr;
    void*    virt_addr;
    size_t   size;
} storage_kiovec_t;

typedef struct {
    uint64_t prp1;            
    uint64_t prp2;            
    void*    prp_list_virt;   
    uint32_t total_bytes;
    bool     is_valid;
} storage_dma_chain_t;

bool rust_dma_guard_validate(const storage_kiovec_t* vectors, size_t count, uint64_t caller_pid);

storage_dma_chain_t rust_storage_build_sg_list(const storage_kiovec_t* vectors, size_t count);

// FIX: Pinned Memory (Ref Count) silinmesi için vektör listesi eklendi.
void rust_storage_free_sg_list(storage_dma_chain_t* chain, const storage_kiovec_t* vectors, size_t count);

#ifdef __cplusplus
}
#endif

#endif