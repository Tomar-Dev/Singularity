// archs/memory/kheap_slab.cpp
#include "memory/kheap.h"
#include "memory/paging.h"
#include "memory/pmm.h"
#include "libc/string.h"
#include "drivers/serial/serial.h"
#include "kernel/ksyms.h"
#include "kernel/fastops.h"

extern "C" uint64_t get_physical_address(uint64_t v);
extern "C" void     unmap_page(uint64_t v);
extern "C" int      map_page(uint64_t v, uint64_t p, uint64_t f);
extern "C" int      map_pages_bulk(uint64_t vs, uint64_t ps, size_t c, uint64_t f);
extern "C" void     memzero_nt_avx(void* dest, size_t count);

#define HEAP_HEADER_SIZE 16
#define HEAP_FOOTER_SIZE  8
#define SLAB_SMALL_PAGES  1
#define SLAB_LARGE_PAGES  2
#define ALIGN_UP(x,a)  (((x)+((a)-1))&~((a)-1))

extern KernelHeap* g_Heap;

static inline void smart_zero(void* p, size_t s) {
    if (s >= PAGE_SIZE) { memzero_nt_avx(p, s); } else { memset(p, 0, s); }
}

void VmArena::init(uint64_t start, uint64_t limit) {
    watermark_ = start; limit_ = limit; count_ = 0;
    memset(regions_, 0, sizeof(regions_));
}

int VmArena::lower_bound(uint64_t addr) const {
    int lo = 0, hi = count_;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (regions_[mid].base < addr) { lo = mid + 1; } else { hi = mid; }
    }
    return lo;
}

void VmArena::insert_at(int idx, uint64_t base, uint32_t pages) {
    for (int i = count_; i > idx; i--) { regions_[i] = regions_[i-1]; }
    regions_[idx] = {base, pages, 0};
    count_++;
}

void VmArena::remove_at(int idx) {
    for (int i = idx; i < count_-1; i++) { regions_[i] = regions_[i+1]; }
    count_--;
}

void VmArena::coalesce_around(int idx) {
    if (idx+1 < count_) {
        if (regions_[idx].base + (uint64_t)regions_[idx].pages*PAGE_SIZE == regions_[idx+1].base) {
            regions_[idx].pages += regions_[idx+1].pages; remove_at(idx+1);
        } else {
            // Gap maintained
        }
    } else {
        // Upper edge condition met
    }
    if (idx > 0) {
        if (regions_[idx-1].base + (uint64_t)regions_[idx-1].pages*PAGE_SIZE == regions_[idx].base) {
            regions_[idx-1].pages += regions_[idx].pages; remove_at(idx);
        } else {
            // Uncoalesced boundary
        }
    } else {
        // Lower edge condition met
    }
}

uint64_t VmArena::alloc(size_t pages) {
    if (unlikely(!pages)) { return 0; } else { /* Size validated */ }
    for (int i = 0; i < count_; i++) {
        if (regions_[i].pages >= (uint32_t)pages) {
            uint64_t base = regions_[i].base;
            if (regions_[i].pages == (uint32_t)pages) { remove_at(i); } else { regions_[i].base += (uint64_t)pages*PAGE_SIZE; regions_[i].pages -= (uint32_t)pages; }
            return base;
        } else {
            // Proceed to next region
        }
    }
    uint64_t base = watermark_;
    if (unlikely(base + pages*PAGE_SIZE > limit_ || base + pages*PAGE_SIZE < base)) { 
        serial_write("[HEAP] CRITICAL: VA space exhausted\n"); return 0;
    } else {
        // Virtual bound fits perfectly
    }
    watermark_ = base + pages*PAGE_SIZE;
    return base;
}

