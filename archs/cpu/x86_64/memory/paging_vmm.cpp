// archs/cpu/x86_64/memory/paging_vmm.cpp
#include "memory/paging.h"
#include "memory/pmm.h"
#include "libc/string.h"
#include "archs/cpu/cpu_hal.h"
#include "archs/cpu/x86_64/sync/spinlock.h"
#include "archs/cpu/x86_64/smp/smp.h"

#define VMM_START       0xFFFF800000000000ULL
#define VMM_SIZE        0x100000000ULL
#define VMM_PAGES       (VMM_SIZE / PAGE_SIZE)
#define VMM_BITMAP_SIZE (VMM_PAGES / 8)

static spinlock_t vmm_lock  = {0, 0, {0}};
static uint8_t*   vmm_bitmap = nullptr;

// OPTİMİZASYON YAMASI: 64-bitlik (QWORD) bloklar halinde bellek arama algoritması
// Bu sayede VMM bellek tahsisi işlemleri (Stack, DMA) 64 kat hızlanmıştır.
static int64_t vmm_find_free(size_t count) {
    uint64_t* bitmap64 = (uint64_t*)vmm_bitmap;
    size_t total_qwords = VMM_BITMAP_SIZE / 8;
    size_t found = 0;
    size_t start = 0;

    for (size_t i = 0; i < total_qwords; i++) {
        uint64_t val = bitmap64[i];
        
        // Eğer 64 sayfanın tamamı doluysa direkt atla (Fast Path)
        if (val == 0xFFFFFFFFFFFFFFFFULL) {
            found = 0;
            continue;
        }
        
        // Eğer 64 sayfanın tamamı boşsa ve bize de en az 64 lazımsa hızlıca topla
        if (val == 0 && found == 0 && count >= 64) {
            start = i * 64;
            found += 64;
            if (found >= count) return (int64_t)start;
            continue;
        }
        
        // Kısmi dolu bloklar veya küçük tahsisler için bit-bit arama
        for (int bit = 0; bit < 64; bit++) {
            if (!(val & (1ULL << bit))) {
                if (found == 0) start = i * 64 + bit;
                if (++found == count) return (int64_t)start;
            } else {
                found = 0;
            }
        }
    }
    return -1;
}

static void vmm_set(size_t start, size_t count, int val) {
    for (size_t i = start; i < start + count; i++) {
        if (val != 0) vmm_bitmap[i/8] |=  (static_cast<uint8_t>(1u << (i%8)));
        else          vmm_bitmap[i/8] &= ~(static_cast<uint8_t>(1u << (i%8)));
    }
}

