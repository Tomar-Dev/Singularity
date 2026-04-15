// archs/memory/kheap.cpp
#include "memory/kheap.h"
#include "memory/pmm.h"
#include "memory/paging.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "drivers/serial/serial.h"
#include "kernel/debug.h"
#include "system/security/rng.h"
#include "kernel/config.h"
#include "kernel/fastops.h"
#include "kernel/ksyms.h"
#include "system/security/gwp_asan.h" 

inline void* operator new(size_t, void* p) { return p; }

#ifndef SIZE_MAX
#define SIZE_MAX (static_cast<size_t>(-1))
#endif

#define HEAP_ASLR_MASK    0x00FFFFFF
#define HEAP_HEADER_SIZE  16
#define HEAP_FOOTER_SIZE  8
#define ASM_THRESHOLD     256
#define POISON_VALUE      0xBD

#define ALIGN_UP(x, align)   (((x) + ((align) - 1)) & ~((align) - 1))

static const uint32_t bucket_sizes[NUM_BUCKETS] = {
    32, 64, 128, 256, 512, 1024, 2048, 4096
};

uint64_t kheap_magic  = 0;
uint64_t kheap_canary = 0;

KernelHeap* g_Heap = nullptr;
alignas(KernelHeap) static uint8_t heap_instance_buffer[sizeof(KernelHeap)];

extern "C" void memzero_nt_avx(void* dest, size_t count);

static inline void fast_poison(void* ptr, size_t size) {
    if (size == 0) return;
    
    if (size < ASM_THRESHOLD) { 
        // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
        memset(ptr, POISON_VALUE, size); 
    } else {
        uint64_t* p = reinterpret_cast<uint64_t*>(ptr);
        size_t qw   = size / 8;
        uint64_t val = 0xBDBDBDBDBDBDBDBDULL;
        __asm__ volatile("rep stosq" : "+D"(p), "+c"(qw) : "a"(val) : "memory");
        size_t rem = size % 8;
        if (rem > 0) {
            // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
            memset(reinterpret_cast<uint8_t*>(p), POISON_VALUE, rem);
        }
    }
}

static inline void smart_zero(void* ptr, size_t size) {
    if (size >= static_cast<size_t>(PAGE_SIZE)) {
        memzero_nt_avx(ptr, size);
    } else {
        // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
        memset(ptr, 0, size);
    }
}

KernelHeap::KernelHeap()
    : magic(0), canary(0), big_alloc_bytes(0) {
    // NOLINTNEXTLINE(clang-diagnostic-missing-field-initializers)
    spinlock_init(&vm_lock);
}

uint64_t KernelHeap::getAslrOffset() {
    return (get_secure_random() & HEAP_ASLR_MASK) & ~0xFFFULL;
}

void KernelHeap::init() {
    magic  = get_secure_random();
    canary = get_secure_random();
    if (!magic)  magic  = 0xC0FFEE1234567890ULL;
    if (!canary) canary = 0xDEADBEEFCAFEBABEULL;

    kheap_magic  = magic;
    kheap_canary = canary;

    uint64_t aslr  = getAslrOffset();
    uint64_t start = KHEAP_START_VIRTUAL + aslr;
    uint64_t end_va = KHEAP_START_VIRTUAL + KHEAP_MAX_SIZE + HEAP_ASLR_MASK;

    vm_arena.init(start, end_va);

    for (int i = 0; i < NUM_BUCKETS; i++) {
        buckets[i].init("kmalloc_bucket", bucket_sizes[i]);
    }
    
    gwp_asan_init(); // GWP-ASAN Sistemi başlatılıyor

    char buf[160];
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    snprintf(buf, sizeof(buf),
             "[HEAP] Hardened v6.9 Active. Magic: %lx  Canary: %lx  ASLR: %lx\n",
             magic, canary, aslr);
    serial_write(buf);

    g_Heap = this;
}

int KernelHeap::getBucketIndex(size_t size) {
    if (size == 0 || size > 4096) return -1;
    if (size <= 32) return 0;
    
    int log2 = 32 - __builtin_clz(static_cast<unsigned int>(size - 1));
    int idx  = log2 - 5;
    return (idx < 0) ? 0 : idx;
}

