// drivers/pci/pci.cpp
#include "drivers/pci/pci.hpp"
#include "archs/cpu/cpu_hal.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "drivers/serial/serial.h"

extern "C" {
    uint32_t rust_pcie_read_dword(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset);
    void rust_pcie_write_dword(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset, uint32_t val);
    
    // FIX: Proper 16-bit and 8-bit FFI access for W1C safety
    uint16_t rust_pcie_read_word(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset);
    void rust_pcie_write_word(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset, uint16_t val);
    uint8_t rust_pcie_read_byte(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset);
    void rust_pcie_write_byte(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset, uint8_t val);
    
    uint32_t rust_pcie_get_device_count();
    FfiPcieDevice rust_pcie_get_device_info(uint32_t index);
}

PCIeDevice::PCIeDevice(uint8_t b, uint8_t d, uint8_t f, uint16_t vid, uint16_t did, uint8_t cid, uint8_t scid, uint8_t pif)
    : bus(b), device(d), function(f), vendor_id(vid), device_id(did), 
      class_id(cid), subclass_id(scid), prog_if(pif),
      pm_cap_offset(0), msi_cap_offset(0), aer_offset(0)
{
    uint32_t reg3 = readDWord(0x0C);
    cache_line_size = reg3 & 0xFF;
    latency_timer = (reg3 >> 8) & 0xFF;
    header_type = (reg3 >> 16) & 0xFF;
    bist = (reg3 >> 24) & 0xFF;

    if ((header_type & 0x7F) == 0) {
        uint32_t reg15 = readDWord(0x3C);
        interrupt_line = reg15 & 0xFF;
        interrupt_pin = (reg15 >> 8) & 0xFF;
    } else {
        interrupt_line = 0;
        interrupt_pin = 0;
    }
}

uint32_t PCIeDevice::readDWord(uint16_t offset) { return rust_pcie_read_dword(bus, device, function, offset); }
uint16_t PCIeDevice::readWord(uint16_t offset) { return rust_pcie_read_word(bus, device, function, offset); }
uint8_t PCIeDevice::readByte(uint16_t offset) { return rust_pcie_read_byte(bus, device, function, offset); }

void PCIeDevice::writeDWord(uint16_t offset, uint32_t value) {
    rust_pcie_write_dword(bus, device, function, offset, value);
}

void PCIeDevice::writeWord(uint16_t offset, uint16_t value) {
    rust_pcie_write_word(bus, device, function, offset, value);
}

void PCIeDevice::writeByte(uint16_t offset, uint8_t value) {
    rust_pcie_write_byte(bus, device, function, offset, value);
}

uint32_t PCIeDevice::getBAR(uint8_t index) {
    if ((header_type & 0x7F) != 0 || index > 5) return 0;
    return readDWord(0x10 + (index * 4));
}

void PCIeDevice::enableBusMaster() {
    uint16_t cmd = readWord(0x04);
    if (!(cmd & (1 << 2))) {
        cmd |= (1 << 2);
        writeWord(0x04, cmd);
    } else {}
}

void PCIeDevice::enableMemorySpace() {
    uint16_t cmd = readWord(0x04);
    if (!(cmd & (1 << 1))) { cmd |= (1 << 1); writeWord(0x04, cmd); } else {}
}

void PCIeDevice::enableIOSpace() {
    uint16_t cmd = readWord(0x04);
    if (!(cmd & (1 << 0))) { cmd |= (1 << 0); writeWord(0x04, cmd); } else {}
}

namespace PCIe {
    static PCIeDevice* device_roster[256] = {0};

    int getDeviceCount() { return (int)rust_pcie_get_device_count(); }
    
    PCIeDevice* getDevice(int index) {
        if (index < 0 || index >= 256) return nullptr;
        if (device_roster[index]) return device_roster[index];
        
        FfiPcieDevice info = rust_pcie_get_device_info((uint32_t)index);
        if (info.valid) {
            device_roster[index] = new PCIeDevice(info.bus, info.dev, info.func, 
                                                  info.vendor_id, info.device_id, 
                                                  info.class_id, info.subclass_id, info.prog_if);
            return device_roster[index];
        } else {
            return nullptr;
        }
    }
}