// drivers/storage/ahci/ahci_port.cpp
#include "drivers/storage/ahci/ahci_port.hpp"
#include "memory/paging.h"
#include "memory/pmm.h"
#include "memory/kheap.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "archs/cpu/cpu_hal.h"
#include "archs/storage/storage_hal.h"

extern "C" void yield();
extern "C" uint64_t get_physical_address(uint64_t virtual_addr);
extern "C" void* kmalloc_contiguous(size_t size);
extern "C" void kfree_contiguous(void* ptr, size_t size);
extern "C" void* kmalloc(size_t size);
extern "C" void kfree(void* ptr);

#define HBA_PxIS_TFES (1 << 30)
#define PAGE_SIZE 4096
#define ATA_CMD_IDENTIFY    0xEC
#define ATA_CMD_READ_DMA_EX 0x25
#define ATA_CMD_WRITE_DMA_EX 0x35
#define ATA_CMD_PACKET      0xA0

#define SCSI_READ_12        0xA8
#define SCSI_READ_10        0x28
#define SCSI_READ_CAPACITY  0x25
#define SCSI_REQUEST_SENSE  0x03

static inline uint32_t swap_uint32(uint32_t val) {
    return ((val >> 24) & 0xff) | ((val << 8) & 0xff0000) |
           ((val >> 8)  & 0xff00) | ((val << 24) & 0xff000000);
}

static inline bool check_timeout(uint64_t start_ms, uint32_t timeout_ms) {
    return (hal_timer_get_uptime_ms() - start_ms) > timeout_ms;
}

void AHCIPort::startCmd() {
    int wait = 0;
    while ((hbaPort->cmd & (1 << 15)) && wait < 500) {
        hal_timer_delay_ms(1);
        wait++;
    }
    hbaPort->cmd |= (1 << 4);
    hbaPort->cmd |= (1 << 0);
}

void AHCIPort::stopCmd() {
    hbaPort->cmd &= ~(1 << 0);
    hbaPort->cmd &= ~(1 << 4);
    int wait = 0;
    while (wait < 500) {
        if (!(hbaPort->cmd & (1 << 14)) && !(hbaPort->cmd & (1 << 15))) {
            break;
        } else {
            // Wait
        }
        hal_timer_delay_ms(1);
        wait++;
    }
}

void AHCIPort::reset() {
    stopCmd();

    uint32_t sctl = hbaPort->sctl;
    sctl &= ~0x0Fu;
    sctl |= 1u;
    hbaPort->sctl = sctl;

    hal_timer_delay_ms(5);

    sctl &= ~0x0Fu;
    hbaPort->sctl = sctl;

    int wait = 0;
    while ((hbaPort->ssts & 0x0F) != 3 && wait < 500) {
        hal_timer_delay_ms(1);
        wait++;
    }

    hbaPort->serr = 0xFFFFFFFF;
}

int AHCIPort::findCmdSlot() {
    uint32_t slots = (hbaPort->sact | hbaPort->ci);
    for (int i = 0; i < 32; i++) {
        if ((slots & 1) == 0) { 
            return i; 
        } else {
            // Occupied
        }
        slots >>= 1;
    }
    return -1;
}

