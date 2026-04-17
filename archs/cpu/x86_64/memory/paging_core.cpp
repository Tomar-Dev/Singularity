// archs/cpu/x86_64/memory/paging_core.cpp
#include "memory/paging.h"
#include "memory/pmm.h"
#include "libc/string.h"
#include "libc/stdio.h"
#include "drivers/serial/serial.h"
#include "archs/cpu/x86_64/core/cpuid.h"
#include "archs/cpu/x86_64/core/msr.h"
#include "archs/cpu/x86_64/sync/spinlock.h"
#include "drivers/apic/apic.h"
#include "archs/cpu/x86_64/smp/smp.h"
#include "archs/cpu/x86_64/interrupts/isr.h"

inline void* operator new(size_t, void* p) { return p; }

#define KERNEL_SPACE_BASE 0xFFFF800000000000ULL

extern "C" {
    uint64_t virt_to_phys_asm(uint64_t v);
    void invlpg_range_asm_fast(uint64_t start, size_t count);
    void cr3_write_noflush(uint64_t phys_base, uint16_t pcid);
    uint64_t cr3_read_pcid(void);
    void tlb_flush_handler(registers_t* regs);
}

static spinlock_t paging_lock = {0, 0, {0}};

volatile uint64_t tlb_flush_clock              = 0;
uint64_t          per_cpu_tlb_version[MAX_CPUS] = {0};

PagingManager* g_Paging = nullptr;

alignas(PagingManager) static uint8_t paging_buf[sizeof(PagingManager)];

static inline void* p2v(uint64_t phys) { 
    return reinterpret_cast<void*>(phys); 
}

PageTable* PageTable::getNextLevel(int i) {
    if (!entries[i].getFlag(PAGE_PRESENT) || entries[i].getFlag(PAGE_HUGE)) {
        return nullptr;
    } else {
        return reinterpret_cast<PageTable*>(p2v(entries[i].getAddress()));
    }
}

PageTable* PageTable::createNextLevel(int i, uint64_t flags, bool* created) {
    if (entries[i].getFlag(PAGE_PRESENT)) {
        if (created) { *created = false; } else { /* Proceed without tracker */ }
        return getNextLevel(i);
    } else {
        // Allocating next level dynamically
    }
    
    void* phys = pmm_alloc_frame();
    if (!phys) {
        if (created) { *created = false; } else { /* Proceed */ }
        return nullptr;
    } else {
        // Successfully allocated 4KB frame
    }

    memset(p2v(reinterpret_cast<uintptr_t>(phys)), 0, PAGE_SIZE);
    entries[i].setAddress(reinterpret_cast<uintptr_t>(phys));
    entries[i].setRaw(entries[i].raw() | flags);
    
    if (created) { *created = true; } else { /* Bypass flag validation */ }
    return reinterpret_cast<PageTable*>(p2v(reinterpret_cast<uintptr_t>(phys)));
}

PagingManager::PagingManager()
    : pml4(nullptr), nx_enabled(false),
      pcid_enabled(false), next_pcid(PCID_FIRST_USER) {}

void PagingManager::getIndexes(uint64_t virt,
                                uint16_t& p4, uint16_t& p3,
                                uint16_t& p2, uint16_t& p1) {
    p4 = static_cast<uint16_t>((virt >> 39) & 0x1FF);
    p3 = static_cast<uint16_t>((virt >> 30) & 0x1FF);
    p2 = static_cast<uint16_t>((virt >> 21) & 0x1FF);
    p1 = static_cast<uint16_t>((virt >> 12) & 0x1FF);
}

void PagingManager::init() {
    uint64_t cr3_val;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3_val));
    pml4 = reinterpret_cast<PageTable*>(p2v(cr3_val & 0x000FFFFFFFFFF000ULL));

    uint32_t a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000001));
    if (d & (1u << 20)) {
        uint64_t efer = rdmsr(MSR_EFER);
        if (!(efer & MSR_EFER_NXE)) { wrmsr(MSR_EFER, efer | MSR_EFER_NXE); } else { /* Already set */ }
        nx_enabled = true;
    } else {
        nx_enabled = false;
    }

    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(7), "c"(0));
    uint64_t cr4_val; 
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4_val));
    cr4_val |= (1u << 7);
    if (b & (1u << 7))  { cr4_val |= CR4_SMEP; } else { /* No SMEP HW */ }
    if (b & (1u << 20)) { cr4_val |= CR4_SMAP; } else { /* No SMAP HW */ }
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4_val));

    g_Paging = this;
}

