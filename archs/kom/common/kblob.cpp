// archs/kom/common/kblob.cpp
#include "archs/kom/common/kblob.hpp"
#include "libc/string.h"
#include "memory/kheap.h"

KBlob::KBlob(const char* blobName, uint64_t initialSize) 
    : KObject(KObjectType::BLOB), size(initialSize) 
{
    if (blobName) {
        strncpy(name, blobName, 127);
        name[127] = '\0';
    } else {
        name[0] = '\0';
    }
    rw_lock = rwlock_create();
}

KBlob::~KBlob() {
    if (rw_lock) {
        rwlock_destroy(rw_lock);
    } else {
        // Null
    }
}

// === KVolatileBlob Implementations ===
KVolatileBlob::KVolatileBlob(const char* blobName) : KBlob(blobName, 0), buffer(nullptr), capacity(0) {}

KVolatileBlob::KVolatileBlob(const char* blobName, const void* data, size_t data_size) : KBlob(blobName, data_size) {
    capacity = data_size;
    if (capacity > 0) {
        buffer = static_cast<uint8_t*>(kmalloc(capacity));
        if (buffer && data) {
            memcpy(buffer, data, capacity);
        } else {
            // Null
        }
    } else {
        buffer = nullptr;
    }
}

KVolatileBlob::~KVolatileBlob() {
    if (buffer) {
        kfree(buffer);
    } else {
        // Null
    }
}

error_t KVolatileBlob::read(uint64_t offset, void* buf, size_t count, size_t* bytes_read) {
    ScopedReadLock srl(rw_lock);
    if (offset >= size) { 
        *bytes_read = 0; 
        return KOM_OK; 
    } else {
        // Valid
    }
    size_t available = size - offset;
    size_t to_read = (count < available) ? count : available;
    memcpy(buf, buffer + offset, to_read);
    *bytes_read = to_read;
    return KOM_OK;
}

error_t KVolatileBlob::write(uint64_t offset, const void* buf, size_t count, size_t* bytes_written) {
    ScopedWriteLock swl(rw_lock);
    
    if (offset + count < offset) {
        *bytes_written = 0;
        return KOM_ERR_IO;
    } else {
        // Valid
    }
    
    if (offset + count > capacity) {
        size_t needed = offset + count;
        // PERFORMANS YAMASI: O(n^2) büyüme engellendi. Capacity * 2 faktörü eklendi.
        size_t new_cap = (capacity * 2 > needed) ? capacity * 2 : needed;
        if (new_cap < 4096) {
            new_cap = 4096;
        } else {
            // Valid
        }
        
        uint8_t* new_buf = static_cast<uint8_t*>(kmalloc(new_cap));
        if (!new_buf) { 
            *bytes_written = 0; 
            return KOM_ERR_NO_MEMORY; 
        } else {
            // Allocated
        }
        
        if (buffer) {
            memcpy(new_buf, buffer, size);
            kfree(buffer);
        } else {
            // First alloc
        }
        buffer = new_buf;
        capacity = new_cap;
    } else {
        // Fits
    }
    
    if (offset > size) {
        memset(buffer + size, 0, offset - size);
    } else {
        // Overwrite or append
    }
    
    memcpy(buffer + offset, buf, count);
    if (offset + count > size) {
        size = offset + count;
    } else {
        // Valid
    }
    *bytes_written = count;
    return KOM_OK;
}

error_t KVolatileBlob::resize(uint64_t new_size) {
    ScopedWriteLock swl(rw_lock);
    if (new_size > capacity) {
        uint8_t* new_buf = static_cast<uint8_t*>(kmalloc(new_size));
        if (!new_buf) {
            return KOM_ERR_NO_MEMORY;
        } else {
            // Allocated
        }
        if (buffer) {
            memcpy(new_buf, buffer, size);
            kfree(buffer);
        } else {
            // First alloc
        }
        buffer = new_buf;
        capacity = new_size;
    } else {
        // Fits
    }
    
    if (new_size > size) {
        memset(buffer + size, 0, new_size - size);
    } else {
        // Shrink
    }
    
    size = new_size;
    return KOM_OK;
}

// === KDynamicBlob Implementations ===
KDynamicBlob::KDynamicBlob(const char* blobName, DynamicReadCallback cb) : KBlob(blobName, 0), read_cb(cb) {}

KDynamicBlob::~KDynamicBlob() {}

error_t KDynamicBlob::read(uint64_t offset, void* buf, size_t count, size_t* bytes_read) {
    if (!read_cb) { 
        *bytes_read = 0; 
        return KOM_ERR_UNSUPPORTED; 
    } else {
        // Valid
    }
    *bytes_read = read_cb(offset, buf, count);
    return KOM_OK;
}

error_t KDynamicBlob::write(uint64_t offset, const void* buf, size_t count, size_t* bytes_written) {
    (void)offset; (void)buf; (void)count;
    *bytes_written = 0;
    return KOM_ERR_CAP_DENIED; 
}