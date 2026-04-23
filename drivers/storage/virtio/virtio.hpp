// drivers/storage/virtio/virtio.hpp
#ifndef VIRTIO_HPP
#define VIRTIO_HPP

#include "system/device/device.h"
#include "drivers/pci/pci.hpp"
#include "archs/cpu/x86_64/sync/spinlock.h"
#include <stdint.h>

#define VIRTIO_VENDOR_ID 0x1AF4
#define VIRTIO_DEV_BLK   0x1001

#define VIRTIO_REG_DEVICE_FEATURES 0x00
#define VIRTIO_REG_GUEST_FEATURES  0x04
#define VIRTIO_REG_QUEUE_ADDRESS   0x08
#define VIRTIO_REG_QUEUE_SIZE      0x0C
#define VIRTIO_REG_QUEUE_SELECT    0x0E
#define VIRTIO_REG_QUEUE_NOTIFY    0x10
#define VIRTIO_REG_DEVICE_STATUS   0x12
#define VIRTIO_REG_ISR_STATUS      0x13
#define VIRTIO_REG_DEVICE_CONFIG   0x14 

#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2
#define VIRTIO_PCI_CAP_ISR_CFG    3
#define VIRTIO_PCI_CAP_DEVICE_CFG 4 
#define VIRTIO_PCI_CAP_PCI_CFG    5

struct virtio_pci_common_cfg {
    uint32_t device_feature_select;
    uint32_t device_feature;
    uint32_t guest_feature_select;
    uint32_t guest_feature;
    uint16_t msix_config;
    uint16_t num_queues;
    uint8_t  device_status;
    uint8_t  config_generation;
    uint16_t queue_select;
    uint16_t queue_size;
    uint16_t queue_msix_vector;
    uint16_t queue_enable;
    uint16_t queue_notify_off;
    uint32_t queue_desc_lo;
    uint32_t queue_desc_hi;
    uint32_t queue_avail_lo;
    uint32_t queue_avail_hi;
    uint32_t queue_used_lo;
    uint32_t queue_used_hi;
} __attribute__((packed));

#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER      2
#define VIRTIO_STATUS_DRIVER_OK   4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_FAILED      128

#define VRING_DESC_F_NEXT      1
#define VRING_DESC_F_WRITE     2
#define VRING_DESC_F_INDIRECT  4

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[]; 
} __attribute__((packed));

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[]; 
} __attribute__((packed));

struct virtio_blk_req_header {
    uint32_t type;
    uint32_t priority;
    uint64_t sector;
} __attribute__((packed));

#define VIRTIO_BLK_T_IN  0
#define VIRTIO_BLK_T_OUT 1

class VirtIODriver : public Device {
private:
    PCIeDevice* pciDev;
    uint16_t io_base;
    
    bool is_modern;
    volatile virtio_pci_common_cfg* common_cfg;
    volatile uint8_t* isr_cfg;
    uint16_t notify_off_multiplier;
    uint64_t notify_base;
    
    void* queue_virt;
    uint64_t queue_phys;
    uint16_t queue_size; 
    
    struct virtq_desc* desc;
    struct virtq_avail* avail;
    struct virtq_used* used;
    
    uint16_t free_head;
    uint16_t last_used_idx;
    uint16_t free_desc_count;
    
    spinlock_t io_lock;

    void mapCapability(uint8_t type, void** virt_addr, uint64_t* phys_addr, uint32_t* length);
    
    void write8(uint16_t offset, uint8_t val);
    void write16(uint16_t offset, uint16_t val);
    void write32(uint16_t offset, uint32_t val);
    uint8_t read8(uint16_t offset);
    uint16_t read16(uint16_t offset);
    uint32_t read32(uint16_t offset);
    
    void notifyQueue(uint16_t queue_index);

public:
    VirtIODriver(PCIeDevice* pci);
    ~VirtIODriver();

    int init() override;
    void emergencyStop() override;
    
    int readBlock(uint64_t lba, uint32_t count, void* buffer) override;
    int writeBlock(uint64_t lba, uint32_t count, const void* buffer) override;
    
    int read(char* buffer, int size) override { (void)buffer; (void)size; return 0; }
    int write(const char* buffer, int size) override { (void)buffer; (void)size; return 0; }
};

#ifdef __cplusplus
extern "C" {
#endif

void init_virtio();

#ifdef __cplusplus
}
#endif

#endif