void* KernelHeap::vmm_alloc(size_t pages) {
    uint64_t virt = vm_arena.alloc(pages);
    if (unlikely(!virt)) return nullptr;

    void* phys = pmm_alloc_contiguous(pages);
    if (likely(phys)) {
        if (!map_pages_bulk(virt, reinterpret_cast<uint64_t>(phys), pages,
                            PAGE_PRESENT | PAGE_WRITE | PAGE_NX)) {
            pmm_free_contiguous(phys, pages);
            vm_arena.free(virt, pages);
            return nullptr;
        }
        memzero_nt_avx(reinterpret_cast<void*>(virt), pages * PAGE_SIZE);
    } else {
        for (size_t i = 0; i < pages; i++) {
            void* p = pmm_alloc_frame();
            if (unlikely(!p)) {
                for (size_t j = 0; j < i; j++) {
                    uint64_t v  = virt + j * PAGE_SIZE;
                    uint64_t pa = get_physical_address(v);
                    unmap_page(v);
                    if (pa) pmm_free_frame(reinterpret_cast<void*>(pa));
                }
                vm_arena.free(virt, pages);
                return nullptr;
            }
            if (!map_page(virt + i * PAGE_SIZE, reinterpret_cast<uint64_t>(p),
                     PAGE_PRESENT | PAGE_WRITE | PAGE_NX)) {
                pmm_free_frame(p);
                for (size_t j = 0; j < i; j++) {
                    uint64_t v  = virt + j * PAGE_SIZE;
                    uint64_t pa = get_physical_address(v);
                    unmap_page(v);
                    if (pa) pmm_free_frame(reinterpret_cast<void*>(pa));
                }
                vm_arena.free(virt, pages);
                return nullptr;
            }
            smart_zero(reinterpret_cast<void*>(virt + i * PAGE_SIZE), PAGE_SIZE);
        }
    }
    return reinterpret_cast<void*>(virt);
}

void* KernelHeap::vmm_alloc_aligned(size_t pages, size_t align_pages) {
    uint64_t virt = vm_arena.alloc_aligned(pages, align_pages);
    if (unlikely(!virt)) return nullptr;

    void* phys = pmm_alloc_contiguous(pages);
    if (likely(phys)) {
        if (!map_pages_bulk(virt, reinterpret_cast<uint64_t>(phys), pages,
                            PAGE_PRESENT | PAGE_WRITE | PAGE_NX)) {
            pmm_free_contiguous(phys, pages);
            vm_arena.free(virt, pages);
            return nullptr;
        }
        memzero_nt_avx(reinterpret_cast<void*>(virt), pages * PAGE_SIZE);
    } else {
        for (size_t i = 0; i < pages; i++) {
            void* p = pmm_alloc_frame();
            if (unlikely(!p)) {
                for (size_t j = 0; j < i; j++) {
                    uint64_t v  = virt + j * PAGE_SIZE;
                    uint64_t pa = get_physical_address(v);
                    unmap_page(v);
                    if (pa) pmm_free_frame(reinterpret_cast<void*>(pa));
                }
                vm_arena.free(virt, pages);
                return nullptr;
            }
            if (!map_page(virt + i * PAGE_SIZE, reinterpret_cast<uint64_t>(p),
                     PAGE_PRESENT | PAGE_WRITE | PAGE_NX)) {
                pmm_free_frame(p);
                for (size_t j = 0; j < i; j++) {
                    uint64_t v  = virt + j * PAGE_SIZE;
                    uint64_t pa = get_physical_address(v);
                    unmap_page(v);
                    if (pa) pmm_free_frame(reinterpret_cast<void*>(pa));
                }
                vm_arena.free(virt, pages);
                return nullptr;
            }
            smart_zero(reinterpret_cast<void*>(virt + i * PAGE_SIZE), PAGE_SIZE);
        }
    }
    return reinterpret_cast<void*>(virt);
}

void* KernelHeap::malloc(size_t size, uint64_t caller) {
    if (unlikely(size == 0)) return nullptr;
    
    // GÜVENLİK YAMASI: GWP-ASAN Rastgele Örneklemeli Bellek Koruması
    if (likely(size < PAGE_SIZE)) {
        if (unlikely(gwp_asan_should_sample())) {
            void* gwp_ptr = gwp_asan_malloc(size, caller);
            if (gwp_ptr) return gwp_ptr;
        }
    }

    size_t total_req = HEAP_HEADER_SIZE + size + HEAP_FOOTER_SIZE;
    if (unlikely(total_req < size)) return nullptr;

    if (likely(total_req <= 4096)) {
        int idx = getBucketIndex(total_req);
        if (likely(idx >= 0 && idx < NUM_BUCKETS)) {
            void* ptr = buckets[idx].alloc(caller);
            if (likely(ptr)) {
                uint64_t* hdr = reinterpret_cast<uint64_t*>(ptr);
                hdr[0] = bucket_sizes[idx];
                hdr[1] = size ^ magic;
                uint64_t* ftr = reinterpret_cast<uint64_t*>(reinterpret_cast<uintptr_t>(ptr) + HEAP_HEADER_SIZE + size);
                *ftr = canary;
                return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(ptr) + HEAP_HEADER_SIZE);
            }
            return nullptr;
        }
    }

    uint64_t flags = spinlock_acquire(&vm_lock);
    size_t pages   = ALIGN_UP(total_req, PAGE_SIZE) / PAGE_SIZE;
    void*  ptr     = vmm_alloc(pages);
    if (likely(ptr)) {
        uint64_t* hdr = reinterpret_cast<uint64_t*>(ptr);
        hdr[0] = static_cast<uint64_t>(pages) * PAGE_SIZE;
        hdr[1] = size ^ magic;
        uint64_t* ftr = reinterpret_cast<uint64_t*>(reinterpret_cast<uintptr_t>(ptr) + HEAP_HEADER_SIZE + size);
        *ftr = canary;
        big_alloc_bytes += pages * PAGE_SIZE;
        spinlock_release(&vm_lock, flags);
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(ptr) + HEAP_HEADER_SIZE);
    }
    spinlock_release(&vm_lock, flags);
    return nullptr;
}