uint64_t VmArena::alloc_aligned(size_t pages, size_t align_pages) {
    if (unlikely(!pages || !align_pages)) { return 0; } else { /* Size checked */ }
    uint64_t ab = (uint64_t)align_pages * PAGE_SIZE;
    for (int i = 0; i < count_; i++) {
        uint64_t base  = regions_[i].base;
        uint64_t aligned = ALIGN_UP(base, ab);
        uint64_t gap   = (aligned - base) / PAGE_SIZE;
        uint64_t need  = gap + pages;
        
        if (regions_[i].pages < (uint32_t)need) { continue; } else { /* Memory fits safely within boundaries */ }
        
        uint64_t rem   = regions_[i].pages - (uint32_t)need;
        if (gap > 0) {
            regions_[i].pages = (uint32_t)gap;
            if (rem > 0) {
                uint64_t tb = aligned + (uint64_t)pages*PAGE_SIZE;
                int ins = lower_bound(tb);
                if (count_ < VM_ARENA_MAX_REGIONS) { insert_at(ins, tb, (uint32_t)rem); } else { /* Drop extra fragmented area */ }
            } else {
                // Gap exists but remaining is strictly 0. Handled by earlier code
            }
        } else {
            if (!rem) { remove_at(i); } else { regions_[i].base = aligned + (uint64_t)pages*PAGE_SIZE; regions_[i].pages = (uint32_t)rem; }
        }
        return aligned;
    }
    
    uint64_t aligned = ALIGN_UP(watermark_, ab);
    if (unlikely(aligned + pages*PAGE_SIZE > limit_)) {
        serial_write("[HEAP] CRITICAL: VA space exhausted (aligned)\n"); return 0;
    } else {
        // Bounds respected
    }
    
    if (aligned > watermark_) {
        uint32_t gap_pages = (aligned - watermark_) / PAGE_SIZE;
        if (count_ < VM_ARENA_MAX_REGIONS) {
            int ins = lower_bound(watermark_);
            insert_at(ins, watermark_, gap_pages);
            coalesce_around(ins);
        } else {
            // Region list full. Virtual memory gap permanently lost. 
        }
    } else {
        // Already aligned directly to watermark.
    }
    
    watermark_ = aligned + pages*PAGE_SIZE;
    return aligned;
}

void VmArena::free(uint64_t base, size_t pages) {
    if (unlikely(!base || !pages)) { return; } else { /* Pointer is active */ }
    if (count_ >= VM_ARENA_MAX_REGIONS - 2) { 
        serial_write("[HEAP] VmArena nearly full, coalesce forced\n"); 
        coalesce_around(count_ - 1); 
    } else {
        // Capacity logic normal
    } 
    uint32_t pg = (pages > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)pages;
    int idx = lower_bound(base);
    insert_at(idx, base, pg);
    coalesce_around(idx);
}

void SlabCache::init(const char* n, size_t size) {
    name   = n;
    obj_size = ALIGN_UP(size + HEAP_HEADER_SIZE + HEAP_FOOTER_SIZE, 16);
    partial = full = empty = nullptr;
    total_pages_allocated = 0;
    spinlock_init(&lock);
}

void SlabCache::removeSlabFromList(Slab* s, Slab** h) {
    if (s->prev) { s->prev->next = s->next; } else { *h = s->next; }
    if (s->next) { s->next->prev = s->prev; } else { /* Reached tail */ }
    s->prev = s->next = nullptr;
}

void SlabCache::addSlabToList(Slab* s, Slab** h) {
    s->next = *h; s->prev = nullptr;
    if (*h) { (*h)->prev = s; } else { /* Empty head */ }
    *h = s;
}

void SlabCache::freeSlab(Slab* s) {
    size_t   pages = s->page_count;
    uint64_t virt  = (uint64_t)s->data_page;
    for (size_t i = 0; i < pages; i++) {
        uint64_t v = virt + i*PAGE_SIZE;
        uint64_t pa = get_physical_address(v);
        unmap_page(v);
        if (pa) { pmm_free_frame((void*)pa); } else { /* Memory unit lost reference */ }
    }
    uint64_t flags = spinlock_acquire(&g_Heap->vm_lock);
    g_Heap->vm_arena.free(virt, pages);
    spinlock_release(&g_Heap->vm_lock, flags);
    total_pages_allocated -= pages;
}

