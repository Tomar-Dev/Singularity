// system/device/partition.h
#ifndef PARTITION_H
#define PARTITION_H

#include "system/device/device.h"
#include "libc/string.h"

class PartitionDevice : public Device {
private:
    Device* parentDisk; 
    uint64_t startLba;  
    uint64_t sectorCount;
    char gptType[16]; 

public:
    PartitionDevice(const char* name, Device* parent, uint64_t start, uint64_t end);
    ~PartitionDevice();

    int init() override { return 1; }

    int readBlock(uint64_t lba, uint32_t count, void* buffer) override;
    int writeBlock(uint64_t lba, uint32_t count, const void* buffer) override;
    
    int read_vector(uint64_t lba, kiovec_t* vectors, int vector_count) override;
    int write_vector(uint64_t lba, kiovec_t* vectors, int vector_count) override;
    
    void unlockWrite(uint32_t magic) {
        Device::unlockWrite(magic);
        if (parentDisk) { parentDisk->unlockWrite(magic); } else { /* Virtual partition mapped */ }
    }
    
    void lockWrite() {
        Device::lockWrite();
        if (parentDisk) { parentDisk->lockWrite(); } else { /* Virtual partition mapped */ }
    }
    
    uint64_t getStartLBA() const override { return startLba; }
    uint64_t getSizeSectors() const { return sectorCount; }
    
    // BUG-005 FIX: Unchecked NULL pointer
    const char* getParentName() const { 
        if (parentDisk) { return parentDisk->getName(); } 
        else { return "Unknown"; } 
    }
    
    void setGptType(const char* type) { 
        strncpy(gptType, type, 15); 
        gptType[15] = '\0';
    }
    const char* getGptType() const { return gptType; }
    uint32_t getBlockSize() const override { 
        if (parentDisk) { return parentDisk->getBlockSize(); } 
        else { return 512; } 
    }

    uint64_t getFreeSpace() override;
};

#endif