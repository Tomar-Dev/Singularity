// drivers/watchdog/wdt.cpp
#include "drivers/watchdog/wdt.hpp"
#include "archs/cpu/cpu_hal.h"
#include "libc/stdio.h"
#include "drivers/serial/serial.h"
#include "system/process/process.h"
#include "system/power/power.h" 
extern "C" void timer_sleep(uint64_t ticks);
extern "C" void print_status(const char* prefix, const char* msg, const char* status);

#define TCO_RLD           0x00 
#define TCO1_DAT_IN       0x02 
#define TCO1_DAT_OUT      0x03 
#define TCO1_STS          0x04 
#define TCO2_STS          0x06 
#define TCO1_CNT          0x08 
#define TCO2_CNT          0x0A 
#define TCO_TMR           0x12 

#define LPC_ACPI_BASE     0x40 
#define LPC_ACPI_CTRL     0x44 
#define LPC_TCO_BASE      0x50 
#define LPC_GEN_PMCON_3   0xD4 

static WatchdogDriver* g_wdt = nullptr;

WatchdogDriver::WatchdogDriver(PCIeDevice* pci) 
    : Device("iTCO Watchdog", DEV_UNKNOWN), pciDev(pci) 
{
    tco_base = 0;
    active = false;
}

WatchdogDriver::~WatchdogDriver() {
    if (g_wdt == this) g_wdt = nullptr;
}

int WatchdogDriver::init() {
    uint32_t tco_reg = pciDev->readDWord(LPC_TCO_BASE);
    if ((tco_reg & 0xFFFFFFF0) != 0) {
        tco_base = tco_reg & 0xFFFFFFF0;
        pciDev->writeDWord(LPC_TCO_BASE, tco_reg | 1); 
    } else {
        uint32_t pmbase = pciDev->readDWord(LPC_ACPI_BASE) & 0xFF80;
        if (pmbase != 0) {
            tco_base = pmbase + 0x60;
            uint32_t smi_en = hal_io_inl(pmbase + 0x30);
            if (smi_en & (1 << 13)) {
                hal_io_outl(pmbase + 0x30, smi_en & ~(1 << 13));
            }
        }
    }

    if (tco_base == 0) return 0;
    
    uint8_t acpi_cntl = pciDev->readByte(LPC_ACPI_CTRL);
    if (!(acpi_cntl & 0x10)) pciDev->writeByte(LPC_ACPI_CTRL, acpi_cntl | 0x10);
    
    // GÜVENLİK YAMASI: NO_REBOOT Hardware Lock Check
    uint8_t pmcon3 = pciDev->readByte(LPC_GEN_PMCON_3);
    if (pmcon3 & 0x02) {
        pciDev->writeByte(LPC_GEN_PMCON_3, pmcon3 & ~0x02);
        if (pciDev->readByte(LPC_GEN_PMCON_3) & 0x02) {
            serial_write("[WDT] WARNING: NO_REBOOT bit is hard-locked by BIOS. Watchdog cannot reset system!\n");
        }
    }
    
    uint16_t sts1 = hal_io_inw(tco_base + TCO1_STS);
    if (sts1 & (1 << 3)) {
        serial_write("[WDT] System was reset by Watchdog! (Previous Boot)\n");
        hal_io_outw(tco_base + TCO1_STS, (1 << 3)); 
    }
    
    uint16_t sts2 = hal_io_inw(tco_base + TCO2_STS);
    if (sts2 & (1 << 1)) hal_io_outw(tco_base + TCO2_STS, (1 << 1));
    
    DeviceManager::registerDevice(this);
    g_wdt = this; 
    
    start(30);
    return 1;
}

void WatchdogDriver::reload() { hal_io_outb(tco_base + TCO_RLD, 0); }

void WatchdogDriver::start(uint32_t timeout_seconds) {
    if (tco_base == 0) return;
    
    uint16_t ticks = (timeout_seconds * 10) / 6;
    if (ticks > 63) ticks = 63;
    if (ticks == 0) ticks = 2; 
    if (ticks < 2) ticks = 2;
    
    uint16_t cnt_stop = hal_io_inw(tco_base + TCO1_CNT);
    cnt_stop |= (1 << 11);
    hal_io_outw(tco_base + TCO1_CNT, cnt_stop);
    
    uint8_t tmr = hal_io_inb(tco_base + TCO_TMR);
    tmr &= 0xC0;
    tmr |= (ticks & 0x3F);
    hal_io_outb(tco_base + TCO_TMR, tmr);
    
    reload();

    uint16_t cnt1 = hal_io_inw(tco_base + TCO1_CNT);
    cnt1 &= ~(1 << 11); 
    hal_io_outw(tco_base + TCO1_CNT, cnt1);
    
    active = true;
}

void WatchdogDriver::stop() {
    if (tco_base == 0) return;
    uint16_t cnt1 = hal_io_inw(tco_base + TCO1_CNT);
    cnt1 |= (1 << 11);
    hal_io_outw(tco_base + TCO1_CNT, cnt1);
    active = false;
}

void WatchdogDriver::kick() { if (active) reload(); }

void WatchdogDriver::force_reset() {
    start(1); 
    serial_write("[WDT] Forcing Reset via Watchdog Timeout (Wait ~2-3 seconds)...\n");
    hal_interrupts_disable();
    while(1) { hal_cpu_halt(); }
}

int WatchdogDriver::shutdown() {
    stop();
    print_shutdown("Watchdog timer disabled");
    return 1;
}

void watchdog_service_task() {
    while (1) {
        if (g_wdt) g_wdt->kick();
        timer_sleep(25); 
    }
}

extern "C" {
    void init_wdt() {
        for (int i = 0; i < PCIe::getDeviceCount(); i++) {
            PCIeDevice* pci = PCIe::getDevice(i);
            if (pci->vendor_id == 0x8086 && pci->class_id == 0x06 && pci->subclass_id == 0x01) {
                WatchdogDriver* wdt = new WatchdogDriver(pci);
                if (wdt->init()) {
                    create_kernel_task_prio(watchdog_service_task, PRIO_REALTIME); 
                    return; 
                }
                delete wdt;
            }
        }
    }
    void wdt_kick() { if (g_wdt) g_wdt->kick(); }
    void wdt_test_crash() {
        if (g_wdt) g_wdt->force_reset();
        else printf("Watchdog not active.\n");
    }
}