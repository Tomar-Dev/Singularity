// drivers/pci/pci.hpp
#ifndef PCI_HPP
#define PCI_HPP

#include <stdint.h>

#define PCI_CLASS_UNCLASSIFIED 0x00
#define PCI_CLASS_STORAGE      0x01 
#define PCI_CLASS_NETWORK      0x02 
#define PCI_CLASS_DISPLAY      0x03 
#define PCI_CLASS_MULTIMEDIA   0x04 
#define PCI_CLASS_MEMORY       0x05 
#define PCI_CLASS_BRIDGE       0x06 
#define PCI_CLASS_COMM         0x07 
#define PCI_CLASS_PERIPHERAL   0x08
#define PCI_CLASS_INPUT        0x09 
#define PCI_CLASS_DOCKING      0x0A
#define PCI_CLASS_PROCESSOR    0x0B
#define PCI_CLASS_SERIAL       0x0C 

#define PCI_SUBCLASS_IDE       0x01
#define PCI_SUBCLASS_AHCI      0x06 
#define PCI_SUBCLASS_NVME      0x08 
#define PCI_SUBCLASS_USB       0x03 

// OPTİMİZASYON YAMASI: FFI Struct Marshalling için eklendi
struct FfiPcieDevice {
    uint8_t bus, dev, func, class_id, subclass_id, prog_if;
    uint16_t vendor_id, device_id;
    bool valid;
};

class PCIeDevice {
public:
    uint8_t bus;
    uint8_t device;
    uint8_t function;

    uint16_t vendor_id;
    uint16_t device_id;
    
    uint8_t class_id;
    uint8_t subclass_id;
    uint8_t prog_if;
    uint8_t revision;

    uint8_t header_type;
    uint8_t latency_timer;
    uint8_t cache_line_size;
    uint8_t bist;

    uint16_t command;
    uint16_t status;
    
    uint8_t interrupt_line;
    uint8_t interrupt_pin;
    
    uint8_t pm_cap_offset; 
    uint8_t msi_cap_offset; 
    uint16_t aer_offset; 

    PCIeDevice(uint8_t b, uint8_t d, uint8_t f, uint16_t vid, uint16_t did, uint8_t cid, uint8_t scid, uint8_t pif);
    
    uint16_t readWord(uint16_t offset);
    uint32_t readDWord(uint16_t offset);
    uint8_t  readByte(uint16_t offset);
    
    void writeWord(uint16_t offset, uint16_t value);
    void writeDWord(uint16_t offset, uint32_t value);
    void writeByte(uint16_t offset, uint8_t value);
    
    uint32_t getBAR(uint8_t index);
    void enableBusMaster();
    void enableMemorySpace();
    void enableIOSpace();
};

namespace PCIe {
    int getDeviceCount();
    PCIeDevice* getDevice(int index);
}

#endif