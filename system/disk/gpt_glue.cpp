// system/disk/gpt_glue.cpp
#include "system/device/device.h"
#include "system/device/partition.h"
#include "system/disk/cache.hpp"
#include "libc/string.h"
extern "C" {
    
    void cpp_create_partition_device(void* parent_ptr, const char* name, uint64_t start, uint64_t end, const char* type_name) {
        Device* parent = (Device*)parent_ptr;
        
        if (DeviceManager::getDevice(name)) return;
        
        PartitionDevice* part = new PartitionDevice(name, parent, start, end);
        if (part) {
            part->setGptType(type_name);
            DeviceManager::registerDevice(part);
        }
    }

    void cpp_invalidate_device_cache(void* dev_ptr) {
        Device* dev = (Device*)dev_ptr;
        DiskCache::invalidateDevice(dev);
    }
    
    uint64_t device_get_capacity(void* dev) {
        return ((Device*)dev)->getCapacity();
    }
    
    void device_set_capacity(void* dev, uint64_t cap) {
        ((Device*)dev)->setCapacity(cap);
    }
    
    // FIX: Ensure this is present
    uint64_t device_get_start_lba(void* dev) {
        return ((Device*)dev)->getStartLBA();
    }
}