bool AHCIPort::identify() {
    hbaPort->is = 0xFFFFFFFF;
    int slot = findCmdSlot();
    if (slot == -1) { 
        return false; 
    } else {
        // Valid
    }

    hba_cmd_header_t* cmdheader = (hba_cmd_header_t*)clb_virt;
    cmdheader += slot;
    cmdheader->cfl   = sizeof(fis_reg_h2d_t) / 4;
    cmdheader->w     = 0;
    cmdheader->prdtl = 1;
    cmdheader->a     = 0;

    hba_cmd_tbl_t* cmdtbl = (hba_cmd_tbl_t*)ctba_virt[slot];
    memset(cmdtbl, 0, sizeof(hba_cmd_tbl_t));

    uint16_t* id_buf = (uint16_t*)kmalloc_contiguous(512);
    if (!id_buf) { 
        return false; 
    } else {
        // Allocated
    }
    memset(id_buf, 0, 512);

    bool result = false;

    uint64_t phys = get_physical_address((uint64_t)id_buf);
    cmdtbl->prdt_entry[0].dba  = (uint32_t)phys;
    cmdtbl->prdt_entry[0].dbau = (uint32_t)(phys >> 32);
    cmdtbl->prdt_entry[0].dbc  = 511;
    cmdtbl->prdt_entry[0].i    = 1;

    fis_reg_h2d_t* cmd = (fis_reg_h2d_t*)(&cmdtbl->cfis);
    cmd->fis_type = 0x27;
    cmd->c        = 1;
    cmd->command  = is_atapi ? 0xA1 : ATA_CMD_IDENTIFY;

    uint64_t spin_start = hal_timer_get_uptime_ms();
    uint64_t start = 0;
    uint64_t wakeups = 0;
    
    while ((hbaPort->tfd & (0x80 | 0x08))) {
        if (check_timeout(spin_start, 500)) {
            goto identify_done; 
        } else {
            // Wait
        }
        hal_cpu_relax();
    }

    hbaPort->ci = 1u << slot;
    start = hal_timer_get_uptime_ms(); 

    while (1) {
        if ((hbaPort->ci & (1u << slot)) == 0) { 
            break; 
        } else {
            // Wait
        }
        if (hbaPort->is & HBA_PxIS_TFES) { 
            goto identify_done; 
        } else {
            // No error
        }
        
        if (check_timeout(start, 750) || wakeups > 5000000) { 
            goto identify_done; 
        } else {
            // Wait
        }
        hal_cpu_relax();
        wakeups++;
    }

    if (!is_atapi) {
        uint64_t lba48 = *((uint64_t*)&id_buf[100]);
        this->sector_count = lba48;
    } else {
        uint16_t config = id_buf[0];
        uint8_t pkt_size = config & 0x3u;
        if (pkt_size == 1) {
            this->atapi_packet_size = 16;
        } else {
            this->atapi_packet_size = 12;
        }
        this->sector_count = 0;
    }

    for (int i = 0; i < 20; i++) {
        uint16_t w = id_buf[27 + i];
        this->model_name[i * 2]     = (char)(w >> 8);
        this->model_name[i * 2 + 1] = (char)w;
    }
    this->model_name[40] = '\0';
    for (int i = 39; i > 0; i--) {
        if (this->model_name[i] == ' ') { 
            this->model_name[i] = '\0'; 
        } else { 
            break; 
        }
    }

    result = true;

identify_done:
    kfree_contiguous(id_buf, 512);
    return result;
}