void KernelHeap::free(void* ptr) {
    if (unlikely(!ptr)) return;
    
    uint64_t caller = reinterpret_cast<uint64_t>(__builtin_return_address(0));
    
    // GÜVENLİK YAMASI: GWP-ASAN Belleği İade Ediliyorsa Özel İşleme Sok
    if (unlikely(gwp_asan_is_managed(ptr))) {
        gwp_asan_free(ptr, caller);
        return;
    }

    uintptr_t addr  = reinterpret_cast<uintptr_t>(ptr);
    uint64_t  h_start = KHEAP_START_VIRTUAL;
    uint64_t  h_end   = KHEAP_START_VIRTUAL + KHEAP_MAX_SIZE + HEAP_ASLR_MASK;

    if (unlikely(addr < h_start || addr > h_end)) return;
    if (unlikely((addr & 0x7) != 0)) return;

    void*    real_ptr   = reinterpret_cast<void*>(addr - HEAP_HEADER_SIZE);
    uint64_t bucket_size = *reinterpret_cast<uint64_t*>(real_ptr);

    if (unlikely(bucket_size & (1ULL << 63))) {
        char msg[256];
        uint64_t off = 0;
        const char* sym = ksyms_resolve_symbol(caller, &off);
        // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
        snprintf(msg, sizeof(msg), "Double Free Detected: ptr=0x%lx caller=%s+0x%lx",
                 reinterpret_cast<uint64_t>(ptr), sym ? sym : "???", off);
        panic_at("HEAP", 0, KERR_HEAP_DOUBLE_FREE, msg);
    }

    uint64_t user_size = *(reinterpret_cast<uint64_t*>(real_ptr) + 1) ^ magic;
    if (unlikely(user_size > bucket_size)) {
        panic_at("HEAP", 0, KERR_MEM_CORRUPT, "Heap Corruption: user_size > bucket_size");
    }

    uint64_t* ftr = reinterpret_cast<uint64_t*>(addr + user_size);
    if (unlikely(*ftr != canary)) {
        char msg[256];
        // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
        snprintf(msg, sizeof(msg), "Heap Overflow: canary 0x%lx != 0x%lx (size: %lu)", *ftr, canary, user_size);
        panic_at("HEAP", 0, KERR_HEAP_OVERFLOW, msg);
    }

    fast_poison(ptr, user_size);
    *reinterpret_cast<uint64_t*>(real_ptr) = bucket_size | (1ULL << 63);

    if (likely(bucket_size <= 4096)) {
        int idx = getBucketIndex(bucket_size);
        if (likely(idx >= 0 && idx < NUM_BUCKETS)) {
            buckets[idx].free(real_ptr);
        }
        return;
    }

    size_t pages = bucket_size / PAGE_SIZE;
    uint64_t flags = spinlock_acquire(&vm_lock);
    for (size_t i = 0; i < pages; i++) {
        uint64_t v  = reinterpret_cast<uintptr_t>(real_ptr) + i * PAGE_SIZE;
        uint64_t pa = get_physical_address(v);
        unmap_page(v);
        if (pa) pmm_free_frame(reinterpret_cast<void*>(pa));
    }
    vm_arena.free(reinterpret_cast<uint64_t>(real_ptr), pages);
    big_alloc_bytes -= bucket_size;
    spinlock_release(&vm_lock, flags);
}

