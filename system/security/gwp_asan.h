// system/security/gwp_asan.h
#ifndef GWP_ASAN_H
#define GWP_ASAN_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GWP_ASAN_SLOTS       64
#define GWP_ASAN_SAMPLE_RATE 16

typedef enum {
    GWP_ALIGN_RIGHT = 0,
    GWP_ALIGN_LEFT  = 1,
} GwpAlignment;

typedef enum {
    GWP_SLOT_FREE    = 0,
    GWP_SLOT_ACTIVE  = 1,
    GWP_SLOT_FREED   = 2,
} GwpSlotState;

typedef struct {
    void*        user_ptr;
    void*        page_base;
    size_t       user_size;
    uint64_t     alloc_caller;
    uint64_t     free_caller;
    GwpSlotState state;
    GwpAlignment alignment;
} GwpSlot;

void gwp_asan_init(void);

bool gwp_asan_should_sample(void);

void* gwp_asan_malloc(size_t size, uint64_t caller);

bool gwp_asan_free(void* ptr, uint64_t caller);

bool gwp_asan_is_managed(void* ptr);

// GÜVENLİK YAMASI: Page Fault işleyicisi için donanım zırhı dışarı açıldı.
bool gwp_asan_check_fault(uint64_t fault_addr);

void gwp_asan_print_stats(void);

#ifdef __cplusplus
}
#endif

#endif