Slab* SlabCache::createSlab() {
    size_t slab_pages = (obj_size > 2048) ? SLAB_LARGE_PAGES : SLAB_SMALL_PAGES;
    void* page = nullptr;
    {
        uint64_t f = spinlock_acquire(&g_Heap->vm_lock);
        if (obj_size > 2048) {
            page = (void*)g_Heap->vm_arena.alloc_aligned(slab_pages, slab_pages);
        } else {
            page = (void*)g_Heap->vm_arena.alloc_aligned(slab_pages, slab_pages);
        }
        spinlock_release(&g_Heap->vm_lock, f);
    }
    
    if (unlikely(!page)) { return nullptr; } else { /* VMM mapping started */ }

    {
        uint64_t f = spinlock_acquire(&g_Heap->vm_lock);
        uint64_t virt = (uint64_t)page;
        void* phys = pmm_alloc_contiguous(slab_pages);
        
        if (likely(phys)) {
            if (!map_pages_bulk(virt, (uint64_t)phys, slab_pages, PAGE_PRESENT|PAGE_WRITE|PAGE_NX)) {
                pmm_free_contiguous(phys, slab_pages);
                g_Heap->vm_arena.free(virt, slab_pages);
                spinlock_release(&g_Heap->vm_lock, f);
                return nullptr;
            } else {
                // Table successfully generated
            }
            memzero_nt_avx((void*)virt, slab_pages * PAGE_SIZE);
        } else {
            for (size_t i = 0; i < slab_pages; i++) {
                void* p = pmm_alloc_frame();
                if (unlikely(!p)) {
                    for (size_t j = 0; j < i; j++) {
                        uint64_t v = virt+j*PAGE_SIZE;
                        uint64_t pa = get_physical_address(v);
                        unmap_page(v); 
                        if (pa) { pmm_free_frame((void*)pa); } else { /* Error context saved */ }
                    }
                    g_Heap->vm_arena.free(virt, slab_pages);
                    spinlock_release(&g_Heap->vm_lock, f);
                    return nullptr;
                } else {
                    // Mapped to single frames
                }
                map_page(virt+i*PAGE_SIZE, (uint64_t)p, PAGE_PRESENT|PAGE_WRITE|PAGE_NX);
                smart_zero((void*)(virt+i*PAGE_SIZE), PAGE_SIZE);
            }
        }
        spinlock_release(&g_Heap->vm_lock, f);
    }

    Slab* slab       = (Slab*)page;
    slab->data_page  = page;
    slab->used_count = 0;
    slab->next = slab->prev = nullptr;
    slab->page_count = (uint16_t)slab_pages;
    uintptr_t ds = ALIGN_UP((uintptr_t)page + sizeof(Slab), 16);
    size_t avail = slab_pages*PAGE_SIZE - (ds - (uintptr_t)page);
    slab->total_count = (uint16_t)(avail / obj_size);
    slab->free_list   = (void*)ds;
    uint8_t* ptr = (uint8_t*)ds;
    
    for (int i = 0; i < slab->total_count - 1; i++) {
        *((void**)ptr) = (void*)(ptr + obj_size); ptr += obj_size;
    }
    *((void**)ptr) = nullptr;
    
    total_pages_allocated += slab_pages;
    return slab;
}

