// system/disk/cache.cpp
#include "system/disk/cache.hpp"
#include "libc/string.h"
#include "libc/stdio.h"
#include "memory/kheap.h" 
#include "system/process/process.h" 
#include "system/power/power.h"     
#include "archs/cpu/cpu_hal.h"
#include "memory/paging.h" 
#include "archs/storage/storage_hal.h" 

extern "C" {
    void print_status(const char* prefix, const char* msg, const char* status);
    uint64_t get_physical_address(uint64_t virt);
}

CacheSet DiskCache::sets[CACHE_SETS];

uint32_t DiskCache::hash(Device* dev, uint64_t lba) {
    uint32_t hash = 2166136261u;
    uint64_t dev_val = (uint64_t)dev;
    
    for (int i = 0; i < 8; i++) {
        hash ^= (dev_val & 0xFF);
        hash *= 16777619;
        dev_val >>= 8;
    }
    for (int i = 0; i < 8; i++) {
        hash ^= (lba & 0xFF);
        hash *= 16777619;
        lba >>= 8;
    }
    return hash & (CACHE_SETS - 1);
}

void DiskCache::init() {
    for (int i = 0; i < CACHE_SETS; i++) {
        spinlock_init(&sets[i].lock);
        for (int w = 0; w < CACHE_WAYS; w++) {
            sets[i].ways[w].valid = false;
            sets[i].ways[w].last_access = 0;
        }
    }
}

int DiskCache::readBlock(Device* dev, uint64_t lba, uint32_t count, void* buffer) {
    if (dev->getBlockSize() != 512 || count > 1 || (((uint64_t)buffer & 0x1FF) != 0)) {
        // Bypass durumunda okunan LBA'ların çöp kalma riski yoktur ancak
        // yine de defansif olarak geçelim.
        return dev->readBlock(lba, count, buffer);
    } else {
        // Standard cache hit mechanics
    }

    uint8_t* ptr = (uint8_t*)buffer;
    uint8_t* bounce_buf = nullptr;

    for (uint32_t i = 0; i < count; i++) {
        uint64_t current_lba = lba + i;
        uint32_t set_idx = hash(dev, current_lba);
        CacheSet* set = &sets[set_idx];

        bool found = false;
        
        uint64_t flags = spinlock_acquire(&set->lock);
        for (int w = 0; w < CACHE_WAYS; w++) {
            if (set->ways[w].valid && set->ways[w].device == dev && set->ways[w].lba == current_lba) {
                set->ways[w].last_access = hal_timer_get_ticks();
                
                uint64_t start_addr = (uint64_t)ptr + ((uint64_t)i * 512);
                uint64_t end_addr = start_addr + 511;
                if (get_physical_address(start_addr) == 0 || 
                   ((start_addr & ~0xFFFULL) != (end_addr & ~0xFFFULL) && get_physical_address(end_addr) == 0)) {
                    spinlock_release(&set->lock, flags);
                    serial_write("[CACHE] TOCTOU Violation: Target memory unmapped during cache read!\n");
                    return 0;
                } else {
                    // Safe to proceed
                }

                memcpy((void*)start_addr, set->ways[w].data, 512);
                found = true;
                break;
            } else {
                // Not the target cache line
            }
        }
        spinlock_release(&set->lock, flags);

        if (found) { continue; } else { /* Proceed to fetch */ }

        if (!bounce_buf) {
            bounce_buf = (uint8_t*)kmalloc_contiguous(4096);
            if (!bounce_buf) { return 0; } else { /* Allocated */ }
        } else {
            // Buffer already initialized
        }
        memset(bounce_buf, 0, 512);
        
        if (!dev->readBlock(current_lba, 1, bounce_buf)) {
            kfree_contiguous(bounce_buf, 4096);
            return 0;
        } else {
            // Successfully fetched from physical device
        }

        uint64_t start_addr = (uint64_t)ptr + ((uint64_t)i * 512);
        uint64_t end_addr = start_addr + 511;
        if (get_physical_address(start_addr) == 0 || 
           ((start_addr & ~0xFFFULL) != (end_addr & ~0xFFFULL) && get_physical_address(end_addr) == 0)) {
            kfree_contiguous(bounce_buf, 4096);
            serial_write("[CACHE] TOCTOU Violation: Target memory unmapped after disk read!\n");
            return 0;
        } else {
            // Safely write to target memory
        }
        
        memcpy((void*)start_addr, bounce_buf, 512);

        flags = spinlock_acquire(&set->lock);
        
        int victim_way = 0;
        uint64_t oldest_time = 0xFFFFFFFFFFFFFFFFULL;

        for (int w = 0; w < CACHE_WAYS; w++) {
            if (!set->ways[w].valid) {
                victim_way = w;
                break;
            } else {
                if (set->ways[w].last_access < oldest_time) {
                    oldest_time = set->ways[w].last_access;
                    victim_way = w;
                } else {
                    // Current is newer
                }
            }
        }

        set->ways[victim_way].valid = true;
        set->ways[victim_way].device = dev;
        set->ways[victim_way].lba = current_lba;
        set->ways[victim_way].last_access = hal_timer_get_ticks();
        memcpy(set->ways[victim_way].data, bounce_buf, 512);

        spinlock_release(&set->lock, flags);
    }

    if (bounce_buf) { kfree_contiguous(bounce_buf, 4096); } else { /* Clean */ }
    return 1;
}