void* KernelHeap::calloc(size_t num, size_t size, uint64_t caller) {
    if (num != 0 && size > (static_cast<size_t>(-1)) / num) return nullptr;
    void* p = malloc(num * size, caller);
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    if (p) memset(p, 0, num * size);
    return p;
}

void* KernelHeap::realloc(void* ptr, size_t new_size, uint64_t caller) {
    if (!ptr) return malloc(new_size, caller);
    if (!new_size) { free(ptr); return nullptr; }
    
    // GWP-ASAN realloc kontrolü
    if (unlikely(gwp_asan_is_managed(ptr))) {
        void* np = malloc(new_size, caller);
        if (np) {
            memcpy(np, ptr, new_size); 
            free(ptr);
        }
        return np;
    }
    
    void* rp = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(ptr) - HEAP_HEADER_SIZE);
    uint64_t old_user_size = *(reinterpret_cast<uint64_t*>(rp) + 1) ^ magic;
    
    void* np = malloc(new_size, caller);
    if (np) { 
        memcpy(np, ptr, (old_user_size < new_size) ? old_user_size : new_size); 
        free(ptr); 
    }
    return np;
}

void* KernelHeap::mallocAligned(size_t size, size_t alignment, uint64_t caller) {
    if (!alignment || (alignment & (alignment - 1))) return nullptr;
    if (alignment >= static_cast<size_t>(PAGE_SIZE)) return mallocDma(size, alignment, caller);
    
    size_t total = size + alignment + sizeof(void*);
    if (total < size) return nullptr; 
    
    void* raw = malloc(total, caller);
    if (!raw) return nullptr;
    
    uintptr_t aligned = ALIGN_UP(reinterpret_cast<uintptr_t>(raw) + sizeof(void*), alignment);
    *reinterpret_cast<void**>(aligned - sizeof(void*)) = raw;
    return reinterpret_cast<void*>(aligned);
}

void KernelHeap::freeAligned(void* ptr) {
    if (!ptr) return;
    if ((reinterpret_cast<uintptr_t>(ptr) & (static_cast<size_t>(PAGE_SIZE) - 1)) == 0) { 
        free(ptr); 
        return; 
    }
    void* rp = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(ptr) - sizeof(void*));
    free(rp);
}

void* KernelHeap::mallocDma(size_t size, size_t alignment, uint64_t caller) {
    (void)caller;
    if (!size || !alignment) return nullptr;
    if (alignment < static_cast<size_t>(PAGE_SIZE)) alignment = PAGE_SIZE;
    
    size_t pages     = ALIGN_UP(size, PAGE_SIZE) / PAGE_SIZE;
    size_t align_pgs = alignment / PAGE_SIZE;
    
    uint64_t flags   = spinlock_acquire(&vm_lock);
    void* ptr = vmm_alloc_aligned(pages, align_pgs);
    if (likely(ptr)) big_alloc_bytes += pages * PAGE_SIZE;
    spinlock_release(&vm_lock, flags);
    return ptr;
}

void KernelHeap::freeDma(void* ptr, size_t size) {
    if (!ptr || !size) return;
    size_t   pages = ALIGN_UP(size, PAGE_SIZE) / PAGE_SIZE;
    uint64_t virt  = reinterpret_cast<uintptr_t>(ptr);
    
    fast_poison(ptr, size);
    
    uint64_t flags = spinlock_acquire(&vm_lock);
    for (size_t i = 0; i < pages; i++) {
        uint64_t v  = virt + i * PAGE_SIZE;
        uint64_t pa = get_physical_address(v);
        unmap_page(v);
        if (pa) pmm_free_frame(reinterpret_cast<void*>(pa));
    }
    vm_arena.free(virt, pages);
    if (big_alloc_bytes >= pages * PAGE_SIZE) big_alloc_bytes -= pages * PAGE_SIZE;
    spinlock_release(&vm_lock, flags);
}

void* KernelHeap::mallocContiguous(size_t size, uint64_t caller) {
    (void)caller;
    if (!size) return nullptr;
    size_t pages = ALIGN_UP(size, PAGE_SIZE) / PAGE_SIZE;
    
    uint64_t flags = spinlock_acquire(&vm_lock);
    uint64_t virt  = vm_arena.alloc(pages);
    if (unlikely(!virt)) { 
        spinlock_release(&vm_lock, flags); 
        return nullptr; 
    }
    
    void* phys = pmm_alloc_contiguous(pages);
    if (unlikely(!phys)) {
        vm_arena.free(virt, pages);
        spinlock_release(&vm_lock, flags);
        return nullptr;
    }
    
    if (!map_pages_bulk(virt, reinterpret_cast<uint64_t>(phys), pages,
                        PAGE_PRESENT | PAGE_WRITE | PAGE_NX)) {
        pmm_free_contiguous(phys, pages);
        vm_arena.free(virt, pages);
        spinlock_release(&vm_lock, flags);
        return nullptr;
    }
    
    memzero_nt_avx(reinterpret_cast<void*>(virt), pages * PAGE_SIZE);
    big_alloc_bytes += pages * PAGE_SIZE;
    spinlock_release(&vm_lock, flags);
    return reinterpret_cast<void*>(virt);
}

