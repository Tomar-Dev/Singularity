// archs/cpu/x86_64/memory/paging.h
#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include <stddef.h>
#include "archs/cpu/x86_64/interrupts/isr.h"
// FIX: PAGE_SIZE is defined as plain 4096 (integer, no ULL suffix) so that
#ifndef PAGE_SIZE
#define PAGE_SIZE        4096
#endif

#define PAGE_SIZE_LARGE  0x200000ULL
#define PAGE_SIZE_HUGE   0x40000000ULL

#define PAGE_PRESENT   0x01ULL
#define PAGE_WRITE     0x02ULL
#define PAGE_USER      0x04ULL
#define PAGE_PWT       0x08ULL
#define PAGE_PCD       0x10ULL
#define PAGE_ACCESSED  0x20ULL
#define PAGE_DIRTY     0x40ULL
#define PAGE_HUGE      0x80ULL
#define PAGE_GLOBAL    0x100ULL
#define PAGE_COW       0x200ULL
#define PAGE_NX        (1ULL << 63)

#define PAGE_WRITE_COMBINE PAGE_PWT

#define PCID_KERNEL     0
#define PCID_FIRST_USER 1
#define PCID_MAX        4095

#define TLB_INVLPG_THRESHOLD 32

#ifdef __cplusplus

class PageEntry {
private:
    uint64_t value;

public:
    void setAddress(uint64_t physAddr) {
        value = (value & 0xFFF0000000000FFFULL)
              | (physAddr & 0x000FFFFFFFFFF000ULL);
    }

    uint64_t getAddress() const {
        return value & 0x000FFFFFFFFFF000ULL;
    }

    void setFlag(uint64_t flag, bool enable) {
        if (enable) {
            value |= flag;
        } else {
            value &= ~flag;
        }
    }

    bool getFlag(uint64_t flag) const {
        return (value & flag) != 0;
    }

    void     clear()            { value = 0; }
    uint64_t raw()        const { return value; }
    void     setRaw(uint64_t v) { value = v; }
};

class PageTable {
public:
    PageEntry entries[512];

    PageTable* getNextLevel(int index);
    PageTable* createNextLevel(int index, uint64_t flags,
                               bool* created = nullptr);
};

class PagingManager {
private:
    PageTable* pml4;
    bool       nx_enabled;
    bool       pcid_enabled;
    uint16_t   next_pcid;

    void getIndexes(uint64_t virt,
                    uint16_t& p4, uint16_t& p3,
                    uint16_t& p2, uint16_t& p1);

    void tlbInvalidate(uint64_t virt_start, size_t page_count);

public:
    PagingManager();
    void init();

    void initPcid();

    uint16_t allocPcid();

    void switchAddressSpace(uint64_t pml4_phys, uint16_t pcid);

    int mapPage(uint64_t virt, uint64_t phys, uint64_t flags);

    int mapPagesBulk(uint64_t virt_start, uint64_t phys_start,
                     size_t count, uint64_t flags);

    int mapPage1G(uint64_t virt, uint64_t phys, uint64_t flags);

    void     unmapPage(uint64_t virt);
    uint64_t getPhysicalAddress(uint64_t virt);

    bool splitHugePage(uint64_t virt);
    bool updatePageFlags(uint64_t virt, uint64_t add_flags,
                         uint64_t remove_flags);

    void  initVMM();
    void* allocStack(size_t pages);
    void  freeStack(void* base, size_t pages);
    void* ioremap(uint64_t phys, uint32_t size, uint64_t flags);
    void  iounmap(void* virt, uint32_t size);

    void checkTlbFlush(uint8_t cpu_id);
    bool handleCowFault(uint64_t fault_addr);

    bool isPcidEnabled() const { return pcid_enabled; }
};

extern PagingManager* g_Paging;

#endif

#ifdef __cplusplus
extern "C" {
#endif

void     init_paging(void);
void     paging_init_pcid(void);
void     paging_enable_write_protect(void);
void     paging_protect_kernel(void);

int      map_page(uint64_t virt, uint64_t phys, uint64_t flags);
int      map_pages_bulk(uint64_t virt_start, uint64_t phys_start,
                        size_t count, uint64_t flags);
int      map_page_1g(uint64_t virt, uint64_t phys, uint64_t flags);

void     unmap_page(uint64_t virt);
uint64_t get_physical_address(uint64_t virt);

void     page_fault_handler(registers_t* regs);

void     vmm_init_manager(void);
void*    ioremap(uint64_t phys_addr, uint32_t size, uint64_t flags);
void     iounmap(void* virt_addr, uint32_t size);
void*    vmm_alloc_stack(size_t pages);
void     vmm_free_stack(void* stack_base, size_t pages);
void     paging_check_tlb_flush(uint8_t cpu_id);

void     invlpg_range_asm_fast(uint64_t start, size_t count);
uint64_t virt_to_phys_asm(uint64_t virt);
void     cr3_write_noflush(uint64_t phys_base, uint16_t pcid);
uint64_t cr3_read_pcid(void);

#ifdef __cplusplus
}
#endif

#endif
