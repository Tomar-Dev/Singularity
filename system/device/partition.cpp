// system/device/partition.cpp
#include "system/device/partition.h"
#include "libc/stdio.h"
// FIX: Include yolu 'common/' dizinine uyarlandı
#include "archs/kom/common/provider_glue.hpp"

PartitionDevice::PartitionDevice(const char* name, Device* parent, uint64_t start, uint64_t end)
    : Device(name, DEV_PARTITION), parentDisk(parent), startLba(start)
{
    if (end >= start) {
        this->sectorCount = end - start + 1;
        this->setCapacity(this->sectorCount * 512);
    } else {
        this->sectorCount = 0;
        this->setCapacity(0);
    }
    gptType[0] = '\0';
}

PartitionDevice::~PartitionDevice() {
}

int PartitionDevice::readBlock(uint64_t lba, uint32_t count, void* buffer) {
    if (lba + count < lba) return 0; 
    if (lba + count > sectorCount) return 0;
    return parentDisk->readBlock(startLba + lba, count, buffer);
}

int PartitionDevice::writeBlock(uint64_t lba, uint32_t count, const void* buffer) {
    if (this->isWriteProtected()) {
        printf("[PART] Blocked write to partition '%s' (Device Lockdown Active).\n", this->getName());
        return 0;
    }

    if (lba + count < lba) return 0;
    if (lba + count > sectorCount) return 0;
    return parentDisk->writeBlock(startLba + lba, count, buffer);
}

int PartitionDevice::read_vector(uint64_t lba, kiovec_t* vectors, int vector_count) {
    uint32_t total_blocks = 0;
    uint32_t bs = getBlockSize();
    if (bs == 0) return 0;
    
    for(int i = 0; i < vector_count; i++) {
        total_blocks += vectors[i].size / bs;
    }

    if (lba + total_blocks < lba) return 0;
    if (lba + total_blocks > sectorCount) return 0;
    
    return parentDisk->read_vector(startLba + lba, vectors, vector_count);
}

int PartitionDevice::write_vector(uint64_t lba, kiovec_t* vectors, int vector_count) {
    if (this->isWriteProtected()) {
        printf("[PART] Blocked vector write to partition '%s' (Lockdown Active).\n", this->getName());
        return 0;
    }

    uint32_t total_blocks = 0;
    uint32_t bs = getBlockSize();
    if (bs == 0) return 0;

    for(int i = 0; i < vector_count; i++) {
        total_blocks += vectors[i].size / bs;
    }

    if (lba + total_blocks < lba) return 0;
    if (lba + total_blocks > sectorCount) return 0;
    
    return parentDisk->write_vector(startLba + lba, vectors, vector_count);
}

uint64_t PartitionDevice::getFreeSpace() {
    return kom_probe_free_space(this); 
}