// drivers/pci/pci.cpp
#include "drivers/pci/pci.hpp"
#include "archs/cpu/cpu_hal.h"
#include "libc/stdio.h"
#include "libc/string.h"

extern "C" {
    uint32_t rust_pcie_read_dword(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset);
    void rust_pcie_write_dword(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset, uint32_t val);
    
    uint16_t rust_pcie_read_word(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset);
    void rust_pcie_write_word(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset, uint16_t val);
    uint8_t rust_pcie_read_byte(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset);
    void rust_pcie_write_byte(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset, uint8_t val);
    
    uint32_t rust_pcie_get_device_count();
    FfiPcieDevice rust_pcie_get_device_info(uint32_t index);
    
    extern void* ioremap(uint64_t phys, uint32_t size, uint64_t flags);
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
    if ((header_type & 0x7F) != 0 || index > 5) { return 0; }
    return readDWord(0x10 + (index * 4));
}

void PCIeDevice::enableBusMaster() {
    uint16_t cmd = readWord(0x04);
    if (!(cmd & (1 << 2))) {
        cmd |= (1 << 2);
        writeWord(0x04, cmd);
    } else {
        serial_printf("[PCIe] Note: Device %04x:%04x already has Bus Master enabled.\n", vendor_id, device_id);
    }
}

void PCIeDevice::enableMemorySpace() {
    uint16_t cmd = readWord(0x04);
    if (!(cmd & (1 << 1))) { 
        cmd |= (1 << 1); 
        writeWord(0x04, cmd); 
    } else {
        serial_printf("[PCIe] Note: Device %04x:%04x already has Memory Space enabled.\n", vendor_id, device_id);
    }
}

void PCIeDevice::enableIOSpace() {
    uint16_t cmd = readWord(0x04);
    if (!(cmd & (1 << 0))) { 
        cmd |= (1 << 0); 
        writeWord(0x04, cmd); 
    } else {
        serial_printf("[PCIe] Note: Device %04x:%04x already has I/O Space enabled.\n", vendor_id, device_id);
    }
}

// YENİ: MSI / MSI-X Donanım Konfigürasyonu
bool PCIeDevice::enable_msi(uint8_t vector, uint8_t cpu_id) {
    uint16_t status = readWord(0x06);
    if (!(status & (1 << 4))) return false; // Capabilities list not supported

    uint8_t cap_offset = readByte(0x34);
    bool msi_found = false;
    bool msix_found = false;
    uint8_t msi_ptr = 0;
    uint8_t msix_ptr = 0;

    while (cap_offset != 0 && cap_offset != 0xFF) {
        uint8_t cap_id = readByte(cap_offset);
        if (cap_id == 0x05) { msi_found = true; msi_ptr = cap_offset; }
        if (cap_id == 0x11) { msix_found = true; msix_ptr = cap_offset; }
        cap_offset = readByte(cap_offset + 1);
    }

    // Local APIC Adresi (0xFEE00000) ve Hedef CPU
    uint64_t apic_addr = 0xFEE00000 | ((uint64_t)cpu_id << 12);
    uint32_t apic_data = vector;

    if (msix_found) {
        uint16_t msg_ctrl = readWord(msix_ptr + 2);
        uint32_t table_info = readDWord(msix_ptr + 4);
        uint8_t bir = table_info & 0x7;
        uint32_t table_offset = table_info & ~0x7;

        uint32_t bar_val = getBAR(bir);
        uint64_t bar_phys = bar_val & 0xFFFFFFF0;
        if ((bar_val & 0x4) == 0x4) {
            bar_phys |= ((uint64_t)getBAR(bir + 1) << 32);
        }

        uint64_t table_phys = bar_phys + table_offset;
        volatile uint32_t* msix_table = (volatile uint32_t*)ioremap(table_phys, 4096, 0x13); // PAGE_PRESENT | PAGE_WRITE | PAGE_PCD
        
        if (msix_table) {
            msix_table[0] = (uint32_t)(apic_addr & 0xFFFFFFFF);
            msix_table[1] = (uint32_t)(apic_addr >> 32);
            msix_table[2] = apic_data;
            msix_table[3] = 0; // Unmask

            msg_ctrl |= (1 << 15); // Enable MSI-X
            writeWord(msix_ptr + 2, msg_ctrl);
            
            uint16_t cmd = readWord(0x04);
            writeWord(0x04, cmd | (1 << 10)); // Disable Legacy INTx
            
            serial_printf("[PCIe] MSI-X Enabled for %04x:%04x (Vector: %d)\n", vendor_id, device_id, vector);
            return true;
        }
    } 
    
    if (msi_found) {
        uint16_t msg_ctrl = readWord(msi_ptr + 2);
        bool is_64bit = (msg_ctrl & (1 << 7)) != 0;

        writeDWord(msi_ptr + 4, (uint32_t)(apic_addr & 0xFFFFFFFF));
        if (is_64bit) {
            writeDWord(msi_ptr + 8, (uint32_t)(apic_addr >> 32));
            writeWord(msi_ptr + 12, apic_data);
        } else {
            writeWord(msi_ptr + 8, apic_data);
        }

        msg_ctrl |= 1; // Enable MSI
        writeWord(msi_ptr + 2, msg_ctrl);
        
        uint16_t cmd = readWord(0x04);
        writeWord(0x04, cmd | (1 << 10)); // Disable Legacy INTx

        serial_printf("[PCIe] MSI Enabled for %04x:%04x (Vector: %d)\n", vendor_id, device_id, vector);
        return true;
    }

    return false;
}

namespace PCIe {
    static PCIeDevice* device_roster[256] = {0};

    int getDeviceCount() { return (int)rust_pcie_get_device_count(); }
    
    PCIeDevice* getDevice(int index) {
        if (index < 0 || index >= 256) { return nullptr; }
        if (device_roster[index]) { return device_roster[index]; }
        
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

extern "C" {
    // Rust xHCI sürücüsü için FFI Köprüsü
    bool rust_pcie_enable_msi(uint8_t bus, uint8_t dev, uint8_t func, uint8_t vector, uint8_t cpu_id) {
        for (int i = 0; i < 256; i++) {
            PCIeDevice* p = PCIe::getDevice(i);
            if (p && p->bus == bus && p->device == dev && p->function == func) {
                return p->enable_msi(vector, cpu_id);
            }
        }
        return false;
    }
}