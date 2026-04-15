// archs/kom/common/kobject.hpp
#ifndef KOBJECT_HPP
#define KOBJECT_HPP

#include <stdint.h>
#include <stddef.h>

#define KOM_OK               0
#define KOM_ERR_STALE_HANDLE 1
#define KOM_ERR_CAP_DENIED   2
#define KOM_ERR_INVALID_TYPE 3
#define KOM_ERR_NO_MEMORY    4
#define KOM_ERR_UNSUPPORTED  5
#define KOM_ERR_NOT_FOUND    6
#define KOM_ERR_COLLISION    7
#define KOM_ERR_IO           8 

#ifdef __cplusplus
#include "archs/cpu/x86_64/sync/spinlock.h"
#include "system/security/rng.h"
#include "kernel/fastops.h"
typedef uint32_t error_t;

enum class KObjectType : uint8_t {
    PROCESS = 1,
    THREAD,
    MEMORY_OBJECT,
    EVENT,       // <-- EKLENDİ
    INTERRUPT,   // <-- EKLENDİ
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

#define KAP_READ    (1ULL << 63)
#define KAP_WRITE   (1ULL << 62)
#define KAP_EXEC    (1ULL << 61)
#define KAP_CTRL    (1ULL << 60)
#define KAP_DUP     (1ULL << 59)
#define KAP_XFER    (1ULL << 58)
#define KAP_WAIT    (1ULL << 57)
#define KAP_DESTROY (1ULL << 56)

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