int AHCIPort::configure() {
    this->sector_count      = 0;
    this->is_atapi          = false;
    this->atapi_packet_size = 12;

    stopCmd();
    hbaPort->serr = 0xFFFFFFFF;

    clb_virt = kmalloc_contiguous(4096);
    if (!clb_virt) { 
        return -1; 
    } else {
        // Allocated
    }
    memset(clb_virt, 0, 4096);

    uint64_t clb_phys = get_physical_address((uint64_t)clb_virt);
    hbaPort->clb  = (uint32_t)clb_phys;
    hbaPort->clbu = (uint32_t)(clb_phys >> 32);

    fb_virt = (void*)((uint64_t)clb_virt + 1024);
    uint64_t fis_phys = clb_phys + 1024;
    hbaPort->fb  = (uint32_t)fis_phys;
    hbaPort->fbu = (uint32_t)(fis_phys >> 32);

    hba_cmd_header_t* cmdheader = (hba_cmd_header_t*)clb_virt;
    for (int i = 0; i < 32; i++) {
        cmdheader[i].prdtl = 0;
        ctba_virt[i] = kmalloc_contiguous(4096);
        if (!ctba_virt[i]) {
            return -1;
        } else {
            // Allocated
        }
        memset(ctba_virt[i], 0, 4096);

        uint64_t cmd_tbl_phys = get_physical_address((uint64_t)ctba_virt[i]);
        cmdheader[i].ctba  = (uint32_t)cmd_tbl_phys;
        cmdheader[i].ctbau = (uint32_t)(cmd_tbl_phys >> 32);
    }

    hbaPort->cmd |= (1 << 4);

    uint64_t spin_start = hal_timer_get_uptime_ms();
    while (hbaPort->sig == 0xFFFFFFFF) {
        if (check_timeout(spin_start, 10)) {
            break;
        } else {
            // Wait
        }
        hal_cpu_relax();
    }

    uint32_t sig = hbaPort->sig;
    if (sig == AHCI_SIG_ATAPI) {
        this->is_atapi = true;
    } else if (sig == AHCI_SIG_SATA) {
        this->is_atapi = false;
    } else {
        hbaPort->cmd &= ~(1 << 4); 
        return -1;
    }

    hbaPort->cmd |= (1 << 0);

    if (!identify()) {
        serial_printf("[AHCI] Port %d identification failed.\n", id);
    } else {
        // Identified
    }

    if (is_atapi) {
        for (int i = 0; i < 3; i++) {
            if (atapi_read_capacity()) { 
                break; 
            } else {
                // Retry
            }
            hal_timer_delay_ms(10);
        }
    } else {
        // SATA
    }

    return 0;
}

void AHCIPort::unpinBuffer(void* buffer, uint32_t byte_count) {
    uint64_t current_virt = (uint64_t)buffer;
    uint32_t bytes_remaining = byte_count;

    while (bytes_remaining > 0) {
        uint64_t phys_addr = get_physical_address(current_virt);
        if (phys_addr != 0) {
            pmm_dec_ref((void*)phys_addr);
        } else {
            // Unmapped
        }
        
        uint32_t page_offset = current_virt & (PAGE_SIZE - 1);
        uint32_t chunk_size = PAGE_SIZE - page_offset;
        if (chunk_size > bytes_remaining) {
            chunk_size = bytes_remaining;
        } else {
            // Fits
        }
        
        bytes_remaining -= chunk_size;
        current_virt += chunk_size;
    }
}

int AHCIPort::fillPrdt(hba_cmd_tbl_t* cmdtbl, void* buffer, uint32_t byte_count) {
    uint64_t current_virt  = (uint64_t)buffer;
    uint32_t bytes_remaining = byte_count;
    int prdt_idx = 0;
    const int MAX_PRDT_ENTRIES = 240;

    while (bytes_remaining > 0) {
        if (prdt_idx >= MAX_PRDT_ENTRIES) { 
            unpinBuffer(buffer, byte_count - bytes_remaining);
            return -1; 
        } else {
            // Fits
        }

        uint64_t phys_addr = get_physical_address(current_virt);
        if (phys_addr == 0) { 
            unpinBuffer(buffer, byte_count - bytes_remaining);
            return -1; 
        } else {
            // Mapped
        }

        pmm_inc_ref((void*)phys_addr);

        uint32_t page_offset    = (uint32_t)(current_virt & (PAGE_SIZE - 1));
        uint32_t bytes_to_end   = PAGE_SIZE - page_offset;
        uint32_t chunk_size     = (bytes_remaining < bytes_to_end) ? bytes_remaining : bytes_to_end;

        cmdtbl->prdt_entry[prdt_idx].dba  = (uint32_t)phys_addr;
        cmdtbl->prdt_entry[prdt_idx].dbau = (uint32_t)(phys_addr >> 32);
        cmdtbl->prdt_entry[prdt_idx].dbc  = chunk_size - 1;
        cmdtbl->prdt_entry[prdt_idx].i    = 0;

        bytes_remaining -= chunk_size;
        current_virt    += chunk_size;
        prdt_idx++;
    }
    return prdt_idx;
}