void KernelHeap::freeContiguous(void* ptr, size_t size) {
    if (!ptr || !size) return;
    size_t   pages = ALIGN_UP(size, PAGE_SIZE) / PAGE_SIZE;
    uint64_t virt  = reinterpret_cast<uintptr_t>(ptr);
    
    fast_poison(ptr, size);
    
    uint64_t flags = spinlock_acquire(&vm_lock);
    for (size_t i = 0; i < pages; i++) {
        uint64_t v  = virt + i * PAGE_SIZE;
        uint64_t pa = get_physical_address(v);
        unmap_page(v);
        if (pa) pmm_free_frame(reinterpret_cast<void*>(pa));
    }
    vm_arena.free(virt, pages);
    if (big_alloc_bytes >= pages * PAGE_SIZE) big_alloc_bytes -= pages * PAGE_SIZE;
    spinlock_release(&vm_lock, flags);
}

void KernelHeap::trim() {
    for (int i = 0; i < NUM_BUCKETS; i++) {
        buckets[i].trim();
    }
}

size_t KernelHeap::getCachedSize() {
    size_t t = 0;
    for (int i = 0; i < NUM_BUCKETS; i++) {
        t += buckets[i].getCachedSize();
    }
    return t;
}

size_t KernelHeap::getUsedPayload() {
    size_t t = 0;
    for (int i = 0; i < NUM_BUCKETS; i++) {
        t += buckets[i].getUsedPayload();
    }
    return t + big_alloc_bytes;
}

size_t KernelHeap::getTotalReserved() {
    size_t t = 0;
    for (int i = 0; i < NUM_BUCKETS; i++) {
        t += buckets[i].getTotalReserved();
    }
    return t + big_alloc_bytes;
}

void KernelHeap::printStats() {
    char buf[128];
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    snprintf(buf, sizeof(buf),
             "Heap watermark: 0x%lx | Free regions: %d | Large allocs: %zu B\n",
             vm_arena.watermark(), vm_arena.free_region_count(), big_alloc_bytes);
    serial_write(buf);
}

extern "C" {

void init_kheap() {
    g_Heap = new (heap_instance_buffer) KernelHeap();
    g_Heap->init();
}

void* kmalloc(size_t size) {
    return g_Heap->malloc(size, reinterpret_cast<uint64_t>(__builtin_return_address(0)));
}

void kfree(void* ptr) {
    g_Heap->free(ptr);
}

void* kcalloc(size_t num, size_t size) {
    return g_Heap->calloc(num, size, reinterpret_cast<uint64_t>(__builtin_return_address(0)));
}

void* krealloc(void* ptr, size_t new_size) {
    return g_Heap->realloc(ptr, new_size, reinterpret_cast<uint64_t>(__builtin_return_address(0)));
}

void* kmalloc_aligned(size_t size, size_t alignment) {
    return g_Heap->mallocAligned(size, alignment, reinterpret_cast<uint64_t>(__builtin_return_address(0)));
}

void kfree_aligned(void* ptr) {
    g_Heap->freeAligned(ptr);
}

void* kmalloc_dma(size_t size, size_t alignment) {
    return g_Heap->mallocDma(size, alignment, reinterpret_cast<uint64_t>(__builtin_return_address(0)));
}

void kfree_dma(void* ptr, size_t size) {
    g_Heap->freeDma(ptr, size);
}

void* kmalloc_contiguous(size_t size) {
    return g_Heap->mallocContiguous(size, reinterpret_cast<uint64_t>(__builtin_return_address(0)));
}

void kfree_contiguous(void* ptr, size_t size) {
    g_Heap->freeContiguous(ptr, size);
}

void kheap_print_stats() {
    if (g_Heap) g_Heap->printStats();
}

void kheap_trim() {
    if (g_Heap) g_Heap->trim();
}

size_t kheap_get_cached_size() {
    return g_Heap ? g_Heap->getCachedSize() : 0;
}

size_t kheap_get_payload_size() {
    return g_Heap ? g_Heap->getUsedPayload() : 0;
}

size_t kheap_get_reserved_size() {
    return g_Heap ? g_Heap->getTotalReserved() : 0;
}

}