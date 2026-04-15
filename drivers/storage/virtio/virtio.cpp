// drivers/storage/virtio/virtio.cpp
#include "drivers/storage/virtio/virtio.hpp"
#include "archs/cpu/x86_64/core/ports.h"
#include "memory/kheap.h"
#include "memory/paging.h"
#include "memory/pmm.h"
#include "libc/string.h"
#include "libc/stdio.h"
#include "drivers/serial/serial.h"
#include "system/disk/gpt.h"
#include "archs/storage/storage_hal.h" 
#include "drivers/timer/tsc.h"

extern "C" void print_status(const char* prefix, const char* msg, const char* status);
extern "C" void yield();
extern "C" void kfree_contiguous(void* ptr, size_t size);
extern "C" void* kmalloc_contiguous(size_t size);
extern "C" uint64_t get_physical_address(uint64_t virt);
extern "C" void* ioremap(uint64_t phys, uint32_t size, uint64_t flags);

extern "C" uint64_t tsc_read_asm();
extern "C" uint64_t get_tsc_freq();

#define ALIGN_UP(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

static inline void virtio_wmb() { __asm__ volatile("sfence" ::: "memory"); }
static inline void virtio_rmb() { __asm__ volatile("lfence" ::: "memory"); }

// YENİ: TSC Donanım Saati Tabanlı Kesin Timeout
static inline bool check_timeout(uint64_t start, uint32_t ms) {
    uint64_t freq = get_tsc_freq();
    if (freq == 0) freq = 2000000000ULL;
    uint64_t timeout_cycles = (freq / 1000) * ms;
    return (tsc_read_asm() - start) > timeout_cycles;
}

VirtIODriver::VirtIODriver(PCIeDevice* pci) 
    : Device("VirtIO Block", DEV_BLOCK), pciDev(pci) 
{
    io_base = 0;
    queue_virt = nullptr;
    free_head = 0;
    last_used_idx = 0;
    queue_size = 0;
    free_desc_count = 0; 
    is_modern = false;
    common_cfg = nullptr;
    isr_cfg = nullptr;
    spinlock_init(&io_lock); 
}

VirtIODriver::~VirtIODriver() {
    if (queue_virt) kfree_contiguous(queue_virt, 16384);
}

void VirtIODriver::mapCapability(uint8_t target_type, void** virt_addr, uint64_t* phys_addr, uint32_t* length) {
    uint8_t cap_offset = pciDev->readByte(0x34);
    
    while (cap_offset != 0) {
        uint8_t cap_id = pciDev->readByte(cap_offset);
        if (cap_id == 0x09) { 
            uint8_t type = pciDev->readByte(cap_offset + 3);
            if (type == target_type) {
                uint8_t bar_idx = pciDev->readByte(cap_offset + 4);
                uint32_t offset = pciDev->readDWord(cap_offset + 8);
                uint32_t len = pciDev->readDWord(cap_offset + 12);
                
                uint32_t bar_val = pciDev->getBAR(bar_idx);
                uint64_t bar_phys = bar_val & 0xFFFFFFF0;
                
                if ((bar_val & 0x4) == 0x4) {
                    uint32_t bar_high = pciDev->getBAR(bar_idx + 1);
                    bar_phys |= ((uint64_t)bar_high << 32);
                }
                
                uint64_t final_phys = bar_phys + offset;
                *phys_addr = final_phys;
                *length = len;
                *virt_addr = ioremap(final_phys, len, PAGE_PRESENT | PAGE_WRITE | PAGE_PCD);
                
                if (target_type == VIRTIO_PCI_CAP_NOTIFY_CFG) {
                    notify_off_multiplier = pciDev->readDWord(cap_offset + 16);
                }
                return;
            }
        }
        cap_offset = pciDev->readByte(cap_offset + 1);
    }
}

void VirtIODriver::write8(uint16_t offset, uint8_t val) {
    if (!is_modern) outb(io_base + offset, val);
}

void VirtIODriver::write16(uint16_t offset, uint16_t val) {
    if (!is_modern) outw(io_base + offset, val);
}

void VirtIODriver::write32(uint16_t offset, uint32_t val) {
    if (!is_modern) outl(io_base + offset, val);
}