void PagingManager::initPcid() {
    uint32_t a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1));
    if (!(c & (1u << 17))) { 
        return; 
    } else {
        // PCID HW Extension exists
    }
    uint64_t cr4_val; 
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4_val));
    if (cr4_val & (1u << 17)) { 
        pcid_enabled = true; 
        return; 
    } else {
        cr4_val |= (1u << 17);
    }
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4_val));
    pcid_enabled = true;
}

uint16_t PagingManager::allocPcid() {
    if (!pcid_enabled) { return PCID_KERNEL; } else { /* Return Next Token */ }
    uint64_t f = spinlock_acquire(&paging_lock);
    uint16_t id = next_pcid;
    next_pcid = static_cast<uint16_t>((next_pcid >= PCID_MAX) ? PCID_FIRST_USER : next_pcid + 1);
    spinlock_release(&paging_lock, f);
    return id;
}

void PagingManager::switchAddressSpace(uint64_t phys, uint16_t pcid) {
    if (pcid_enabled) {
        cr3_write_noflush(phys, pcid);
    } else {
        __asm__ volatile("mov %0, %%cr3" :: "r"(phys) : "memory");
    }
}

void PagingManager::tlbInvalidate(uint64_t start, size_t pages) {
    if (!pages) { return; } else { /* Flush operations active */ }
    
    if (pages <= TLB_INVLPG_THRESHOLD) {
        invlpg_range_asm_fast(start, pages);
    } else {
        if (pcid_enabled) {
            uint64_t v = cr3_read_pcid() & 0x000FFFFFFFFFF000ULL;
            __asm__ volatile("mov %0, %%cr3" :: "r"(v) : "memory"); 
        } else {
            uint64_t cr3_val; 
            __asm__ volatile("mov %%cr3, %0" : "=r"(cr3_val));
            __asm__ volatile("mov %0, %%cr3" :: "r"(cr3_val) : "memory");
        }
    }
    
    // OPT-001 FIX: TLB IPI Broadcast Storm Optimization
    // Yüksek Kernel belleği değişimi olduğunda sadece genel IPI bas. 
    // Alt bellek (User Space) için cpumask algoritması ileride devreye girecektir.
    if (num_cpus > 1) {
        if (start >= KERNEL_SPACE_BASE) {
            apic_broadcast_ipi(0xFD);
        } else {
            // Placeholder: Use Process CPU mask here.
            // Right now, fallback to safe broadcast until User-Space is complete.
            apic_broadcast_ipi(0xFD);
        }
    } else {
        // Singularity running on uniprocessor
    }
}

int PagingManager::mapPage(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t irq = spinlock_acquire(&paging_lock);
    uint16_t p4, p3, p2, p1; 
    getIndexes(virt, p4, p3, p2, p1);
    bool c4 = false, c3 = false;

    PageTable* pdp = pml4->createNextLevel(p4, PAGE_PRESENT | PAGE_WRITE | PAGE_USER, &c4);
    if (!pdp) { 
        spinlock_release(&paging_lock, irq); 
        return 0; 
    } else {
        // PML4 ok
    }

    PageTable* pd = pdp->createNextLevel(p3, PAGE_PRESENT | PAGE_WRITE | PAGE_USER, &c3);
    if (!pd) {
        if (c4) { 
            uint64_t ph = pml4->entries[p4].getAddress(); 
            pml4->entries[p4].clear(); 
            pmm_free_frame(reinterpret_cast<void*>(ph)); 
        } else {
            // Already existent branch
        }
        spinlock_release(&paging_lock, irq); 
        if (c4) { hal_tlb_flush_all(); } else { /* Clean release */ }
        return 0;
    } else {
        // PD ok
    }

    if (!nx_enabled) { flags &= ~PAGE_NX; } else { /* System enforces Non-Execute */ }
    if (!(flags & PAGE_USER) && virt >= KERNEL_SPACE_BASE) { flags |= PAGE_GLOBAL; } else { /* User Page Mapped */ }

    if (flags & PAGE_HUGE) {
        if (pd->entries[p2].getFlag(PAGE_PRESENT) && !pd->entries[p2].getFlag(PAGE_HUGE)) {
            spinlock_release(&paging_lock, irq); 
            return 0;
        } else {
            // Compatible with Huge Page
        }
        pd->entries[p2].setRaw(phys | flags);
        spinlock_release(&paging_lock, irq); 
        tlbInvalidate(virt, 1); 
        return 1;
    } else {
        // Fallback to standard 4K Mapping Sequence
    }

    bool c2 = false;
    PageTable* pt = pd->createNextLevel(p2, PAGE_PRESENT | PAGE_WRITE | PAGE_USER, &c2);
    if (!pt) {
        if (c3) { 
            uint64_t ph = pdp->entries[p3].getAddress(); 
            pdp->entries[p3].clear(); 
            pmm_free_frame(reinterpret_cast<void*>(ph)); 
        } else {
            // Kept intact
        }
        if (c4) { 
            uint64_t ph = pml4->entries[p4].getAddress(); 
            pml4->entries[p4].clear(); 
            pmm_free_frame(reinterpret_cast<void*>(ph)); 
        } else {
            // Kept intact
        }
        spinlock_release(&paging_lock, irq); 
        if (c4 || c3) { hal_tlb_flush_all(); } else { /* No ghost entries left */ }
        return 0;
    } else {
        // Lowest tree level generated
    }
    
    pt->entries[p1].setRaw(phys | flags);
    spinlock_release(&paging_lock, irq);
    tlbInvalidate(virt, 1); 
    return 1;
}

