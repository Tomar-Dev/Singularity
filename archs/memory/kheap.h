// archs/memory/kheap.h
#ifndef KHEAP_H
#define KHEAP_H

#include <stdint.h>
#include <stddef.h>
#include "archs/cpu/x86_64/sync/spinlock.h"
#include "kernel/debug.h"
#define KHEAP_START_VIRTUAL 0xFFFF900000000000
#define KHEAP_MAX_SIZE      0x20000000

#define NUM_BUCKETS      8
#define MIN_BLOCK_SIZE   32
#define MAX_SLAB_SIZE    4096

#define VM_ARENA_MAX_REGIONS 512

#ifdef __cplusplus

struct VmRange {
    uint64_t base;
    uint32_t pages;
    uint32_t _pad;
};

class VmArena {
    VmRange  regions_[VM_ARENA_MAX_REGIONS];
    int      count_;
    uint64_t watermark_;
    uint64_t limit_;

    void insert_at(int idx, uint64_t base, uint32_t pages);
    void remove_at(int idx);
    int  lower_bound(uint64_t addr) const;
    void coalesce_around(int idx);

public:
    void     init(uint64_t start, uint64_t limit);
    uint64_t alloc(size_t pages);
    uint64_t alloc_aligned(size_t pages, size_t align_pages);
    void     free(uint64_t base, size_t pages);

    uint64_t watermark()         const { return watermark_; }
    int      free_region_count() const { return count_; }
};

struct HeapDebugHeader {
    uint64_t caller_addr;
    uint64_t size;
    uint64_t magic;
};

struct Slab {
    Slab*    next;
    Slab*    prev;
    void*    free_list;
    uint16_t used_count;
    uint16_t total_count;
    void*    data_page;
    uint16_t page_count;
    uint16_t _pad[3];
};

class KernelHeap;

class SlabCache {
public:
    const char* name;
    size_t      obj_size;
    Slab*       partial;
    Slab*       full;
    Slab*       empty;
    spinlock_t  lock;
    size_t      total_pages_allocated;

    void  init(const char* n, size_t size);
    void* alloc(uint64_t caller);
    bool  free(void* ptr);
    void  trim();

    size_t getCachedSize();
    size_t getUsedPayload();
    size_t getTotalReserved();

private:
    Slab* createSlab();
    void  freeSlab(Slab* slab);
    void  removeSlabFromList(Slab* slab, Slab** list_head);
    void  addSlabToList(Slab* slab, Slab** list_head);
};

class KernelHeap {
    friend class SlabCache;

private:
    uint64_t   magic;
    uint64_t   canary;
    spinlock_t vm_lock;
    SlabCache  buckets[NUM_BUCKETS];
    size_t     big_alloc_bytes;
    VmArena    vm_arena;

    int      getBucketIndex(size_t size);
    uint64_t getAslrOffset();

    void* vmm_alloc(size_t pages);
    void* vmm_alloc_aligned(size_t pages, size_t align_pages);

public:
    KernelHeap();
    void init();

    void* malloc(size_t size, uint64_t caller);
    void  free(void* ptr);
    void* calloc(size_t num, size_t size, uint64_t caller);
    void* realloc(void* ptr, size_t new_size, uint64_t caller);

    void* mallocAligned(size_t size, size_t alignment, uint64_t caller);
    void  freeAligned(void* ptr);

    void* mallocDma(size_t size, size_t alignment, uint64_t caller);
    void  freeDma(void* ptr, size_t size);

    void* mallocContiguous(size_t size, uint64_t caller);
    void  freeContiguous(void* ptr, size_t size);

    void   trim();
    size_t getCachedSize();
    size_t getUsedPayload();
    size_t getTotalReserved();
    void   printStats();
};

extern KernelHeap* g_Heap;

#endif

#ifdef __cplusplus
extern "C" {
#endif

extern uint64_t kheap_magic;
extern uint64_t kheap_canary;

void init_kheap();

void* kmalloc(size_t size);
void* kcalloc(size_t num, size_t size);
void  kfree(void* ptr);
void* krealloc(void* ptr, size_t new_size);

void* kmalloc_aligned(size_t size, size_t alignment);
void  kfree_aligned(void* ptr);

void* kmalloc_dma(size_t size, size_t alignment);
void  kfree_dma(void* ptr, size_t size);

void* kmalloc_contiguous(size_t size);
void  kfree_contiguous(void* ptr, size_t size);

void   kheap_print_stats();
size_t kheap_get_cached_size();
size_t kheap_get_payload_size();
size_t kheap_get_reserved_size();
void   kheap_trim();

#ifdef __cplusplus
}
#endif

#endif
