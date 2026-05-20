// archs/kom/common/kobject.hpp
#ifndef KOBJECT_HPP
#define KOBJECT_HPP

#include <stdint.h>
#include <stddef.h>

#define KOM_OK                   0
#define KOM_ERR_STALE_HANDLE     1
#define KOM_ERR_CAP_DENIED       2
#define KOM_ERR_INVALID_TYPE     3
#define KOM_ERR_NO_MEMORY        4
#define KOM_ERR_UNSUPPORTED      5
#define KOM_ERR_NOT_FOUND        6
#define KOM_ERR_COLLISION        7
#define KOM_ERR_IO               8 
#define KOM_ERR_PEER_CLOSED      9  
#define KOM_ERR_BUFFER_TOO_SMALL 10 

#ifdef __cplusplus
#include "archs/cpu/x86_64/sync/spinlock.h"
#include "system/security/rng.h"
#include "kernel/fastops.h"
typedef uint32_t error_t;

enum class KObjectType : uint8_t {
    PROCESS = 1,
    THREAD,
    MEMORY_OBJECT,
    EVENT,       
    INTERRUPT,   
    TIMER,
    CHANNEL,     
    PORT,
    BLOB,
    CONTAINER,
    DEVICE,
    BLOCK_DEVICE,
    CHAR_DEVICE,
    NET_DEVICE,
    GPU_DEVICE
};

struct KObject {
    KObjectType type;
    uint8_t _pad[3];
    uint32_t ref_count; 
    spinlock_t lock;
    uint64_t id;          
    uint64_t created_tsc; 

    KObject(KObjectType t) : type(t), ref_count(1) {
        spinlock_init(&lock);
        id = get_secure_random();
        created_tsc = rdtsc_ordered();
    }

    virtual ~KObject() = default;
};

void kobject_ref(KObject* obj);
void kobject_unref(KObject* obj);

typedef uint64_t handle_t;

#define KAP_READ    ((uint16_t)(1 << 15))
#define KAP_WRITE   ((uint16_t)(1 << 14))
#define KAP_EXEC    ((uint16_t)(1 << 13))
#define KAP_CTRL    ((uint16_t)(1 << 12))
#define KAP_DUP     ((uint16_t)(1 << 11))
#define KAP_XFER    ((uint16_t)(1 << 10))
#define KAP_WAIT    ((uint16_t)(1 << 9))
#define KAP_DESTROY ((uint16_t)(1 << 8))

struct HandleEntry {
    KObject* obj;
    uint16_t caps;
    uint16_t generation;
};

class HandleTable {
public:
    static constexpr uint32_t CAPACITY = 4096;

private:
    HandleEntry entries[CAPACITY];
    spinlock_t lock;
    uint16_t freelist[CAPACITY];
    uint16_t freelist_top;

public:
    void init();
    handle_t alloc(KObject* obj, uint16_t caps);
    KObject* resolve(handle_t h, uint16_t req_caps);
    handle_t dup(handle_t h, uint16_t new_caps);
    void close(handle_t h);
    
    // YENİ: Move Semantics için Transfer fonksiyonu
    handle_t transfer(handle_t h);
};

#endif

#ifdef __cplusplus
extern "C" {
#endif

void kobject_unref_c(void* obj);

#ifdef __cplusplus
}
#endif

#endif