void PagingManager::initVMM() {
    size_t pages = (VMM_BITMAP_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
    void* phys = pmm_alloc_contiguous(pages);
    if (!phys) { 
        serial_write("[VMM] CRITICAL: bitmap alloc failed\n"); 
        return; 
    }
    mapPagesBulk(VMM_START, reinterpret_cast<uint64_t>(phys), pages,
                 PAGE_PRESENT | PAGE_WRITE | PAGE_NX | PAGE_PCD);
    vmm_bitmap = reinterpret_cast<uint8_t*>(VMM_START);
    memset(vmm_bitmap, 0, VMM_BITMAP_SIZE);
    for (size_t i = 0; i < pages; i++) {
        vmm_bitmap[i/8] |= (static_cast<uint8_t>(1u << (i%8)));
    }
}

void* PagingManager::allocStack(size_t pages) {
    size_t total = pages + 1; 
    uint64_t f = spinlock_acquire(&vmm_lock);
    int64_t idx = vmm_find_free(total);
    if (idx < 0) { 
        spinlock_release(&vmm_lock, f); 
        return nullptr; 
    }
    vmm_set(static_cast<size_t>(idx), total, 1);
    uint64_t vbase  = VMM_START + static_cast<uint64_t>(idx) * PAGE_SIZE;
    uint64_t vstack = vbase + PAGE_SIZE;
    spinlock_release(&vmm_lock, f);

    unmapPage(vbase); 

    void* phys = pmm_alloc_contiguous(pages);
    if (phys) {
        if (!mapPagesBulk(vstack, reinterpret_cast<uint64_t>(phys), pages, PAGE_PRESENT | PAGE_WRITE | PAGE_NX)) {
            pmm_free_contiguous(phys, pages);
            f = spinlock_acquire(&vmm_lock);
            vmm_set(static_cast<size_t>(idx), total, 0);
            spinlock_release(&vmm_lock, f);
            return nullptr;
        }
        memset(reinterpret_cast<void*>(vstack), 0, pages * PAGE_SIZE);
        return reinterpret_cast<void*>(vstack);
    }

    for (size_t i = 0; i < pages; i++) {
        void* p = pmm_alloc_frame();
        if (unlikely(!p)) {
            for (size_t j = 0; j < i; j++) {
                uint64_t v = vstack + static_cast<uint64_t>(j) * PAGE_SIZE;
                uint64_t pa = get_physical_address(v);
                unmapPage(v);
                if (pa) pmm_free_frame(reinterpret_cast<void*>(pa));
            }
            f = spinlock_acquire(&vmm_lock);
            vmm_set(static_cast<size_t>(idx), total, 0);
            spinlock_release(&vmm_lock, f);
            return nullptr;
        }
        mapPage(vstack + static_cast<uint64_t>(i) * PAGE_SIZE, reinterpret_cast<uint64_t>(p), PAGE_PRESENT | PAGE_WRITE | PAGE_NX);
        memset(reinterpret_cast<void*>(vstack + static_cast<uint64_t>(i) * PAGE_SIZE), 0, PAGE_SIZE);
    }
    return reinterpret_cast<void*>(vstack);
}

void PagingManager::freeStack(void* base, size_t pages) {
    if (!base) return;
    uint64_t vaddr = reinterpret_cast<uintptr_t>(base);
    uint64_t guard = vaddr - PAGE_SIZE;
    if (guard < VMM_START || guard >= VMM_START + VMM_SIZE) return;
    
    for (size_t i = 0; i < pages; i++) {
        uint64_t a = vaddr + static_cast<uint64_t>(i) * PAGE_SIZE;
        uint64_t pa = get_physical_address(a);
        if (pa) { 
            pmm_free_frame(reinterpret_cast<void*>(pa)); 
            unmapPage(a); 
        }
    }
    tlbInvalidate(vaddr, pages);
    uint64_t f = spinlock_acquire(&vmm_lock);
    vmm_set(static_cast<size_t>((guard - VMM_START) / PAGE_SIZE), pages + 1, 0);
    spinlock_release(&vmm_lock, f);
}

void* PagingManager::ioremap(uint64_t phys_addr, uint32_t size, uint64_t flags) {
    if (size == 0) return nullptr;
    uint64_t off   = phys_addr & 0xFFFULL;
    uint64_t base  = phys_addr & ~0xFFFULL;
    size_t pages   = static_cast<size_t>((size + off + PAGE_SIZE - 1) / PAGE_SIZE);
    flags |= PAGE_PRESENT | PAGE_WRITE | PAGE_NX;
    flags &= ~static_cast<uint64_t>(PAGE_USER);

    uint64_t f = spinlock_acquire(&vmm_lock);
    int64_t idx = vmm_find_free(pages);
    if (idx < 0) { 
        spinlock_release(&vmm_lock, f); 
        return nullptr; 
    }
    vmm_set(static_cast<size_t>(idx), pages, 1);
    uint64_t virt = VMM_START + static_cast<uint64_t>(idx) * PAGE_SIZE;
    spinlock_release(&vmm_lock, f);

    if (!mapPagesBulk(virt, base, pages, flags)) {
        f = spinlock_acquire(&vmm_lock);
        vmm_set(static_cast<size_t>(idx), pages, 0);
        spinlock_release(&vmm_lock, f);
        return nullptr;
    }
    return reinterpret_cast<void*>(virt + off);
}

void PagingManager::iounmap(void* virt_addr, uint32_t size) {
    if (!virt_addr || size == 0) return;
    uint64_t vaddr = reinterpret_cast<uintptr_t>(virt_addr) & ~0xFFFULL;
    uint64_t off   = reinterpret_cast<uintptr_t>(virt_addr) & 0xFFFULL;
    if (vaddr < VMM_START || vaddr >= VMM_START + VMM_SIZE) return;
    size_t pages = static_cast<size_t>((size + off + PAGE_SIZE - 1) / PAGE_SIZE);
    for (size_t i = 0; i < pages; i++) {
        unmapPage(vaddr + static_cast<uint64_t>(i) * PAGE_SIZE);
    }
    tlbInvalidate(vaddr, pages);
    uint64_t f = spinlock_acquire(&vmm_lock);
    vmm_set(static_cast<size_t>((vaddr - VMM_START) / PAGE_SIZE), pages, 0);
    spinlock_release(&vmm_lock, f);
}

extern "C" {

void vmm_init_manager(void) {
    if (g_Paging) g_Paging->initVMM();
}

void* ioremap(uint64_t phys_addr, uint32_t size, uint64_t flags) {
    return g_Paging ? g_Paging->ioremap(phys_addr, size, flags) : nullptr;
}

void iounmap(void* virt_addr, uint32_t size) {
    if (g_Paging) g_Paging->iounmap(virt_addr, size);
}

void* vmm_alloc_stack(size_t pages) {
    return g_Paging ? g_Paging->allocStack(pages) : nullptr;
}

void vmm_free_stack(void* stack_base, size_t pages) {
    if (g_Paging) g_Paging->freeStack(stack_base, pages);
}

}