#ifdef __clang__
__attribute__((optnone))
#else
__attribute__((optimize("O0")))
#endif
bool AHCIPort::scsi_packet(uint8_t* packet, uint32_t packet_len,
                            void* buffer, uint32_t transfer_size, bool write) {
    
    if (buffer && transfer_size > 0) {
        storage_kiovec_t vec;
        vec.virt_addr = buffer;
        vec.size = transfer_size;
        vec.phys_addr = 0;
        if (!rust_dma_guard_validate(&vec, 1, 0)) {
            serial_printf("[AHCI] DMA Guard Blocked ATAPI Packet Access!\n");
            return false;
        } else {
            // Valid
        }
    } else {
        // No buffer
    }

    uint64_t flags = spinlock_acquire(&this->lock);

    hbaPort->is = 0xFFFFFFFF;
    int slot = findCmdSlot();
    if (slot == -1) {
        spinlock_release(&this->lock, flags);
        return false;
    } else {
        // Valid
    }

    hba_cmd_header_t* cmdheader = (hba_cmd_header_t*)clb_virt;
    cmdheader += slot;
    cmdheader->cfl = sizeof(fis_reg_h2d_t) / 4;
    cmdheader->w   = write ? 1 : 0;
    cmdheader->a   = 1;

    hba_cmd_tbl_t* cmdtbl = (hba_cmd_tbl_t*)ctba_virt[slot];
    memset(cmdtbl, 0, sizeof(hba_cmd_tbl_t));

    for (uint32_t i = 0; i < packet_len; i++) {
        cmdtbl->acmd[i] = packet[i];
    }

    int prdt_count = 0;
    if (transfer_size > 0 && buffer != nullptr) {
        prdt_count = fillPrdt(cmdtbl, buffer, transfer_size);
        if (prdt_count < 0) {
            spinlock_release(&this->lock, flags);
            return false;
        } else {
            // Valid
        }
    } else {
        // No data
    }
    cmdheader->prdtl = (uint16_t)prdt_count;

    fis_reg_h2d_t* cmd = (fis_reg_h2d_t*)(&cmdtbl->cfis);
    cmd->fis_type = 0x27;
    cmd->c        = 1;
    cmd->command  = 0xA0;
    cmd->featurel = 1;

    uint64_t spin_start = hal_timer_get_uptime_ms();
    while ((hbaPort->tfd & (0x80 | 0x08))) {
        if (check_timeout(spin_start, 500)) {
            reset();
            unpinBuffer(buffer, transfer_size);
            spinlock_release(&this->lock, flags);
            return false;
        } else {
            // Wait
        }
        hal_cpu_relax(); 
    }

    if (buffer && transfer_size > 0) {
        if ((uint64_t)buffer >= 0xC0000000ULL) {
            hal_memory_barrier_full();
        } else {
            // Low mem
        }
    } else {
        // No buffer
    }

    hbaPort->ci = 1u << slot;

    uint64_t start = hal_timer_get_uptime_ms();
    bool ok = true;

    while (1) {
        if ((hbaPort->ci & (1u << slot)) == 0) { 
            break; 
        } else {
            // Wait
        }
        if (hbaPort->is & HBA_PxIS_TFES) {
            ok = false;
            break;
        } else {
            // No error
        }
        if (check_timeout(start, 750)) { 
            reset();
            ok = false;
            break;
        } else {
            // Wait
        }
        hal_cpu_relax(); 
    }
    
    unpinBuffer(buffer, transfer_size);

    if (!ok || (hbaPort->is & HBA_PxIS_TFES)) {
        spinlock_release(&this->lock, flags);
        return false;
    } else {
        // Success
    }
    
    spinlock_release(&this->lock, flags);
    return true;
}