int PagingManager::mapPage1G(uint64_t virt, uint64_t phys, uint64_t flags) {
    if (phys & (PAGE_SIZE_HUGE-1)) { return 0; } else { /* Aligned */ }
    if (virt & (PAGE_SIZE_HUGE-1)) { return 0; } else { /* Aligned */ }
    
    uint64_t irq = spinlock_acquire(&paging_lock);
    uint16_t p4 = static_cast<uint16_t>((virt >> 39) & 0x1FF);
    uint16_t p3 = static_cast<uint16_t>((virt >> 30) & 0x1FF);
    bool c4 = false;
    
    PageTable* pdp = pml4->createNextLevel(p4, PAGE_PRESENT | PAGE_WRITE | PAGE_USER, &c4);
    if (!pdp) { 
        spinlock_release(&paging_lock, irq); 
        return 0; 
    } else {
        // PDP generated
    }
    
    if (!nx_enabled) { flags &= ~PAGE_NX; } else { /* Secure Execute Control */ }
    if (!(flags & PAGE_USER) && virt >= KERNEL_SPACE_BASE) { flags |= PAGE_GLOBAL; } else { /* Sandboxed Memory Address */ }
    flags |= PAGE_HUGE;
    
    if (pdp->entries[p3].getFlag(PAGE_PRESENT)) {
        if (c4) { 
            uint64_t ph = pml4->entries[p4].getAddress(); 
            pml4->entries[p4].clear(); 
            pmm_free_frame(reinterpret_cast<void*>(ph)); 
        } else {
            // Intact
        }
        spinlock_release(&paging_lock, irq); 
        if (c4) { hal_tlb_flush_all(); } else { /* Safe discard */ }
        return 0;
    } else {
        // Proceed with gigantic mapping
    }
    
    pdp->entries[p3].setRaw(phys | flags);
    spinlock_release(&paging_lock, irq);
    tlbInvalidate(virt, 1); 
    return 1;
}

