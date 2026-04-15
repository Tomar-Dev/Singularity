// drivers/watchdog/wdt.hpp
#ifndef WDT_HPP
#define WDT_HPP

#include "system/device/device.h"
#include "drivers/pci/pci.hpp"
#include <stdint.h>

class WatchdogDriver : public Device {
private:
    PCIeDevice* pciDev;
    uint32_t tco_base;
    bool active;

    void reload(); 

public:
    WatchdogDriver(PCIeDevice* pci);
    ~WatchdogDriver();

    int init() override;
    int shutdown() override;
    
    void start(uint32_t timeout_seconds);
    void stop();
    void kick();
    void force_reset();
};

#ifdef __cplusplus
extern "C" {
#endif

void init_wdt();
void wdt_kick();
void wdt_test_crash();

#ifdef __cplusplus
}
#endif

#endif
