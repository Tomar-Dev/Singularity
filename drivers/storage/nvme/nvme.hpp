// drivers/storage/nvme/nvme.hpp
#ifndef NVME_HPP
#define NVME_HPP

#include "system/device/device.h"
#include "drivers/pci/pci.hpp"
#include "drivers/storage/nvme/nvme_structs.h"
#include "archs/cpu/x86_64/sync/spinlock.h"

#ifdef __cplusplus

class NVMeDriver : public Device {
private:
    PCIeDevice* pciDev;
    volatile nvme_registers_t* regs;
    
    spinlock_t lock;

    nvme_sq_entry_t* admin_sq;
    nvme_cq_entry_t* admin_cq;
    uint16_t admin_sq_tail;
    uint16_t admin_cq_head;
    bool admin_phase;
    
    nvme_sq_entry_t* io_sq;
    nvme_cq_entry_t* io_cq;
    uint16_t io_sq_tail;
    uint16_t io_cq_head;
    bool io_phase;

    uint32_t db_stride; 
    uint32_t namespace_id; 
    
    bool initialized_early;

    void writeDoorbell(uint16_t index, uint16_t val);
    void waitReady();
    void submitAdminCmd(nvme_sq_entry_t* cmd);
    bool waitForAdminCompletion(uint16_t cid);
    bool createIOQueues(); 
    void submitIOCmd(nvme_sq_entry_t* cmd);
    bool waitForIOCompletion(uint16_t cid);
    
    void unpinBuffer(void* buffer, uint32_t size);
    uint64_t setupPRPs(nvme_sq_entry_t* cmd, void* buffer, uint32_t size, void** out_prp_page_virt);
    void freePRPChain(void* prp_head_virt, uint32_t total_size);

public:
    NVMeDriver(PCIeDevice* pci);
    
    int init() override;
    int earlyInit();
    int finalize();
    
    int shutdown() override;
    void emergencyStop() override;
    
    int readBlock(uint64_t lba, uint32_t count, void* buffer) override;
    int writeBlock(uint64_t lba, uint32_t count, const void* buffer) override;
    
    int read_vector(uint64_t lba, kiovec_t* vectors, int vector_count) override;
    int write_vector(uint64_t lba, kiovec_t* vectors, int vector_count) override;

    int read(char* buffer, int size) override { (void)buffer; (void)size; return 0; }
    int write(const char* buffer, int size) override { (void)buffer; (void)size; return 0; }
};

#endif

#ifdef __cplusplus
extern "C" {
#endif

void init_nvme_early(void);
int  init_nvme_late(void);

#ifdef __cplusplus
}
#endif

#endif