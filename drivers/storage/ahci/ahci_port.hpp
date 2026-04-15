// drivers/storage/ahci/ahci_port.hpp
#ifndef AHCI_PORT_HPP
#define AHCI_PORT_HPP

#include "drivers/storage/ahci/ahci_structs.h"
#include "archs/cpu/x86_64/sync/spinlock.h"
#include <stdint.h>

extern "C" void kfree_contiguous(void* ptr, size_t size);

class AHCIPort {
public:
    int id;
    volatile hba_port_t* hbaPort;
    volatile hba_mem_t* hbaMem;

    spinlock_t lock;

    void* clb_virt;
    void* fb_virt;
    void* ctba_virt[32];

    uint64_t sector_count;
    bool is_atapi;
    uint8_t atapi_packet_size;
    char model_name[41];

    AHCIPort()
        : id(0), hbaPort(nullptr), hbaMem(nullptr),
          clb_virt(nullptr), fb_virt(nullptr),
          sector_count(0), is_atapi(false), atapi_packet_size(12)
    {
        spinlock_init(&lock);
        model_name[0] = '\0';
        for (int i = 0; i < 32; i++) {
            ctba_virt[i] = nullptr;
        }
    }

    ~AHCIPort() {
        freeResources();
    }

    void freeResources() {
        if (clb_virt) {
            kfree_contiguous(clb_virt, 4096);
            clb_virt  = nullptr;
            fb_virt   = nullptr;
        }
        for (int i = 0; i < 32; i++) {
            if (ctba_virt[i]) {
                kfree_contiguous(ctba_virt[i], 4096);
                ctba_virt[i] = nullptr;
            }
        }
    }

    void startCmd();
    void stopCmd();
    void reset();
    int  configure();

    bool identify();

    void unpinBuffer(void* buffer, uint32_t byte_count);
    int  fillPrdt(hba_cmd_tbl_t* cmdtbl, void* buffer, uint32_t byte_count);

    bool read(uint64_t sector, uint32_t count, void* buffer);
    bool write(uint64_t sector, uint32_t count, const void* buffer);

    bool scsi_packet(uint8_t* packet, uint32_t packet_len,
                     void* buffer, uint32_t transfer_size, bool write);
    bool atapi_read(uint64_t sector, uint32_t count, void* buffer);
    bool atapi_read_capacity();

    int findCmdSlot();
};

#endif