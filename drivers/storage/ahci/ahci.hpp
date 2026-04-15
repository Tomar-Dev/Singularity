// drivers/storage/ahci/ahci.hpp
#ifndef AHCI_HPP
#define AHCI_HPP

#include "system/device/device.h"
#include "drivers/pci/pci.hpp"
#include "drivers/storage/ahci/ahci_structs.h"
#include "drivers/storage/ahci/ahci_port.hpp"
#ifdef __cplusplus

class AHCIDriver : public Device {
private:
    PCIeDevice* pciDev;
    volatile hba_mem_t* abar;
    AHCIPort* ports[32];
    int portCount;
    bool initialized_early;

public:
    AHCIDriver(PCIeDevice* pci);
    ~AHCIDriver();

    int init() override;
    int earlyInit(); 
    int finalize();  
    
    int shutdown() override;
    void emergencyStop() override;
    
    int readBlock(uint64_t lba, uint32_t count, void* buffer) override;
    int writeBlock(uint64_t lba, uint32_t count, const void* buffer) override;
    
    int read(char* buffer, int size) override { (void)buffer; (void)size; return 0; }
    int write(const char* buffer, int size) override { (void)buffer; (void)size; return 0; }
};

#endif

#ifdef __cplusplus
extern "C" {
#endif

void init_ahci(void);
void init_ahci_early(void);
void init_ahci_late(void);

#ifdef __cplusplus
}
#endif

#endif