bool AHCIPort::atapi_read_capacity() {
    uint8_t packet[16];
    memset(packet, 0, 16);
    packet[0] = SCSI_READ_CAPACITY;

    uint32_t* resp = (uint32_t*)kmalloc_contiguous(8);
    if (!resp) { 
        return false; 
    } else {
        // Allocated
    }
    memset(resp, 0, 8);

    bool result = false;

    if (scsi_packet(packet, 12, resp, 8, false)) {
        uint32_t last_lba   = swap_uint32(resp[0]);
        uint32_t block_size = swap_uint32(resp[1]);

        if (last_lba > 0 && block_size > 0) {
            this->sector_count = last_lba + 1;
            serial_printf("[AHCI] ATAPI Capacity: %d blocks, %d bytes/block\n",
                          this->sector_count, block_size);
            result = true;
        } else {
            uint8_t sense_pkt[16];
            memset(sense_pkt, 0, 16);
            sense_pkt[0] = SCSI_REQUEST_SENSE;
            sense_pkt[4] = 18;

            uint8_t* sense_buf = (uint8_t*)kmalloc_contiguous(32);
            if (sense_buf) {
                scsi_packet(sense_pkt, 12, sense_buf, 18, false);
                kfree_contiguous(sense_buf, 32);
            } else {
                // OOM
            }
        }
    } else {
        // Packet failed
    }

    kfree_contiguous(resp, 8);
    return result;
}

#ifdef __clang__
__attribute__((optnone))
#else
__attribute__((optimize("O0")))
#endif
bool AHCIPort::atapi_read(uint64_t sector, uint32_t count, void* buffer) {
    uint8_t* ptr = (uint8_t*)buffer;
    uint32_t sectors_read = 0;
    const uint32_t MAX_BLOCKS_PER_CMD = 32;

    while (sectors_read < count) {
        uint32_t chunk = count - sectors_read;
        if (chunk > MAX_BLOCKS_PER_CMD) { 
            chunk = MAX_BLOCKS_PER_CMD; 
        } else {
            // Fits
        }

        uint64_t current_sector = sector + sectors_read;
        uint32_t byte_count     = chunk * 2048;

        uint8_t* temp_buf = (uint8_t*)kmalloc_contiguous(byte_count);
        if (!temp_buf) { 
            return false; 
        } else {
            // Allocated
        }

        uint8_t pkt[16];
        memset(pkt, 0, 16);
        pkt[0] = SCSI_READ_10;
        pkt[2] = (uint8_t)(current_sector >> 24);
        pkt[3] = (uint8_t)(current_sector >> 16);
        pkt[4] = (uint8_t)(current_sector >> 8);
        pkt[5] = (uint8_t)(current_sector);
        pkt[7] = (uint8_t)(chunk >> 8);
        pkt[8] = (uint8_t)(chunk);

        bool ok = scsi_packet(pkt, 12, temp_buf, byte_count, false);

        if (ok) {
            memcpy(ptr + (sectors_read * 2048), temp_buf, byte_count);
        } else {
            // Failed
        }

        kfree_contiguous(temp_buf, byte_count);

        if (!ok) { 
            return false; 
        } else {
            // Success
        }

        sectors_read += chunk;
    }

    return true;
}

