// drivers/bus/smbus.hpp
#ifndef SMBUS_HPP
#define SMBUS_HPP

#include "system/device/device.h"
#include "drivers/pci/pci.hpp"
#include "archs/cpu/x86_64/sync/spinlock.h"
#include <stdint.h>

class SMBusDriver : public Device {
private:
    PCIeDevice* pciDev;
    uint32_t io_base;
    spinlock_t lock;

public:
    SMBusDriver(PCIeDevice* pci);
    ~SMBusDriver() override;

    int init() override;
    
    uint8_t readByte(uint8_t device_addr, uint8_t reg_offset);

    int read(char* buffer, int size) override { (void)buffer; (void)size; return 0; }
    int write(const char* buffer, int size) override { (void)buffer; (void)size; return 0; }
};

#ifdef __cplusplus
extern "C" {
#endif

void init_smbus(void);
void i2c_dump(uint8_t device_addr);

#ifdef __cplusplus
}
#endif

#endif
