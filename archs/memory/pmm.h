// archs/memory/pmm.h
#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096

#ifdef __cplusplus
extern "C" {
#endif

void pmm_init(void* multiboot_addr, void* kernel_end);

void*   pmm_alloc_frame(void);
void    pmm_free_frame(void* addr);

void    pmm_inc_ref(void* addr);
void    pmm_dec_ref(void* addr);
uint8_t pmm_get_ref(void* addr);

void* pmm_alloc_contiguous(size_t count);
void* pmm_alloc_contiguous_aligned(size_t count, size_t align_pages);
void  pmm_free_contiguous(void* addr, size_t count);

void     pmm_print_stats(void);
uint64_t pmm_get_total_memory(void);
uint64_t pmm_get_used_memory(void);
uint64_t pmm_get_free_memory(void);

int  pmm_is_low_memory(void);
void pmm_flush_magazines(void);
void pmm_map_remaining_memory(void);
void pmm_register_region(uint32_t node_id, uint64_t base, uint64_t length);

#ifdef __cplusplus
}
#endif

#endif