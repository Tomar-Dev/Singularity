// drivers/bus/smbus.cpp
#include "drivers/bus/smbus.hpp"
#include "archs/cpu/cpu_hal.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "drivers/serial/serial.h"
#define SMB_HST_STS   0x00
#define SMB_HST_CNT   0x02
#define SMB_HST_CMD   0x03
#define SMB_XMIT_SLVA 0x04
#define SMB_HST_D0    0x05

static SMBusDriver* g_smbus = nullptr;

extern "C" void print_status(const char* prefix, const char* msg, const char* status);

SMBusDriver::SMBusDriver(PCIeDevice* pci) : Device("Intel SMBus (I2C)", DEV_UNKNOWN), pciDev(pci) {
    io_base = 0;
    spinlock_init(&lock);
}

SMBusDriver::~SMBusDriver() {
    if (g_smbus == this) g_smbus = nullptr;
}

int SMBusDriver::init() {
    pciDev->enableIOSpace();
    
    uint32_t bar4 = pciDev->getBAR(4);
    io_base = bar4 & 0xFFFE;
    
    if (io_base == 0) return 0;
    
    hal_io_outb(io_base + SMB_HST_STS, 0x1E); 
    
    DeviceManager::registerDevice(this);
    g_smbus = this;
    
    char msg[64];
    sprintf(msg, "I2C/SMBus Controller Active (I/O: 0x%X)", io_base);
    print_status("[ BUS  ]", msg, "INFO");
    
    return 1;
}

uint8_t SMBusDriver::readByte(uint8_t device_addr, uint8_t reg_offset) {
    if (io_base == 0) return 0xFF;
    
    uint64_t flags = spinlock_acquire(&lock);

    int timeout = 100000;
    while ((hal_io_inb(io_base + SMB_HST_STS) & 0x01) && timeout--) {
        hal_cpu_relax();
    }
    if (timeout <= 0) {
        hal_io_outb(io_base + SMB_HST_STS, 0xFF);
        spinlock_release(&lock, flags);
        return 0xFF;
    }

    hal_io_outb(io_base + SMB_HST_STS, 0xFE);

    hal_io_outb(io_base + SMB_XMIT_SLVA, (device_addr << 1) | 1);
    
    hal_io_outb(io_base + SMB_HST_CMD, reg_offset);
    
    hal_io_outb(io_base + SMB_HST_CNT, 0x48);

    timeout = 100000;
    while (!(hal_io_inb(io_base + SMB_HST_STS) & 0x02) && timeout--) {
        uint8_t status = hal_io_inb(io_base + SMB_HST_STS);
        if (status & 0x1C) {
            hal_io_outb(io_base + SMB_HST_STS, 0xFF);
            spinlock_release(&lock, flags);
            return 0xFF;
        }
        hal_cpu_relax();
    }

    uint8_t data = hal_io_inb(io_base + SMB_HST_D0);
    hal_io_outb(io_base + SMB_HST_STS, 0xFF);
    
    spinlock_release(&lock, flags);
    return data;
}

extern "C" {
    __attribute__((used, noinline))
    void init_smbus() {
        for (int i = 0; i < PCIe::getDeviceCount(); i++) {
            PCIeDevice* pci = PCIe::getDevice(i);
            if (pci->class_id == 0x0C && pci->subclass_id == 0x05) {
                SMBusDriver* drv = new SMBusDriver(pci);
                if (drv->init()) return;
                delete drv;
            }
        }
    }

    void i2c_dump(uint8_t device_addr) {
        if (!g_smbus) {
            printf("Error: SMBus/I2C Controller is not initialized.\n");
            return;
        }
        
        printf("Dumping 256 bytes from I2C Device 0x%02X:\n", device_addr);
        printf("     00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n");
        printf("----------------------------------------------------\n");
        
        for (int i = 0; i < 16; i++) {
            printf("%02X | ", i * 16);
            for (int j = 0; j < 16; j++) {
                uint8_t val = g_smbus->readByte(device_addr, (i * 16) + j);
                if (val == 0xFF) vga_set_color(8, 0);
                printf("%02X ", val);
                vga_set_color(15, 0);
            }
            printf("\n");
        }
    }
}
