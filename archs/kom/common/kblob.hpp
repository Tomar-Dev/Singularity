// archs/kom/common/kblob.hpp
#ifndef KBLOB_HPP
#define KBLOB_HPP

#include "archs/kom/common/kobject.hpp"
#include "system/sync/rwlock.h"
#include "archs/kom/common/kiovec.h"

class KBlob : public KObject {
protected:
    char name[128];
    uint64_t size;
    rwlock_t rw_lock;

public:
    KBlob(const char* blobName, uint64_t initialSize = 0);
    virtual ~KBlob() override;

    const char* getName() const { return name; }
    uint64_t getSize() const { return size; }
    void setSize(uint64_t newSize) { size = newSize; }

    virtual error_t read(uint64_t offset, void* buffer, size_t count, size_t* bytes_read) = 0;
    virtual error_t write(uint64_t offset, const void* buffer, size_t count, size_t* bytes_written) = 0;
    
    virtual error_t read_vector(uint64_t offset, kiovec_t* vectors, int vector_count, size_t* total_read) {
        (void)offset; (void)vectors; (void)vector_count;
        *total_read = 0;
        return KOM_ERR_UNSUPPORTED;
    }
    
    virtual error_t write_vector(uint64_t offset, kiovec_t* vectors, int vector_count, size_t* total_written) {
        (void)offset; (void)vectors; (void)vector_count;
        *total_written = 0;
        return KOM_ERR_UNSUPPORTED; 
    }

    virtual error_t resize(uint64_t new_size) {
        (void)new_size;
        return KOM_ERR_UNSUPPORTED; 
    }
};

// VFS RAMFS YERİNE GEÇEN NESNE (Sıfır Overhead)
class KVolatileBlob : public KBlob {
private:
    uint8_t* buffer;
    size_t capacity;
public:
    KVolatileBlob(const char* blobName);
    KVolatileBlob(const char* blobName, const void* data, size_t data_size);
    ~KVolatileBlob() override;

    error_t read(uint64_t offset, void* buf, size_t count, size_t* bytes_read) override;
    error_t write(uint64_t offset, const void* buf, size_t count, size_t* bytes_written) override;
    error_t resize(uint64_t new_size) override;
};

// VFS PROCFS YERİNE GEÇEN NESNE (Sıfır Overhead)
typedef size_t (*DynamicReadCallback)(uint64_t offset, void* buffer, size_t count);
class KDynamicBlob : public KBlob {
private:
    DynamicReadCallback read_cb;
public:
    KDynamicBlob(const char* blobName, DynamicReadCallback cb);
    ~KDynamicBlob() override;

    error_t read(uint64_t offset, void* buf, size_t count, size_t* bytes_read) override;
    error_t write(uint64_t offset, const void* buf, size_t count, size_t* bytes_written) override;
};

#endif