int DiskCache::writeBlock(Device* dev, uint64_t lba, uint32_t count, const void* buffer) {
    if (dev->getBlockSize() != 512 || count > 1 || (((uint64_t)buffer & 0x1FF) != 0)) {
        // BUG-002 FIX: Invalidate cache before bypass write to prevent stale data!
        for (uint32_t i = 0; i < count; i++) {
            uint32_t set_idx = hash(dev, lba + i);
            CacheSet* set = &sets[set_idx];
            uint64_t flags = spinlock_acquire(&set->lock);
            for (int w = 0; w < CACHE_WAYS; w++) {
                if (set->ways[w].valid && set->ways[w].device == dev && set->ways[w].lba == lba + i) {
                    set->ways[w].valid = false; // Mark dirty line as destroyed
                } else {
                    // Ignore irrelevant data
                }
            }
            spinlock_release(&set->lock, flags);
        }
        return dev->writeBlock(lba, count, buffer);
    } else {
        // Normal cache write sequence
    }

    const uint8_t* ptr = (const uint8_t*)buffer;
    uint8_t* bounce_buf = nullptr;

    for (uint32_t i = 0; i < count; i++) {
        uint64_t current_lba = lba + i;
        
        if (!bounce_buf) {
            bounce_buf = (uint8_t*)kmalloc_contiguous(4096);
            if (!bounce_buf) { return 0; } else { /* Allocated */ }
        } else {
            // Buffer already initialized
        }

        uint64_t start_addr = (uint64_t)ptr + ((uint64_t)i * 512);
        uint64_t end_addr = start_addr + 511;
        if (get_physical_address(start_addr) == 0 || 
           ((start_addr & ~0xFFFULL) != (end_addr & ~0xFFFULL) && get_physical_address(end_addr) == 0)) {
            kfree_contiguous(bounce_buf, 4096);
            serial_write("[CACHE] TOCTOU Violation: Source memory unmapped during cache write!\n");
            return 0;
        } else {
            // Safely proceed
        }

        memcpy(bounce_buf, (void*)start_addr, 512);

        if (!dev->writeBlock(current_lba, 1, bounce_buf)) {
            kfree_contiguous(bounce_buf, 4096);
            return 0;
        } else {
            // Flushed to disk safely
        }

        uint32_t set_idx = hash(dev, current_lba);
        CacheSet* set = &sets[set_idx];

        uint64_t flags = spinlock_acquire(&set->lock);
        
        bool updated = false;
        for (int w = 0; w < CACHE_WAYS; w++) {
            if (set->ways[w].valid && set->ways[w].device == dev && set->ways[w].lba == current_lba) {
                set->ways[w].last_access = hal_timer_get_ticks();
                memcpy(set->ways[w].data, bounce_buf, 512);
                updated = true;
                break;
            } else {
                // Not target or invalid
            }
        }

        if (!updated) {
            int victim_way = 0;
            uint64_t oldest_time = 0xFFFFFFFFFFFFFFFFULL;

            for (int w = 0; w < CACHE_WAYS; w++) {
                if (!set->ways[w].valid) {
                    victim_way = w;
                    break;
                } else {
                    if (set->ways[w].last_access < oldest_time) {
                        oldest_time = set->ways[w].last_access;
                        victim_way = w;
                    } else {
                        // Current is newer
                    }
                }
            }

            set->ways[victim_way].valid = true;
            set->ways[victim_way].device = dev;
            set->ways[victim_way].lba = current_lba;
            set->ways[victim_way].last_access = hal_timer_get_ticks();
            memcpy(set->ways[victim_way].data, bounce_buf, 512);
        } else {
            // Line updated correctly
        }

        spinlock_release(&set->lock, flags);
    }

    if (bounce_buf) { kfree_contiguous(bounce_buf, 4096); } else { /* Clean */ }
    return 1;
}

