// system/disk/cache.hpp
#ifndef DISK_CACHE_HPP
#define DISK_CACHE_HPP

#include <stdint.h>
#include <stddef.h>
#include "system/device/device.h"
#include "archs/cpu/x86_64/sync/spinlock.h"

#ifdef __cplusplus

// GÜVENLİK YAMASI: RAM BSS Alanı optimizasyon için 1024'ten 256'ya düşürüldü.
// Bu sayede Kernel/Static bellek kalıcı olarak 1.64 MB aşağı inecektir.
#define CACHE_SETS 256
#define CACHE_WAYS 4

struct CacheEntry {
    uint64_t lba;
    Device* device;
    uint8_t data[512] __attribute__((aligned(16))); 
    bool valid;
    uint64_t last_access; 
};

struct CacheSet {
    spinlock_t lock;
    CacheEntry ways[CACHE_WAYS];
};

class DiskCache {
private:
    static CacheSet sets[CACHE_SETS];
    static uint32_t hash(Device* dev, uint64_t lba);

public:
    static void init();
    static int readBlock(Device* dev, uint64_t lba, uint32_t count, void* buffer);
    static int writeBlock(Device* dev, uint64_t lba, uint32_t count, const void* buffer);
    
    static int readVector(Device* dev, uint64_t lba, kiovec_t* vectors, int vector_count);
    static int writeVector(Device* dev, uint64_t lba, kiovec_t* vectors, int vector_count);

    static void invalidateDevice(Device* dev);
    static void flushAll();
    static size_t getCacheSize();
};

#endif

#ifdef __cplusplus
extern "C" {
#endif

void disk_cache_init(void);
void disk_cache_flush_all(void);

int disk_cache_read_block(device_t dev, uint64_t lba, uint32_t count, void* buffer);
int disk_cache_write_block(device_t dev, uint64_t lba, uint32_t count, const void* buffer);

int32_t disk_cache_read_vector(device_t dev, uint64_t lba, kiovec_t* vectors, int count);
int32_t disk_cache_write_vector(device_t dev, uint64_t lba, kiovec_t* vectors, int count);

size_t disk_cache_get_size(void);

#ifdef __cplusplus
}
#endif

#endif