void* SlabCache::alloc(uint64_t caller) {
    uint64_t f = spinlock_acquire(&lock);
    Slab* slab = partial;
    
    if (unlikely(!slab)) {
        if (empty) { 
            slab = empty; 
            removeSlabFromList(slab, &empty); 
            addSlabToList(slab, &partial); 
        } else {
            spinlock_release(&lock, f);
            Slab* ns = createSlab();
            f = spinlock_acquire(&lock);
            
            if (unlikely(!ns)) { 
                spinlock_release(&lock, f); 
                return nullptr; 
            } else {
                // BUG-001 FIX: Double-Checked Locking (Race condition prevention)
                // If another SMP thread populated the partial list while we were unlocked:
                if (partial != nullptr) {
                    freeSlab(ns); // Discard our newly created slab to prevent memory waste!
                    slab = partial;
                } else {
                    addSlabToList(ns, &partial); 
                    slab = ns;
                }
            }
        }
    } else {
        // Standard caching path
    }
    
    void* obj = slab->free_list;
    uintptr_t send = (uintptr_t)slab->data_page + (size_t)slab->page_count*PAGE_SIZE;
    
    if (unlikely(obj != nullptr && ((uintptr_t)obj < (uintptr_t)slab->data_page || (uintptr_t)obj >= send))) { 
        serial_write("[HEAP] Slab corruption detected\n");
        spinlock_release(&lock, f); 
        return nullptr;
    } else {
        // Block is valid and protected
    }
    
    slab->free_list = *((void**)obj);
    slab->used_count++;
    
    if (!slab->free_list) { 
        removeSlabFromList(slab, &partial); 
        addSlabToList(slab, &full); 
    } else {
        // Still has some empty blocks
    }
    
    spinlock_release(&lock, f);
    (void)caller;
    return obj;
}

bool SlabCache::free(void* ptr) {
    if (unlikely(!ptr)) { return false; } else { /* Active Reference */ }
    
    uint64_t f = spinlock_acquire(&lock);
    
    size_t slab_pages = (obj_size > 2048) ? SLAB_LARGE_PAGES : SLAB_SMALL_PAGES;
    size_t slab_size_bytes = slab_pages * PAGE_SIZE;
    
    Slab* found = (Slab*)((uintptr_t)ptr & ~(slab_size_bytes - 1));
    
    if (unlikely(found->page_count != slab_pages || found->total_count == 0)) {
        spinlock_release(&lock, f);
        return false;
    } else {
        // Verified cache container
    }

    Slab** src = nullptr;
    if (found->used_count == found->total_count) { src = &full; } else { src = &partial; }

    *((void**)ptr) = found->free_list;
    found->free_list = ptr;
    found->used_count--;

    if (src == &full) { 
        removeSlabFromList(found, &full); 
        addSlabToList(found, &partial); 
    } else {
        // Remains in partial array list
    }
    
    if (found->used_count == 0) { 
        removeSlabFromList(found, &partial); 
        addSlabToList(found, &empty); 
    } else {
        // Partially active, kept alive
    }
    
    spinlock_release(&lock, f);
    return true;
}

void SlabCache::trim() {
    uint64_t f = spinlock_acquire(&lock);
    Slab* s = empty;
    while (s) { 
        Slab* n = s->next; 
        removeSlabFromList(s, &empty); 
        freeSlab(s); 
        s = n; 
    }
    spinlock_release(&lock, f);
}

size_t SlabCache::getCachedSize() {
    uint64_t f = spinlock_acquire(&lock);
    size_t sz = (obj_size > 2048) ? SLAB_LARGE_PAGES : SLAB_SMALL_PAGES;
    size_t total = 0;
    for (Slab* e = empty; e; e = e->next) { total += sz * PAGE_SIZE; }
    spinlock_release(&lock, f);
    return total;
}

size_t SlabCache::getUsedPayload() {
    uint64_t f = spinlock_acquire(&lock);
    size_t total = 0;
    for (Slab* s = partial; s; s = s->next) { total += s->used_count * obj_size; }
    for (Slab* s = full;    s; s = s->next) { total += s->used_count * obj_size; }
    spinlock_release(&lock, f);
    return total;
}

size_t SlabCache::getTotalReserved() {
    return total_pages_allocated * PAGE_SIZE;
}