uint8_t VirtIODriver::read8(uint16_t offset) {
    if (!is_modern) return inb(io_base + offset);
    return 0;
}

uint16_t VirtIODriver::read16(uint16_t offset) {
    if (!is_modern) return inw(io_base + offset);
    return 0;
}

uint32_t VirtIODriver::read32(uint16_t offset) {
    if (!is_modern) return inl(io_base + offset);
    return 0;
}

void VirtIODriver::notifyQueue(uint16_t queue_index) {
    if (is_modern) {
        uint16_t q_notify_off = common_cfg->queue_notify_off; 
        uint64_t offset = (uint64_t)q_notify_off * notify_off_multiplier;
        volatile uint16_t* notify_addr = (volatile uint16_t*)(notify_base + offset);
        *notify_addr = queue_index;
    } else {
        write16(VIRTIO_REG_QUEUE_NOTIFY, queue_index);
    }
}

int VirtIODriver::init() {
    pciDev->enableBusMaster();
    
    uint64_t common_phys = 0; uint32_t common_len = 0;
    mapCapability(VIRTIO_PCI_CAP_COMMON_CFG, (void**)&common_cfg, &common_phys, &common_len);
    
    uint64_t capacity_sectors = 0; 
    
    if (common_cfg) {
        is_modern = true;
        pciDev->enableMemorySpace();
        
        uint64_t isr_phys = 0; uint32_t isr_len = 0;
        mapCapability(VIRTIO_PCI_CAP_ISR_CFG, (void**)&isr_cfg, &isr_phys, &isr_len);
        
        uint64_t notify_phys = 0; uint32_t notify_len = 0;
        void* notify_virt = nullptr;
        mapCapability(VIRTIO_PCI_CAP_NOTIFY_CFG, &notify_virt, &notify_phys, &notify_len);
        notify_base = (uint64_t)notify_virt;
        
        uint64_t dev_cfg_phys = 0; uint32_t dev_cfg_len = 0;
        void* dev_cfg_virt = nullptr;
        mapCapability(VIRTIO_PCI_CAP_DEVICE_CFG, &dev_cfg_virt, &dev_cfg_phys, &dev_cfg_len);
        if (dev_cfg_virt) {
            uint8_t g1 = common_cfg->config_generation;
            capacity_sectors = *(volatile uint64_t*)dev_cfg_virt;
            uint8_t g2 = common_cfg->config_generation;
            if (g1 != g2) capacity_sectors = *(volatile uint64_t*)dev_cfg_virt;
        }
        
        common_cfg->device_status = 0;
        while(common_cfg->device_status != 0) __asm__ volatile("pause");
        
        common_cfg->device_status |= VIRTIO_STATUS_ACKNOWLEDGE;
        common_cfg->device_status |= VIRTIO_STATUS_DRIVER;
        common_cfg->device_status |= VIRTIO_STATUS_FEATURES_OK;
        
        common_cfg->queue_select = 0;
        queue_size = common_cfg->queue_size;
        
        if (queue_size == 0) return 0;
        
        uint32_t desc_size = 16 * queue_size;
        uint32_t avail_size = 6 + 2 * queue_size;
        uint32_t used_size = 6 + 8 * queue_size;
        uint32_t avail_offset = desc_size;
        uint32_t used_offset = ALIGN_UP(avail_offset + avail_size, 4096);
        uint32_t total_size = used_offset + ALIGN_UP(used_size, 4096);
        
        queue_virt = kmalloc_contiguous(total_size);
        if (!queue_virt) return 0;
        memset(queue_virt, 0, total_size);
        queue_phys = get_physical_address((uint64_t)queue_virt);
        
        desc = (struct virtq_desc*)queue_virt;
        avail = (struct virtq_avail*)((uint8_t*)queue_virt + avail_offset);
        used = (struct virtq_used*)((uint8_t*)queue_virt + used_offset);
        
        for (int i = 0; i < queue_size - 1; i++) desc[i].next = i + 1;
        desc[queue_size - 1].next = 0;
        free_head = 0;
        free_desc_count = queue_size; 
        
        common_cfg->queue_desc_lo = (uint32_t)queue_phys;
        common_cfg->queue_desc_hi = (uint32_t)(queue_phys >> 32);
        
        uint64_t avail_phys = queue_phys + avail_offset;
        common_cfg->queue_avail_lo = (uint32_t)avail_phys;
        common_cfg->queue_avail_hi = (uint32_t)(avail_phys >> 32);
        
        uint64_t used_phys = queue_phys + used_offset;
        common_cfg->queue_used_lo = (uint32_t)used_phys;
        common_cfg->queue_used_hi = (uint32_t)(used_phys >> 32);
        
        common_cfg->queue_enable = 1;
        common_cfg->device_status |= VIRTIO_STATUS_DRIVER_OK;
        
    } else {
        uint32_t bar0 = pciDev->getBAR(0);
        if ((bar0 & 1) == 0) return 0; 
        pciDev->enableIOSpace();
        io_base = bar0 & 0xFFFC;
        
        capacity_sectors = read32(VIRTIO_REG_DEVICE_CONFIG) | ((uint64_t)read32(VIRTIO_REG_DEVICE_CONFIG + 4) << 32);
        
        write8(VIRTIO_REG_DEVICE_STATUS, 0);
        write8(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
        write8(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
        
        write16(VIRTIO_REG_QUEUE_SELECT, 0);
        queue_size = read16(VIRTIO_REG_QUEUE_SIZE);
        
        if (queue_size == 0) return 0;
        
        uint32_t desc_size = 16 * queue_size;
        uint32_t avail_size = 6 + 2 * queue_size;
        uint32_t used_size = 6 + 8 * queue_size;
        uint32_t avail_offset = desc_size;
        uint32_t used_offset = ALIGN_UP(avail_offset + avail_size, 4096);
        uint32_t total_size = used_offset + ALIGN_UP(used_size, 4096);
        
        queue_virt = kmalloc_contiguous(total_size);
        if (!queue_virt) return 0;
        memset(queue_virt, 0, total_size);
        queue_phys = get_physical_address((uint64_t)queue_virt);
        
        desc = (struct virtq_desc*)queue_virt;
        avail = (struct virtq_avail*)((uint8_t*)queue_virt + avail_offset);
        used = (struct virtq_used*)((uint8_t*)queue_virt + used_offset);
        
        for (int i = 0; i < queue_size - 1; i++) desc[i].next = i + 1;
        desc[queue_size - 1].next = 0;
        free_head = 0;
        free_desc_count = queue_size; 
        
        uint32_t pfn = (uint32_t)(queue_phys >> 12);
        write32(VIRTIO_REG_QUEUE_ADDRESS, pfn);
        
        write8(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);
    }
    
    char buf[128];
    sprintf(buf, "Block Device (VirtIO %s)", is_modern ? "1.0 MMIO" : "Legacy I/O");
    print_status("[ VIRT ]", buf, "INFO");
    
    char newName[32];
    static int virt_count = 0;
    sprintf(newName, "virtio%d", virt_count++);
    this->setName(newName);
    
    if (capacity_sectors == 0) capacity_sectors = 262144; 
    this->setCapacity(capacity_sectors * 512); 
    
    this->setModel(is_modern ? "VirtIO Block (Modern)" : "VirtIO Block (Legacy)");
    
    DeviceManager::registerDevice(this);
    gpt_scan_partitions(this);
    
    return 1;
}

void VirtIODriver::emergencyStop() {
    if (is_modern && common_cfg) {
        common_cfg->device_status = 0;
    } else if (!is_modern && io_base != 0) {
        outb(io_base + VIRTIO_REG_DEVICE_STATUS, 0);
    }
}

int VirtIODriver::readBlock(uint64_t lba, uint32_t count, void* buffer) {
    storage_kiovec_t vec;
    vec.virt_addr = buffer;
    vec.size = (size_t)count * 512;
    vec.phys_addr = 0;
    if (!rust_dma_guard_validate(&vec, 1, 0)) {
        serial_printf("[VIRT] DMA Guard Blocked Read Access!\n");
        return 0;
    }

    uint8_t* ptr = (uint8_t*)buffer;
    volatile struct virtq_used* v_used = (volatile struct virtq_used*)used;
    
    uint64_t flags = spinlock_acquire(&io_lock);

    for (uint32_t i = 0; i < count; i++) {
        while (free_desc_count < 3) {
            spinlock_release(&io_lock, flags);
            hal_cpu_relax();
            flags = spinlock_acquire(&io_lock);
        }
        
        uint16_t head_idx = free_head;
        uint16_t desc1 = head_idx;
        uint16_t desc2 = desc[desc1].next;
        uint16_t desc3 = desc[desc2].next;
        
        free_head = desc[desc3].next;
        free_desc_count -= 3;
        
        uint8_t* meta_page = (uint8_t*)kmalloc_contiguous(4096);
        if (!meta_page) {
            desc[desc3].next = free_head;
            free_head = head_idx;
            free_desc_count += 3;
            spinlock_release(&io_lock, flags);
            return 0;
        }
        memset(meta_page, 0, 4096);
        
        struct virtio_blk_req_header* req = (struct virtio_blk_req_header*)meta_page;
        req->type = VIRTIO_BLK_T_IN;
        req->priority = 0;
        req->sector = lba + i;
        
        uint8_t* status_ptr = meta_page + sizeof(struct virtio_blk_req_header);
        
        uint64_t req_phys = get_physical_address((uint64_t)req);
        uint64_t buf_phys = get_physical_address((uint64_t)ptr + ((uint64_t)i * 512));
        uint64_t status_phys = get_physical_address((uint64_t)status_ptr);
        
        if (buf_phys == 0) {
            kfree_contiguous(meta_page, 4096);
            desc[desc3].next = free_head; free_head = head_idx; free_desc_count += 3;
            spinlock_release(&io_lock, flags);
            return 0;
        }
        pmm_inc_ref((void*)buf_phys); // PIN DMA MEMORY
        
        desc[desc1].addr = req_phys;
        desc[desc1].len = sizeof(struct virtio_blk_req_header);
        desc[desc1].flags = VRING_DESC_F_NEXT;
        desc[desc1].next = desc2;
        
        desc[desc2].addr = buf_phys;
        desc[desc2].len = 512;
        desc[desc2].flags = VRING_DESC_F_NEXT | VRING_DESC_F_WRITE;
        desc[desc2].next = desc3;
        
        desc[desc3].addr = status_phys;
        desc[desc3].len = 1;
        desc[desc3].flags = VRING_DESC_F_WRITE;
        desc[desc3].next = 0;
        
        virtio_wmb(); 
        
        uint16_t avail_idx = avail->idx;
        avail->ring[avail_idx % queue_size] = head_idx;
        
        __asm__ volatile("sfence" ::: "memory"); 
        avail->idx = avail_idx + 1;
        virtio_wmb(); 
        
        notifyQueue(0);
        
        uint64_t start_tsc = tsc_read_asm();
        bool completed = false;
        
        while (!check_timeout(start_tsc, 5000)) { // 5 saniye
            virtio_rmb(); 
            if (last_used_idx != v_used->idx) {
                completed = true;
                break;
            }
            __asm__ volatile("pause");
        }
        
        int result = 1;
        if (!completed) {
            result = 0;
            emergencyStop(); 
        } else {
            last_used_idx++;
            uint8_t status_val = *status_ptr;
            if (status_val != 0) result = 0;
        }
        
        pmm_dec_ref((void*)buf_phys); // UNPIN DMA MEMORY
        
        desc[desc3].next = free_head;
        free_head = head_idx;
        free_desc_count += 3;
        
        kfree_contiguous(meta_page, 4096);
        if (result == 0) {
            spinlock_release(&io_lock, flags);
            return 0;
        }
    }
    
    spinlock_release(&io_lock, flags);
    return 1;
}

int VirtIODriver::writeBlock(uint64_t lba, uint32_t count, const void* buffer) {
    if (this->isWriteProtected()) {
        printf("[VIRT] Blocked write to '%s' (Device Lockdown Active).\n", this->getName());
        return 0;
    }
    
    storage_kiovec_t vec;
    vec.virt_addr = (void*)buffer;
    vec.size = (size_t)count * 512;
    vec.phys_addr = 0;
    if (!rust_dma_guard_validate(&vec, 1, 0)) {
        serial_printf("[VIRT] DMA Guard Blocked Write Access!\n");
        return 0;
    }
    
    const uint8_t* ptr = (const uint8_t*)buffer;
    volatile struct virtq_used* v_used = (volatile struct virtq_used*)used;
    
    uint64_t flags = spinlock_acquire(&io_lock);

    for (uint32_t i = 0; i < count; i++) {
        while (free_desc_count < 3) {
            spinlock_release(&io_lock, flags);
            hal_cpu_relax();
            flags = spinlock_acquire(&io_lock);
        }
        
        uint16_t head_idx = free_head;
        uint16_t desc1 = head_idx;
        uint16_t desc2 = desc[desc1].next;
        uint16_t desc3 = desc[desc2].next;
        
        free_head = desc[desc3].next;
        free_desc_count -= 3;
        
        uint8_t* meta_page = (uint8_t*)kmalloc_contiguous(4096);
        if (!meta_page) {
            desc[desc3].next = free_head;
            free_head = head_idx;
            free_desc_count += 3;
            spinlock_release(&io_lock, flags);
            return 0;
        }
        memset(meta_page, 0, 4096);
        
        struct virtio_blk_req_header* req = (struct virtio_blk_req_header*)meta_page;
        req->type = VIRTIO_BLK_T_OUT; 
        req->priority = 0;
        req->sector = lba + i;
        
        uint8_t* status_ptr = meta_page + sizeof(struct virtio_blk_req_header);
        
        uint64_t req_phys = get_physical_address((uint64_t)req);
        uint64_t buf_phys = get_physical_address((uint64_t)ptr + ((uint64_t)i * 512));
        uint64_t status_phys = get_physical_address((uint64_t)status_ptr);
        
        if (buf_phys == 0) {
            kfree_contiguous(meta_page, 4096);
            desc[desc3].next = free_head; free_head = head_idx; free_desc_count += 3;
            spinlock_release(&io_lock, flags);
            return 0;
        }
        pmm_inc_ref((void*)buf_phys); // PIN DMA MEMORY

        desc[desc1].addr = req_phys;
        desc[desc1].len = sizeof(struct virtio_blk_req_header);
        desc[desc1].flags = VRING_DESC_F_NEXT; 
        desc[desc1].next = desc2;
        
        desc[desc2].addr = buf_phys;
        desc[desc2].len = 512;
        desc[desc2].flags = VRING_DESC_F_NEXT; 
        desc[desc2].next = desc3;
        
        desc[desc3].addr = status_phys;
        desc[desc3].len = 1;
        desc[desc3].flags = VRING_DESC_F_WRITE; 
        desc[desc3].next = 0;
        
        virtio_wmb(); 
        
        uint16_t avail_idx = avail->idx;
        avail->ring[avail_idx % queue_size] = head_idx;
        
        __asm__ volatile("sfence" ::: "memory"); 
        avail->idx = avail_idx + 1;
        virtio_wmb(); 
        
        notifyQueue(0);
        
        uint64_t start_tsc = tsc_read_asm();
        bool completed = false;
        
        while (!check_timeout(start_tsc, 5000)) { // 5 saniye
            virtio_rmb(); 
            if (last_used_idx != v_used->idx) {
                completed = true;
                break;
            }
            __asm__ volatile("pause");
        }
        
        int result = 1;
        if (!completed) {
            result = 0;
            emergencyStop();
        } else {
            last_used_idx++;
            uint8_t status_val = *status_ptr;
            if (status_val != 0) result = 0;
        }
        
        pmm_dec_ref((void*)buf_phys); // UNPIN DMA MEMORY

        desc[desc3].next = free_head;
        free_head = head_idx;
        free_desc_count += 3;
        
        kfree_contiguous(meta_page, 4096);
        if (result == 0) {
            spinlock_release(&io_lock, flags);
            return 0;
        }
    }
    
    spinlock_release(&io_lock, flags);
    return 1;
}

extern "C" {
    void init_virtio() {
        for (int i = 0; i < PCIe::getDeviceCount(); i++) {
            PCIeDevice* pci = PCIe::getDevice(i);
            if (pci->vendor_id == VIRTIO_VENDOR_ID && pci->device_id == VIRTIO_DEV_BLK) {
                VirtIODriver* drv = new VirtIODriver(pci);
                drv->init();
                return; 
            }
        }
    }
}