int DiskCache::readVector(Device* dev, uint64_t lba, kiovec_t* vectors, int vector_count) {
    if (!dev || !vectors || vector_count == 0) { return 0; } else { return dev->read_vector(lba, vectors, vector_count); }
}

int DiskCache::writeVector(Device* dev, uint64_t lba, kiovec_t* vectors, int vector_count) {
    if (!dev || !vectors || vector_count == 0) { return 0; } else { return dev->write_vector(lba, vectors, vector_count); }
}

void DiskCache::invalidateDevice(Device* dev) {
    for (int i = 0; i < CACHE_SETS; i++) {
        uint64_t flags = spinlock_acquire(&sets[i].lock);
        for (int w = 0; w < CACHE_WAYS; w++) {
            if (sets[i].ways[w].valid && sets[i].ways[w].device == dev) {
                sets[i].ways[w].valid = false;
            } else {
                // Ignore
            }
        }
        spinlock_release(&sets[i].lock, flags);
    }
}

void DiskCache::flushAll() {
    int flushed_count = 0;
    for (int i = 0; i < CACHE_SETS; i++) {
        uint64_t flags = spinlock_acquire(&sets[i].lock);
        for (int w = 0; w < CACHE_WAYS; w++) {
            if (sets[i].ways[w].valid) {
                sets[i].ways[w].valid = false;
                flushed_count++;
            } else {
                // Already empty
            }
        }
        spinlock_release(&sets[i].lock, flags);
    }
    
    char buf[64];
    snprintf(buf, sizeof(buf), "Cache synced (%d buffers cleared)", flushed_count);
    
    extern bool scheduler_active;
    if (!scheduler_active) {
        print_shutdown(buf);
    } else {
        // Output naturally
    }
}

size_t DiskCache::getCacheSize() {
    size_t total = 0;
    for (int i = 0; i < CACHE_SETS; i++) {
        uint64_t flags = spinlock_acquire(&sets[i].lock);
        for (int w = 0; w < CACHE_WAYS; w++) {
            if (sets[i].ways[w].valid) { total += 512; } else { /* Empty */ }
        }
        spinlock_release(&sets[i].lock, flags);
    }
    return total;
}

extern "C" {
    void disk_cache_init() { DiskCache::init(); }
    void disk_cache_flush_all() { DiskCache::flushAll(); }
    int disk_cache_read_block(device_t dev, uint64_t lba, uint32_t count, void* buffer) {
        return DiskCache::readBlock((Device*)dev, lba, count, buffer);
    }
    int disk_cache_write_block(device_t dev, uint64_t lba, uint32_t count, const void* buffer) {
        return DiskCache::writeBlock((Device*)dev, lba, count, buffer);
    }
    
    int32_t disk_cache_read_vector(device_t dev, uint64_t lba, kiovec_t* vectors, int count) {
        return DiskCache::readVector((Device*)dev, lba, vectors, count);
    }
    int32_t disk_cache_write_vector(device_t dev, uint64_t lba, kiovec_t* vectors, int count) {
        return DiskCache::writeVector((Device*)dev, lba, vectors, count);
    }
    
    size_t disk_cache_get_size() { return DiskCache::getCacheSize(); }
}