bool AHCIPort::read(uint64_t sector, uint32_t count, void* buffer) {
    if (is_atapi) { 
        return atapi_read(sector, count, buffer); 
    } else {
        // SATA
    }

    storage_kiovec_t vec;
    vec.virt_addr = buffer;
    vec.size = (size_t)count * 512;
    vec.phys_addr = 0;
    if (!rust_dma_guard_validate(&vec, 1, 0)) {
        serial_printf("[AHCI] DMA Guard Blocked Read Access!\n");
        return false;
    } else {
        // Valid
    }

    uint8_t* user_buf = (uint8_t*)buffer;
    uint32_t sectors_read = 0;
    const uint32_t MAX_SECTORS_PER_CMD = 1024;

    uint64_t flags = spinlock_acquire(&this->lock);

    while (sectors_read < count) {
        uint32_t chunk = count - sectors_read;
        if (chunk > MAX_SECTORS_PER_CMD) {
            chunk = MAX_SECTORS_PER_CMD;
        } else {
            // Fits
        }

        hbaPort->serr = 0xFFFFFFFF;
        hbaPort->is   = 0xFFFFFFFF;
        int slot = findCmdSlot();
        if (slot == -1) { 
            spinlock_release(&this->lock, flags); 
            return false; 
        } else {
            // Valid
        }

        hba_cmd_header_t* cmdheader = (hba_cmd_header_t*)clb_virt;
        cmdheader += slot;
        cmdheader->cfl = sizeof(fis_reg_h2d_t) / 4;
        cmdheader->w   = 0;
        cmdheader->a   = 0;

        hba_cmd_tbl_t* cmdtbl = (hba_cmd_tbl_t*)ctba_virt[slot];
        memset(cmdtbl, 0, sizeof(hba_cmd_tbl_t));

        int prdt_count = fillPrdt(cmdtbl, user_buf + (sectors_read * 512), chunk * 512);
        if (prdt_count < 0) { 
            spinlock_release(&this->lock, flags); 
            return false; 
        } else {
            // Valid
        }
        cmdheader->prdtl = (uint16_t)prdt_count;

        fis_reg_h2d_t* cmd = (fis_reg_h2d_t*)(&cmdtbl->cfis);
        cmd->fis_type = 0x27;
        cmd->c        = 1;
        cmd->command  = ATA_CMD_READ_DMA_EX;
        
        uint64_t curr_sector = sector + sectors_read;
        cmd->lba0     = (uint8_t)curr_sector;
        cmd->lba1     = (uint8_t)(curr_sector >> 8);
        cmd->lba2     = (uint8_t)(curr_sector >> 16);
        cmd->device   = 1 << 6;
        cmd->lba3     = (uint8_t)(curr_sector >> 24);
        cmd->lba4     = (uint8_t)(curr_sector >> 32);
        cmd->lba5     = (uint8_t)(curr_sector >> 40);
        cmd->countl   = (uint8_t)(chunk & 0xFF);
        cmd->counth   = (uint8_t)((chunk >> 8) & 0xFF);

        uint64_t spin_start = hal_timer_get_uptime_ms();
        while ((hbaPort->tfd & (0x80 | 0x08))) {
            if(check_timeout(spin_start, 500)) { 
                unpinBuffer(user_buf + (sectors_read * 512), chunk * 512);
                spinlock_release(&this->lock, flags); 
                return false; 
            } else {
                // Wait
            }
            hal_cpu_relax();
        }

        for (uint64_t i = 0; i < (chunk * 512) + 64; i += 64) {
            hal_cache_flush_line((const void*)((uint64_t)user_buf + (sectors_read * 512) + i));
        }
        hal_memory_barrier_release();

        hbaPort->ci = 1u << slot;

        uint64_t start = hal_timer_get_uptime_ms();
        bool ok = true;

        while (1) {
            if ((hbaPort->ci & (1u << slot)) == 0) { 
                break; 
            } else {
                // Wait
            }
            if (hbaPort->is & HBA_PxIS_TFES) { 
                ok = false;
                break; 
            } else {
                // No error
            }
            if (check_timeout(start, 750)) { 
                reset();
                ok = false;
                break;
            } else {
                // Wait
            }
            hal_cpu_relax();
        }
        
        unpinBuffer(user_buf + (sectors_read * 512), chunk * 512);

        if (!ok || (hbaPort->is & HBA_PxIS_TFES)) {
            spinlock_release(&this->lock, flags);
            return false;
        } else {
            // Success
        }
        
        sectors_read += chunk;
    }

    spinlock_release(&this->lock, flags);
    return true;
}