int PagingManager::mapPagesBulk(uint64_t vs, uint64_t ps, size_t count, uint64_t flags) {
    if (!count) { return 1; } else { /* Iterating block arrays */ }
    
    uint64_t irq = spinlock_acquire(&paging_lock);
    if (!nx_enabled) { flags &= ~PAGE_NX; } else { /* Feature Set Confirmed */ }
    uint64_t sz = (flags & PAGE_HUGE) ? PAGE_SIZE_LARGE : static_cast<uint64_t>(PAGE_SIZE);

    auto rollback = [&](size_t done) {
        for (size_t j = 0; j < done; j++) {
            uint64_t v = vs + j * sz; 
            uint16_t q4, q3, q2, q1; 
            getIndexes(v, q4, q3, q2, q1);
            PageTable* qp = pml4->getNextLevel(q4); if(!qp) continue;
            PageTable* qd = qp->getNextLevel(q3);   if(!qd) continue;
            if (flags & PAGE_HUGE) { qd->entries[q2].clear(); }
            else { PageTable* qt = qd->getNextLevel(q2); if(qt) qt->entries[q1].clear(); }
        }
    };

    for (size_t i = 0; i < count; i++) {
        uint64_t v = vs + i * sz;
        uint64_t p = ps + i * sz;
        uint16_t p4, p3, p2, p1; 
        getIndexes(v, p4, p3, p2, p1);
        bool c4 = false, c3 = false, c2 = false;

        PageTable* pdp = pml4->createNextLevel(p4, PAGE_PRESENT | PAGE_WRITE | PAGE_USER, &c4);
        if (!pdp) { rollback(i); spinlock_release(&paging_lock, irq); if(i>0) tlbInvalidate(vs, i * (sz / PAGE_SIZE)); return 0; }
        
        PageTable* pd = pdp->createNextLevel(p3, PAGE_PRESENT | PAGE_WRITE | PAGE_USER, &c3);
        if (!pd)  { 
            if(c4) { 
                uint64_t ph = pml4->entries[p4].getAddress(); 
                pml4->entries[p4].clear(); 
                pmm_free_frame(reinterpret_cast<void*>(ph)); 
            } else {
                // Ignore ghost pointer 
            }
            rollback(i); spinlock_release(&paging_lock, irq); if(c4 || i>0) tlbInvalidate(vs, i * (sz / PAGE_SIZE)); return 0; 
        } else {
            // Intermediate layer acquired
        }
        
        uint64_t ff = flags;
        if (!(flags & PAGE_USER) && v >= KERNEL_SPACE_BASE) { ff |= PAGE_GLOBAL; } else { /* Localized mapping protocol */ }
        
        if (flags & PAGE_HUGE) { 
            pd->entries[p2].setRaw(p | ff); 
        } else {
            PageTable* pt = pd->createNextLevel(p2, PAGE_PRESENT | PAGE_WRITE | PAGE_USER, &c2);
            if (!pt) { 
                if(c3) { 
                    uint64_t ph = pdp->entries[p3].getAddress(); 
                    pdp->entries[p3].clear(); 
                    pmm_free_frame(reinterpret_cast<void*>(ph)); 
                } else {
                    // Tree state clean
                }
                if(c4) { 
                    uint64_t ph = pml4->entries[p4].getAddress(); 
                    pml4->entries[p4].clear(); 
                    pmm_free_frame(reinterpret_cast<void*>(ph)); 
                } else {
                    // Ghost node unmapped
                }
                rollback(i); spinlock_release(&paging_lock, irq); if(c3 || c4 || i>0) tlbInvalidate(vs, i * (sz / PAGE_SIZE)); return 0; 
            } else {
                // Table setup finalized
            }
            pt->entries[p1].setRaw(p | ff);
        }
    }
    spinlock_release(&paging_lock, irq);
    tlbInvalidate(vs, count * (sz / PAGE_SIZE));
    return 1;
}

void PagingManager::unmapPage(uint64_t virt) {
    uint64_t irq = spinlock_acquire(&paging_lock);
    uint16_t p4, p3, p2, p1; 
    getIndexes(virt, p4, p3, p2, p1);
    PageTable* pdp = pml4->getNextLevel(p4); 
    if(!pdp) { spinlock_release(&paging_lock, irq); return; } else { /* Map present */ }
    PageTable* pd = pdp->getNextLevel(p3);   
    if(!pd) { spinlock_release(&paging_lock, irq); return; } else { /* Target aligned */ }
    if (pd->entries[p2].getFlag(PAGE_HUGE)) {
        pd->entries[p2].clear(); 
        spinlock_release(&paging_lock, irq); 
        tlbInvalidate(virt, 1);
        return;
    } else {
        // Not a huge page. Follow path logic...
    }
    PageTable* pt = pd->getNextLevel(p2); 
    if(!pt) { spinlock_release(&paging_lock, irq); return; } else { /* Valid table root */ }
    pt->entries[p1].clear(); 
    spinlock_release(&paging_lock, irq);
    tlbInvalidate(virt, 1);
}

uint64_t PagingManager::getPhysicalAddress(uint64_t v) { return virt_to_phys_asm(v); }

