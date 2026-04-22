// drivers/storage/nvme/nvme.cpp
#include "drivers/storage/nvme/nvme.hpp"
#include "drivers/pci/pci.hpp"
#include "memory/paging.h" 
#include "memory/pmm.h"
#include "memory/kheap.h" 
#include "libc/stdio.h"
#include "libc/string.h"
#include "archs/cpu/cpu_hal.h"
#include "system/disk/gpt.h"
#include "system/power/power.h"
#include "archs/storage/storage_hal.h"

extern "C" void yield();
extern "C" void print_status(const char* prefix, const char* msg, const char* status);
extern bool scheduler_active;

#define PAGE_SIZE 4096

static inline uint64_t rdtsc() {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

NVMeDriver::NVMeDriver(PCIeDevice* pci)
    : Device("NVMe_INIT", DEV_BLOCK), pciDev(pci) 
{
    admin_sq_tail = 0;
    admin_cq_head = 0;
    admin_phase = 1;
    io_sq_tail = 0;
    io_cq_head = 0;
    io_phase = 1;
    db_stride = 4;
    namespace_id = 1; 
    initialized_early = false;
    
    spinlock_init(&lock); 
}

void NVMeDriver::writeDoorbell(uint16_t index, uint16_t val) {
    __asm__ volatile("sfence" ::: "memory");
    uintptr_t db_addr = (uintptr_t)regs + 0x1000 + (index * db_stride);
    *(volatile uint32_t*)db_addr = val;
}

void NVMeDriver::waitReady() {
    uint64_t start = rdtsc();
    uint64_t timeout_cycles = 1000000000ULL; 
    
    while((rdtsc() - start) < timeout_cycles) {
        if (regs->csts & NVME_CSTS_RDY) return;
        __asm__ volatile("pause");
    }
}

void NVMeDriver::submitAdminCmd(nvme_sq_entry_t* cmd) {
    memcpy(&admin_sq[admin_sq_tail], cmd, sizeof(nvme_sq_entry_t));
    admin_sq_tail++;
    if (admin_sq_tail >= 64) admin_sq_tail = 0;
    writeDoorbell(0, admin_sq_tail); 
}

bool NVMeDriver::waitForAdminCompletion(uint16_t cid) {
    uint64_t start = rdtsc();
    uint64_t timeout_cycles = 1000000000ULL; 

    while ((rdtsc() - start) < timeout_cycles) {
        volatile nvme_cq_entry_t* cqe = (volatile nvme_cq_entry_t*)&admin_cq[admin_cq_head];
        __asm__ volatile("lfence" ::: "memory");
        
        uint8_t pt = (cqe->status >> 0) & 1;
        if (pt == admin_phase) {
            if (cqe->cid == cid) {
                admin_cq_head++;
                if (admin_cq_head >= 64) {
                    admin_cq_head = 0;
                    admin_phase = !admin_phase;
                }
                writeDoorbell(1, admin_cq_head); 
                return true;
            }
        }
        
        hal_cpu_relax(); 
    }
    return false;
}

void NVMeDriver::submitIOCmd(nvme_sq_entry_t* cmd) {
    uint16_t next_tail = (io_sq_tail + 1);
    if (next_tail >= 64) next_tail = 0;
    
    if (next_tail == io_cq_head) return;

    memcpy(&io_sq[io_sq_tail], cmd, sizeof(nvme_sq_entry_t));
    io_sq_tail = next_tail;
    writeDoorbell(2, io_sq_tail); 
}

bool NVMeDriver::waitForIOCompletion(uint16_t cid) {
    uint64_t start = rdtsc();
    uint64_t timeout_cycles = 2000000000ULL;

    while ((rdtsc() - start) < timeout_cycles) {
        volatile nvme_cq_entry_t* cqe = (volatile nvme_cq_entry_t*)&io_cq[io_cq_head];
        __asm__ volatile("lfence" ::: "memory");
        
        uint8_t pt = (cqe->status >> 0) & 1;
        if (pt == io_phase) {
            if (cqe->cid == cid) {
                io_cq_head++;
                if (io_cq_head >= 64) {
                    io_cq_head = 0;
                    io_phase = !io_phase; 
                }
                writeDoorbell(3, io_cq_head); 
                return true;
            }
        }
        
        hal_cpu_relax(); 
    }
    return false;
}

void NVMeDriver::unpinBuffer(void* buffer, uint32_t size) {
    uint64_t current_virt = (uint64_t)buffer;
    uint32_t bytes_left = size;
    
    while (bytes_left > 0) {
        uint64_t phys_addr = get_physical_address(current_virt);
        if (phys_addr != 0) {
            pmm_dec_ref((void*)phys_addr);
        }
        
        uint32_t page_offset = current_virt & (PAGE_SIZE - 1);
        uint32_t chunk_size = PAGE_SIZE - page_offset;
        if (chunk_size > bytes_left) chunk_size = bytes_left;
        
        bytes_left -= chunk_size;
        current_virt += chunk_size;
    }
}

uint64_t NVMeDriver::setupPRPs(nvme_sq_entry_t* cmd, void* buffer, uint32_t size, void** out_prp_page_virt) {
    uint64_t current_virt = (uint64_t)buffer;
    uint64_t phys_addr = get_physical_address(current_virt);
    
    *out_prp_page_virt = nullptr;
    cmd->prp1 = phys_addr;
    pmm_inc_ref((void*)phys_addr); 
    
    uint32_t page_offset = current_virt & (PAGE_SIZE - 1);
    uint32_t first_page_cap = PAGE_SIZE - page_offset;
    
    if (size <= first_page_cap) {
        cmd->prp2 = 0;
        return 0;
    }
    
    uint32_t bytes_left = size - first_page_cap;
    current_virt += first_page_cap;
    
    if (bytes_left <= PAGE_SIZE) {
        uint64_t p2 = get_physical_address(current_virt);
        cmd->prp2 = p2;
        pmm_inc_ref((void*)p2); 
        return 0;
    }
    
    uint32_t data_pages_needed = (bytes_left + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32_t prp_list_pages = (data_pages_needed + 511 - 1) / 511;
    
    void* prp_block_virt = kmalloc_contiguous(prp_list_pages * PAGE_SIZE);
    if (!prp_block_virt) {
        unpinBuffer(buffer, size - bytes_left);
        return 0;
    }
    memset(prp_block_virt, 0, prp_list_pages * PAGE_SIZE);
    
    *out_prp_page_virt = prp_block_virt;
    uint64_t prp_head_phys = get_physical_address((uint64_t)prp_block_virt);
    cmd->prp2 = prp_head_phys;
    
    uint64_t* current_prp_list = (uint64_t*)prp_block_virt;
    uint32_t current_list_page_idx = 0;
    int prp_idx = 0;
    
    while (bytes_left > 0) {
        if (prp_idx == 511 && bytes_left > PAGE_SIZE) {
            current_list_page_idx++;
            uint64_t next_page_virt = (uint64_t)prp_block_virt + (current_list_page_idx * PAGE_SIZE);
            uint64_t next_page_phys = get_physical_address(next_page_virt);
            current_prp_list[511] = next_page_phys;
            current_prp_list = (uint64_t*)next_page_virt;
            prp_idx = 0;
        }
        
        phys_addr = get_physical_address(current_virt);
        current_prp_list[prp_idx++] = phys_addr;
        pmm_inc_ref((void*)phys_addr); 
        
        if (bytes_left > PAGE_SIZE) bytes_left -= PAGE_SIZE;
        else bytes_left = 0;
        
        current_virt += PAGE_SIZE;
    }
    
    return prp_head_phys;
}

void NVMeDriver::freePRPChain(void* prp_head_virt, uint32_t total_size) {
    if (!prp_head_virt) return;
    uint32_t first_page_cap = PAGE_SIZE; 
    if (total_size <= first_page_cap) return; 
    uint32_t bytes_left = total_size - first_page_cap;
    if (bytes_left <= PAGE_SIZE) return; 
    
    uint32_t data_pages_needed = (bytes_left + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32_t prp_list_pages = (data_pages_needed + 511 - 1) / 511;
    
    kfree_contiguous(prp_head_virt, prp_list_pages * PAGE_SIZE);
}

bool NVMeDriver::createIOQueues() {
    void* sq_virt = kmalloc_contiguous(4096);
    void* cq_virt = kmalloc_contiguous(4096);
    
    if (!sq_virt || !cq_virt) {
        if (sq_virt) kfree_contiguous(sq_virt, 4096);
        if (cq_virt) kfree_contiguous(cq_virt, 4096);
        return false;
    }
    
    io_sq = (nvme_sq_entry_t*)sq_virt;
    io_cq = (nvme_cq_entry_t*)cq_virt;
    
    memset(sq_virt, 0, 4096);
    memset(cq_virt, 0, 4096);
    
    uint64_t sq_phys = get_physical_address((uint64_t)sq_virt);
    uint64_t cq_phys = get_physical_address((uint64_t)cq_virt);

    nvme_sq_entry_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_OP_ADMIN_CREATE_CQ;
    cmd.cid = 2;
    cmd.prp1 = cq_phys; 
    cmd.cdw10 = (63 << 16) | 1; 
    cmd.cdw11 = 1; 
    submitAdminCmd(&cmd);
    if (!waitForAdminCompletion(2)) return false;

    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_OP_ADMIN_CREATE_SQ;
    cmd.cid = 3;
    cmd.prp1 = sq_phys; 
    cmd.cdw10 = (63 << 16) | 1;
    cmd.cdw11 = (1 << 16) | 1;
    submitAdminCmd(&cmd);
    
    return waitForAdminCompletion(3);
}

// OPTİMİZASYON YAMASI: Asenkron Donanım Başlatma (Phase 1)
int NVMeDriver::earlyInit() {
    pciDev->enableBusMaster();
    pciDev->enableMemorySpace();

    uint32_t bar0 = pciDev->getBAR(0);
    uint32_t bar1 = pciDev->getBAR(1);
    uint64_t base_phys = ((uint64_t)bar1 << 32) | (bar0 & 0xFFFFFFF0);
    
    void* virt_addr = ioremap(base_phys, 16384, PAGE_PRESENT | PAGE_WRITE | PAGE_PCD | PAGE_PWT);
    if (!virt_addr) return 0;
    
    regs = (volatile nvme_registers_t*)virt_addr;
    
    uint8_t dstrd = (regs->cap >> 32) & 0xF;
    db_stride = 1 << (2 + dstrd);
    
    regs->cc &= ~NVME_CC_EN;
    uint64_t start = rdtsc();
    while ((regs->csts & NVME_CSTS_RDY) && (rdtsc() - start) < 1000000000ULL) { hal_cpu_relax(); }
    
    regs->intms = 0xFFFFFFFF;

    void* sq_virt = kmalloc_contiguous(4096);
    void* cq_virt = kmalloc_contiguous(4096);
    
    if (!sq_virt || !cq_virt) {
        if (sq_virt) kfree_contiguous(sq_virt, 4096);
        if (cq_virt) kfree_contiguous(cq_virt, 4096);
        return 0;
    }
    
    memset(sq_virt, 0, 4096);
    memset(cq_virt, 0, 4096);

    admin_sq = (nvme_sq_entry_t*)sq_virt;
    admin_cq = (nvme_cq_entry_t*)cq_virt;
    
    regs->asq = get_physical_address((uint64_t)sq_virt);
    regs->acq = get_physical_address((uint64_t)cq_virt);
    regs->aqa = (63 << 16) | 63;

    // Kontrolcüyü başlat ve ASLA BEKLEMEDEN dön! (Arka planda uyanacak)
    regs->cc = NVME_CC_IOCQES_16 | NVME_CC_IOSQES_64 | NVME_CC_EN;
    
    initialized_early = true;
    return 1;
}

// OPTİMİZASYON YAMASI: Asenkron Donanım Başlatma (Phase 2)
int NVMeDriver::finalize() {
    if (!initialized_early) return 0;
    
    waitReady(); // SMP süreci geçtiği için bu bekleme anında (0ms) tamamlanır.

    if (!createIOQueues()) return 0;
    
    void* id_buf = kmalloc_contiguous(4096);
    if (!id_buf) return 0;
    memset(id_buf, 0, 4096);
    
    nvme_sq_entry_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_OP_ADMIN_IDENTIFY;
    cmd.cid = 1;
    cmd.nsid = 0; 
    cmd.prp1 = get_physical_address((uint64_t)id_buf); 
    cmd.cdw10 = 1;
    
    submitAdminCmd(&cmd);
    
    if (waitForAdminCompletion(1)) {
        char model[41];
        memcpy(model, (char*)id_buf + 24, 40); 
        model[40] = '\0';
        for (int i=39; i>=0; i--) { if (model[i] == ' ') model[i] = 0; else break; }
        
        char newName[32];
        DeviceManager::getNextNvmeName(newName); 
        this->setName(newName);
        this->setModel(model); 
        
        memset(id_buf, 0, 4096);
        memset(&cmd, 0, sizeof(cmd));
        cmd.opcode = NVME_OP_ADMIN_IDENTIFY;
        cmd.cid = 2;
        cmd.nsid = namespace_id; 
        cmd.prp1 = get_physical_address((uint64_t)id_buf);
        cmd.cdw10 = 0;
        
        submitAdminCmd(&cmd);
        if (waitForAdminCompletion(2)) {
            uint64_t nsze = *(uint64_t*)id_buf; 
            
            uint8_t flbas = *((uint8_t*)id_buf + 26);
            uint8_t lbaf_idx = flbas & 0x0F;
            uint32_t lbaf_ds = *((uint32_t*)id_buf + 128 + (lbaf_idx * 4));
            uint8_t lba_data_size = (lbaf_ds >> 16) & 0xFF; 
            
            uint32_t real_sector = 1 << lba_data_size;
            if (real_sector < 512 || real_sector > 4096) real_sector = 512;
            
            this->setCapacity(nsze * real_sector);
        } else {
            this->setCapacity(0);
        }

        DeviceManager::registerDevice(this);
        gpt_scan_partitions(this);
        
        kfree_contiguous(id_buf, 4096); 
        return 1;
    }
    
    kfree_contiguous(id_buf, 4096); 
    return 0;
}

int NVMeDriver::init() {
    if (earlyInit()) { return finalize(); }
    return 0;
}

int NVMeDriver::shutdown() {
    regs->cc &= ~NVME_CC_EN; 
    while (regs->csts & NVME_CSTS_RDY) {
        __asm__ volatile("pause");
    }
    return 1;
}

void NVMeDriver::emergencyStop() {
    if (regs) {
        regs->cc &= ~NVME_CC_EN;
    }
}

int NVMeDriver::readBlock(uint64_t lba, uint32_t count, void* buffer) {
    if (count == 0) return 0;

    storage_kiovec_t vec;
    vec.virt_addr = buffer;
    vec.size = (size_t)count * 512;
    vec.phys_addr = 0; 
    if (!rust_dma_guard_validate(&vec, 1, 0)) {
        serial_printf("[NVMe] DMA Guard Blocked Read Access!\n");
        return 0;
    }

    uint64_t flags = spinlock_acquire(&this->lock);

    uint8_t* user_buf = (uint8_t*)buffer;
    uint32_t sectors_read = 0;
    
    const uint32_t MAX_SECTORS_PER_CMD = 2048;

    while (sectors_read < count) {
        uint32_t chunk = count - sectors_read;
        if (chunk > MAX_SECTORS_PER_CMD) chunk = MAX_SECTORS_PER_CMD;

        nvme_sq_entry_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.opcode = NVME_OP_READ;
        cmd.cid = 100;
        cmd.nsid = namespace_id;
        
        void* prp_page = nullptr;
        uint32_t bytes = chunk * 512;
        
        uint64_t prp_res = setupPRPs(&cmd, user_buf + (sectors_read * 512), bytes, &prp_page);
        if (prp_res == 0 && cmd.prp1 == 0) { 
            spinlock_release(&this->lock, flags);
            return 0;
        }
        
        cmd.cdw10 = (uint32_t)(lba + sectors_read);
        cmd.cdw11 = (uint32_t)((lba + sectors_read) >> 32);
        cmd.cdw12 = (chunk - 1) & 0xFFFF;

        submitIOCmd(&cmd);
        
        if (!waitForIOCompletion(100)) {
            emergencyStop(); 
            unpinBuffer(user_buf + (sectors_read * 512), bytes);
            if(prp_page) freePRPChain(prp_page, bytes);
            spinlock_release(&this->lock, flags);
            return 0;
        }
        
        unpinBuffer(user_buf + (sectors_read * 512), bytes);
        if(prp_page) freePRPChain(prp_page, bytes);
        
        for(uint32_t i=0; i < bytes; i += 64) {
            __asm__ volatile("clflush (%0)" :: "r"((uint64_t)user_buf + (sectors_read * 512) + i));
        }
        __asm__ volatile("sfence" ::: "memory");

        sectors_read += chunk;
    }
    
    spinlock_release(&this->lock, flags);
    return 1;
}

int NVMeDriver::writeBlock(uint64_t lba, uint32_t count, const void* buffer) {
    if (count == 0) return 0;

    if (this->isWriteProtected()) {
        printf("[NVMe] Blocked write to '%s' (Device Lockdown Active).\n", this->getName());
        return 0;
    }

    storage_kiovec_t vec;
    vec.virt_addr = (void*)buffer;
    vec.size = (size_t)count * 512;
    vec.phys_addr = 0; 
    if (!rust_dma_guard_validate(&vec, 1, 0)) {
        serial_printf("[NVMe] DMA Guard Blocked Write Access!\n");
        return 0;
    }

    uint64_t flags = spinlock_acquire(&this->lock);

    const uint8_t* user_buf = (const uint8_t*)buffer;
    uint32_t sectors_written = 0;
    const uint32_t MAX_SECTORS_PER_CMD = 2048;

    while (sectors_written < count) {
        uint32_t chunk = count - sectors_written;
        if (chunk > MAX_SECTORS_PER_CMD) chunk = MAX_SECTORS_PER_CMD;
        
        uint32_t bytes = chunk * 512;

        for(uint32_t i=0; i < bytes; i += 64) {
            __asm__ volatile("clflush (%0)" :: "r"((uint64_t)user_buf + (sectors_written * 512) + i));
        }
        __asm__ volatile("mfence" ::: "memory");

        nvme_sq_entry_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.opcode = NVME_OP_WRITE; 
        cmd.cid = 101;
        cmd.nsid = namespace_id;
        
        void* prp_page = nullptr;
        uint64_t prp_res = setupPRPs(&cmd, (void*)(user_buf + (sectors_written * 512)), bytes, &prp_page);
        if (prp_res == 0 && cmd.prp1 == 0) {
            spinlock_release(&this->lock, flags);
            return 0;
        }
        
        cmd.cdw10 = (uint32_t)(lba + sectors_written);
        cmd.cdw11 = (uint32_t)((lba + sectors_written) >> 32);
        cmd.cdw12 = (chunk - 1) & 0xFFFF;

        submitIOCmd(&cmd);
        
        if (!waitForIOCompletion(101)) {
            emergencyStop();
            unpinBuffer((void*)(user_buf + (sectors_written * 512)), bytes);
            if(prp_page) freePRPChain(prp_page, bytes);
            spinlock_release(&this->lock, flags);
            return 0;
        }
        
        unpinBuffer((void*)(user_buf + (sectors_written * 512)), bytes);
        if(prp_page) freePRPChain(prp_page, bytes);
        
        sectors_written += chunk;
    }
    
    spinlock_release(&this->lock, flags);
    return 1;
}

int NVMeDriver::read_vector(uint64_t lba, kiovec_t* vectors, int vector_count) {
    if (vector_count == 0) return 0;
    
    if (!rust_dma_guard_validate((const storage_kiovec_t*)vectors, vector_count, 0)) {
        serial_printf("[NVMe] DMA Guard Blocked Vector Read!\n");
        return 0;
    }
    
    bool is_aligned = true;
    size_t total_bytes = 0;
    
    for (int i = 0; i < vector_count; i++) {
        total_bytes += vectors[i].size;
        if (i > 0) {
            if ((vectors[i].phys_addr & 0xFFF) != 0 || (vectors[i].size % PAGE_SIZE) != 0) {
                is_aligned = false; 
                break;
            }
        }
    }
    
    if (!is_aligned) {
        return Device::read_vector(lba, vectors, vector_count);
    }
    
    uint64_t flags = spinlock_acquire(&this->lock);
    
    nvme_sq_entry_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_OP_READ;
    cmd.cid = 102;
    cmd.nsid = namespace_id;
    
    uint32_t total_sectors = (uint32_t)(total_bytes / 512);
    
    storage_dma_chain_t chain = rust_storage_build_sg_list((const storage_kiovec_t*)vectors, vector_count);
    if (!chain.is_valid) {
        spinlock_release(&this->lock, flags);
        return 0;
    }
    
    cmd.prp1 = chain.prp1;
    cmd.prp2 = chain.prp2;
    
    cmd.cdw10 = (uint32_t)lba;
    cmd.cdw11 = (uint32_t)(lba >> 32);
    cmd.cdw12 = (total_sectors - 1) & 0xFFFF;

    submitIOCmd(&cmd);
    bool success = waitForIOCompletion(102);

    if (!success) emergencyStop();

    rust_storage_free_sg_list(&chain, (const storage_kiovec_t*)vectors, vector_count);
    
    spinlock_release(&this->lock, flags);
    return success ? total_sectors : 0;
}

int NVMeDriver::write_vector(uint64_t lba, kiovec_t* vectors, int vector_count) {
    if (this->isWriteProtected()) {
        printf("[NVMe] Blocked vector write to '%s' (Lockdown).\n", this->getName());
        return 0;
    }
    
    if (vector_count == 0) return 0;
    
    if (!rust_dma_guard_validate((const storage_kiovec_t*)vectors, vector_count, 0)) {
        serial_printf("[NVMe] DMA Guard Blocked Vector Write!\n");
        return 0;
    }
    
    bool is_aligned = true;
    size_t total_bytes = 0;
    for (int i = 0; i < vector_count; i++) {
        total_bytes += vectors[i].size;
        if (i > 0) {
            if ((vectors[i].phys_addr & 0xFFF) != 0 || (vectors[i].size % PAGE_SIZE) != 0) {
                is_aligned = false; 
                break;
            }
        }
    }
    
    if (!is_aligned) {
        return Device::write_vector(lba, vectors, vector_count);
    }
    
    uint64_t flags = spinlock_acquire(&this->lock);
    
    nvme_sq_entry_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_OP_WRITE;
    cmd.cid = 103;
    cmd.nsid = namespace_id;
    
    uint32_t total_sectors = (uint32_t)(total_bytes / 512);
    
    storage_dma_chain_t chain = rust_storage_build_sg_list((const storage_kiovec_t*)vectors, vector_count);
    if (!chain.is_valid) {
        spinlock_release(&this->lock, flags);
        return 0;
    }
    
    cmd.prp1 = chain.prp1;
    cmd.prp2 = chain.prp2;
    
    cmd.cdw10 = (uint32_t)lba;
    cmd.cdw11 = (uint32_t)(lba >> 32);
    cmd.cdw12 = (total_sectors - 1) & 0xFFFF;

    submitIOCmd(&cmd);
    bool success = waitForIOCompletion(103);

    if (!success) emergencyStop();

    rust_storage_free_sg_list(&chain, (const storage_kiovec_t*)vectors, vector_count);
    
    spinlock_release(&this->lock, flags);
    return success ? total_sectors : 0;
}

// Global instances array
static NVMeDriver* g_nvme_drivers[4] = {nullptr};
static int g_nvme_count = 0;

extern "C" {
    __attribute__((used, noinline))
    void init_nvme_early() {
        for (int i = 0; i < PCIe::getDeviceCount(); i++) {
            PCIeDevice* pci = PCIe::getDevice(i);
            if (pci->class_id == 0x01 && pci->subclass_id == 0x08) {
                if (g_nvme_count < 4) {
                    NVMeDriver* drv = new NVMeDriver(pci);
                    if (drv->earlyInit()) {
                        g_nvme_drivers[g_nvme_count++] = drv;
                    } else {
                        delete drv;
                    }
                }
            }
        }
    }

    __attribute__((used, noinline))
    int init_nvme_late() {
        int initialized = 0;
        for (int i = 0; i < g_nvme_count; i++) {
            if (g_nvme_drivers[i]) {
                if (g_nvme_drivers[i]->finalize()) {
                    initialized++;
                }
            }
        }
        return initialized > 0 ? 1 : 0;
    }
}