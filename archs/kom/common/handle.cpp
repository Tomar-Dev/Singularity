// archs/kom/common/handle.cpp
#include "archs/kom/common/kobject.hpp"
#include "memory/kheap.h"
#include "libc/string.h"
#include "libc/stdio.h"
#include "kernel/debug.h"

void kobject_ref(KObject* obj) {
    if (obj) {
        __atomic_fetch_add(&obj->ref_count, 1, __ATOMIC_SEQ_CST);
    } else { /* Ignored */ }
}

// Bulgu 2.1 FIX: Strict Refcount Underflow Protection
void kobject_unref(KObject* obj) {
    if (obj) {
        uint32_t old_val = __atomic_fetch_sub(&obj->ref_count, 1, __ATOMIC_SEQ_CST);
        if (old_val == 1) {
            delete obj;
        } else if (__builtin_expect(old_val == 0, 0)) {
            panic_at("KOM", 0, KERR_HEAP_DOUBLE_FREE, "KObject Reference Count Underflow Detected!");
        } else {
            // Ref count still positive
        }
    } else { /* Null skip */ }
}

extern "C" void kobject_unref_c(void* obj) {
    if (obj) { kobject_unref((KObject*)obj); } else { /* Null skip */ }
}

void HandleTable::init() {
    spinlock_init(&lock);
    memset(entries, 0, sizeof(entries));
    freelist_top = 0;
    
    for (uint32_t i = CAPACITY - 1; i > 0; i--) {
        freelist[freelist_top++] = i;
        entries[i].generation = 1; 
    }
}

handle_t HandleTable::alloc(KObject* obj, uint16_t caps) {
    if (!obj) { return 0; } else { /* Valid */ }

    uint64_t flags = spinlock_acquire(&lock);
    if (freelist_top == 0) {
        spinlock_release(&lock, flags);
        return 0; 
    } else { /* Slots available */ }

    uint32_t idx = freelist[--freelist_top];
    HandleEntry& entry = entries[idx];
    
    entry.obj = obj;
    entry.caps = caps;
    uint16_t gen = entry.generation;
    spinlock_release(&lock, flags);

    kobject_ref(obj);

    handle_t h = ((uint64_t)caps << 48) | ((uint64_t)gen << 32) | idx;
    return h;
}

KObject* HandleTable::resolve(handle_t h, uint16_t req_caps) {
    uint32_t idx = h & 0xFFFFFFFF;
    uint16_t gen = (h >> 32) & 0xFFFF;
    uint16_t provided_caps = (h >> 48) & 0xFFFF;

    if ((provided_caps & req_caps) != req_caps) { return nullptr; } else { /* Authorized */ }
    if (idx == 0 || idx >= CAPACITY) { return nullptr; } else { /* In bounds */ }

    uint64_t flags = spinlock_acquire(&lock);
    HandleEntry& entry = entries[idx];
    
    if (entry.generation != gen || entry.obj == nullptr) {
        spinlock_release(&lock, flags);
        return nullptr;
    } else { /* Gen matched */ }

    if ((entry.caps & req_caps) != req_caps) {
        spinlock_release(&lock, flags);
        return nullptr;
    } else { /* Caps matched */ }

    KObject* target = entry.obj;
    kobject_ref(target); 
    spinlock_release(&lock, flags);

    return target;
}

handle_t HandleTable::dup(handle_t h, uint16_t new_caps) {
    uint32_t idx = h & 0xFFFFFFFF;
    uint16_t gen = (h >> 32) & 0xFFFF;
    
    if (idx == 0 || idx >= CAPACITY) { return 0; } else { /* Bound check pass */ }

    uint64_t flags = spinlock_acquire(&lock);
    HandleEntry& entry = entries[idx];

    if (entry.generation != gen || entry.obj == nullptr) {
        spinlock_release(&lock, flags);
        return 0;
    } else { /* OK */ }

    if ((entry.caps & new_caps) != new_caps) {
        spinlock_release(&lock, flags);
        return 0;
    } else { /* OK */ }

    KObject* obj = entry.obj;
    spinlock_release(&lock, flags);

    return alloc(obj, new_caps);
}

void HandleTable::close(handle_t h) {
    uint32_t idx = h & 0xFFFFFFFF;
    uint16_t gen = (h >> 32) & 0xFFFF;

    if (idx == 0 || idx >= CAPACITY) { return; } else { /* Bound check pass */ }

    uint64_t flags = spinlock_acquire(&lock);
    HandleEntry& entry = entries[idx];

    if (entry.generation != gen || entry.obj == nullptr) {
        spinlock_release(&lock, flags);
        return;
    } else { /* OK */ }

    KObject* obj = entry.obj;
    
    entry.obj = nullptr;
    entry.caps = 0;
    
    entry.generation++; 
    if (entry.generation == 0) { entry.generation = 1; } else { /* Carry */ }

    freelist[freelist_top++] = idx;
    spinlock_release(&lock, flags);

    kobject_unref(obj);
}