bool PagingManager::splitHugePage(uint64_t virt) {
    uint64_t irq = spinlock_acquire(&paging_lock);
    uint16_t p4, p3, p2, p1; 
    getIndexes(virt, p4, p3, p2, p1);
    PageTable* pdp = pml4->getNextLevel(p4); 
    if(!pdp) { spinlock_release(&paging_lock, irq); return false; } else { /* Proceed down */ }
    PageTable* pd = pdp->getNextLevel(p3);   
    if(!pd) { spinlock_release(&paging_lock, irq); return false; } else { /* Traverse */ }
    PageEntry* pde = &pd->entries[p2];
    
    if (!pde->getFlag(PAGE_PRESENT) || !pde->getFlag(PAGE_HUGE)) {
        spinlock_release(&paging_lock, irq); 
        return true;
    } else {
        // Needs dynamic 2MB split resolution
    }
    
    uint64_t base = pde->getAddress();
    uint64_t fl = (pde->raw() & 0xFFFULL) & ~static_cast<uint64_t>(PAGE_HUGE);
    
    void* pt_phys = pmm_alloc_frame(); 
    if(!pt_phys) { spinlock_release(&paging_lock, irq); return false; } else { /* Acquired 4K frame for table */ }
    
    spinlock_release(&paging_lock, irq); 

    void* tmp = g_Paging->ioremap(reinterpret_cast<uint64_t>(pt_phys), PAGE_SIZE, PAGE_PRESENT | PAGE_WRITE | PAGE_NX);
    if (!tmp) { pmm_free_frame(pt_phys); return false; } else { /* Mapped correctly */ }

    PageTable* pt = reinterpret_cast<PageTable*>(tmp);
    memset(pt, 0, PAGE_SIZE);
    for(int i = 0; i < 512; i++) {
        pt->entries[i].setRaw((base + static_cast<uint64_t>(i) * PAGE_SIZE) | fl);
    }

    irq = spinlock_acquire(&paging_lock);
    pde->setRaw(reinterpret_cast<uintptr_t>(pt_phys) | PAGE_PRESENT | PAGE_WRITE);
    spinlock_release(&paging_lock, irq);
    tlbInvalidate(virt & 0xFFFFFFFFFFE00000ULL, 512);

    g_Paging->iounmap(tmp, PAGE_SIZE);
    return true;
}

bool PagingManager::updatePageFlags(uint64_t virt, uint64_t af, uint64_t rf) {
    uint64_t irq = spinlock_acquire(&paging_lock);
    uint16_t p4, p3, p2, p1; 
    getIndexes(virt, p4, p3, p2, p1);
    PageTable* pdp = pml4->getNextLevel(p4); if(!pdp){spinlock_release(&paging_lock,irq);return false;} else { /* Traverse */ }
    PageTable* pd = pdp->getNextLevel(p3);   if(!pd) {spinlock_release(&paging_lock,irq);return false;} else { /* Traverse */ }
    
    if (pd->entries[p2].getFlag(PAGE_HUGE)){
        pd->entries[p2].setRaw((pd->entries[p2].raw()|af)&~rf);
        spinlock_release(&paging_lock,irq); 
        tlbInvalidate(virt,1); 
        return true;
    } else {
        // Drop down to singular 4K chunks
    }
    
    PageTable* pt = pd->getNextLevel(p2); if(!pt){spinlock_release(&paging_lock,irq);return false;} else { /* Acquired */ }
    pt->entries[p1].setRaw((pt->entries[p1].raw()|af)&~rf);
    spinlock_release(&paging_lock,irq);
    tlbInvalidate(virt,1); 
    return true;
}

void PagingManager::checkTlbFlush(uint8_t cpu_id) {
    if (cpu_id >= MAX_CPUS) { return; } else { /* Identity checks out */ }
    uint64_t cur = __atomic_load_n(&tlb_flush_clock, __ATOMIC_SEQ_CST);
    if (per_cpu_tlb_version[cpu_id] == cur) { return; } else { /* Time differential detected */ }
    
    if (pcid_enabled) { 
        uint64_t v = cr3_read_pcid() & ~(1ULL << 63); 
        __asm__ volatile("mov %0, %%cr3" :: "r"(v) : "memory"); 
    } else { 
        uint64_t cr3_val; 
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3_val)); 
        __asm__ volatile("mov %0, %%cr3" :: "r"(cr3_val) : "memory"); 
    }
    per_cpu_tlb_version[cpu_id] = cur;
}