bool AHCIPort::write(uint64_t sector, uint32_t count, const void* buffer) {
    if (is_atapi) { 
        return false; 
    } else {
        // SATA
    }

    storage_kiovec_t vec;
    vec.virt_addr = (void*)buffer;
    vec.size = (size_t)count * 512;
    vec.phys_addr = 0;
    if (!rust_dma_guard_validate(&vec, 1, 0)) {
        serial_printf("[AHCI] DMA Guard Blocked Write Access!\n");
        return false;
    } else {
        // Valid
    }

    const uint8_t* user_buf = (const uint8_t*)buffer;
    uint32_t sectors_written = 0;
    const uint32_t MAX_SECTORS_PER_CMD = 1024;

    uint64_t flags = spinlock_acquire(&this->lock);

    while (sectors_written < count) {
        uint32_t chunk = count - sectors_written;
        if (chunk > MAX_SECTORS_PER_CMD) {
            chunk = MAX_SECTORS_PER_CMD;
        } else {
            // Fits
        }

        hbaPort->serr = 0xFFFFFFFF;
        hbaPort->is   = 0xFFFFFFFF;
        int slot = findCmdSlot();
        if (slot == -1) { 
            spinlock_release(&this->lock, flags); 
            return false; 
        } else {
            // Valid
        }

        hba_cmd_header_t* cmdheader = (hba_cmd_header_t*)clb_virt;
        cmdheader += slot;
        cmdheader->cfl = sizeof(fis_reg_h2d_t) / 4;
        cmdheader->w   = 1;
        cmdheader->a   = 0;

        hba_cmd_tbl_t* cmdtbl = (hba_cmd_tbl_t*)ctba_virt[slot];
        memset(cmdtbl, 0, sizeof(hba_cmd_tbl_t));

        int prdt_count = fillPrdt(cmdtbl, (void*)(user_buf + (sectors_written * 512)), chunk * 512);
        if (prdt_count < 0) { 
            spinlock_release(&this->lock, flags); 
            return false; 
        } else {
            // Valid
        }
        cmdheader->prdtl = (uint16_t)prdt_count;

        fis_reg_h2d_t* cmd = (fis_reg_h2d_t*)(&cmdtbl->cfis);
        cmd->fis_type = 0x27;
        cmd->c        = 1;
        cmd->command  = ATA_CMD_WRITE_DMA_EX;
        
        uint64_t curr_sector = sector + sectors_written;
        cmd->lba0     = (uint8_t)curr_sector;
        cmd->lba1     = (uint8_t)(curr_sector >> 8);
        cmd->lba2     = (uint8_t)(curr_sector >> 16);
        cmd->device   = 1 << 6;
        cmd->lba3     = (uint8_t)(curr_sector >> 24);
        cmd->lba4     = (uint8_t)(curr_sector >> 32);
        cmd->lba5     = (uint8_t)(curr_sector >> 40);
        cmd->countl   = (uint8_t)(chunk & 0xFF);
        cmd->counth   = (uint8_t)((chunk >> 8) & 0xFF);

        uint64_t spin_start = hal_timer_get_uptime_ms();
        while ((hbaPort->tfd & (0x80 | 0x08))) {
            if(check_timeout(spin_start, 500)) { 
                unpinBuffer((void*)(user_buf + (sectors_written * 512)), chunk * 512);
                spinlock_release(&this->lock, flags); 
                return false; 
            } else {
                // Wait
            }
            hal_cpu_relax();
        }

        for (uint64_t i = 0; i < (chunk * 512) + 64; i += 64) {
            hal_cache_flush_line((const void*)((uint64_t)user_buf + (sectors_written * 512) + i));
        }
        hal_memory_barrier_full();

        hbaPort->ci = 1u << slot;

        uint64_t start = hal_timer_get_uptime_ms();
        bool ok = true;

        while (1) {
            if ((hbaPort->ci & (1u << slot)) == 0) { 
                break; 
            } else {
                // Wait
            }
            if (hbaPort->is & HBA_PxIS_TFES) { 
                ok = false;
                break; 
            } else {
                // No error
            }
            if (check_timeout(start, 750)) { 
                reset();
                ok = false;
                break;
            } else {
                // Wait
            }
            hal_cpu_relax();
        }
        
        unpinBuffer((void*)(user_buf + (sectors_written * 512)), chunk * 512);

        if (!ok || (hbaPort->is & HBA_PxIS_TFES)) {
            spinlock_release(&this->lock, flags);
            return false;
        } else {
            // Success
        }

        sectors_written += chunk;
    }

    spinlock_release(&this->lock, flags);
    return true;
}