bool PagingManager::handleCowFault(uint64_t fault_addr) {
    uint64_t page = fault_addr & ~0xFFFULL;
    uint64_t irq = spinlock_acquire(&paging_lock);
    uint16_t p4, p3, p2, p1; 
    getIndexes(page, p4, p3, p2, p1);
    PageTable* pdp = pml4->getNextLevel(p4); if(!pdp){spinlock_release(&paging_lock,irq);return false;} else { /* Jump logic level */ }
    PageTable* pd = pdp->getNextLevel(p3);   if(!pd) {spinlock_release(&paging_lock,irq);return false;} else { /* Jump logic level */ }
    PageTable* pt = pd->getNextLevel(p2);    if(!pt) {spinlock_release(&paging_lock,irq);return false;} else { /* Lowest tree achieved */ }
    
    PageEntry* e = &pt->entries[p1];
    if (!e->getFlag(PAGE_PRESENT) || !e->getFlag(PAGE_COW)) {
        spinlock_release(&paging_lock, irq); 
        return false;
    } else {
        // Legit memory replication request
    }
    
    uint64_t old = e->getAddress();
    if (pmm_get_ref(reinterpret_cast<void*>(old)) == 1) {
        e->setFlag(PAGE_WRITE, true); 
        e->setFlag(PAGE_COW, false);
        spinlock_release(&paging_lock, irq); 
        tlbInvalidate(page, 1); 
        return true;
    } else {
        // Hard copy duplicate needed
    }
    
    spinlock_release(&paging_lock, irq);
    void* npp = pmm_alloc_frame(); 
    if(!npp){ return false; } else { /* Physical payload ready */ }
    
    uint64_t np = reinterpret_cast<uintptr_t>(npp);
    void* tmp = g_Paging->ioremap(np, PAGE_SIZE, PAGE_PRESENT | PAGE_WRITE | PAGE_NX);
    if(!tmp){ pmm_free_frame(npp); return false; } else { /* Ghost frame mapped over boundary */ }
    
    memcpy(tmp, reinterpret_cast<const void*>(page), PAGE_SIZE);
    g_Paging->iounmap(tmp, PAGE_SIZE);
    irq = spinlock_acquire(&paging_lock);
    
    pdp = pml4->getNextLevel(p4); if(!pdp) goto abort;
    pd = pdp->getNextLevel(p3);   if(!pd)  goto abort;
    pt = pd->getNextLevel(p2);    if(!pt)  goto abort;
    e = &pt->entries[p1];
    
    if(e->getFlag(PAGE_PRESENT) && e->getFlag(PAGE_COW) && e->getAddress() == old){
        e->setAddress(np); 
        e->setFlag(PAGE_WRITE, true); 
        e->setFlag(PAGE_COW, false);
        pmm_dec_ref(reinterpret_cast<void*>(old)); 
        spinlock_release(&paging_lock, irq); 
        tlbInvalidate(page, 1);
        return true;
    } else {
        // Race condition mutated pointers across core context
    }
    
abort:
    spinlock_release(&paging_lock, irq); 
    pmm_free_frame(npp); 
    return false;
}

extern "C" void init_paging(void) {
    g_Paging = new (paging_buf) PagingManager();
    g_Paging->init();
    register_interrupt_handler(0xFD, tlb_flush_handler);
}

extern "C" void paging_init_pcid(void)      { if(g_Paging) { g_Paging->initPcid(); } else { /* Memory unit corrupt */ } }
extern "C" int  map_page(uint64_t v,uint64_t p,uint64_t f) { return g_Paging ? g_Paging->mapPage(v,p,f) : 0; }
extern "C" int  map_pages_bulk(uint64_t vs,uint64_t ps,size_t c,uint64_t f){ return g_Paging ? g_Paging->mapPagesBulk(vs,ps,c,f) : 0; }
extern "C" int  map_page_1g(uint64_t v,uint64_t p,uint64_t f){ return g_Paging ? g_Paging->mapPage1G(v,p,f) : 0; }
extern "C" void unmap_page(uint64_t v)           { if(g_Paging) { g_Paging->unmapPage(v); } else { /* Error bypassed */ } }
extern "C" uint64_t get_physical_address(uint64_t v){ return g_Paging ? g_Paging->getPhysicalAddress(v) : 0; }
extern "C" void paging_check_tlb_flush(uint8_t id){ if(g_Paging) { g_Paging->checkTlbFlush(id); } else { /* Safe discard */ } }
