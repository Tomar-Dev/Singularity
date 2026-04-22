// archs/kom/common/kcontainer.cpp
#include "archs/kom/common/kcontainer.hpp"
#include "memory/kheap.h"
#include "libc/string.h"
#include "kernel/debug.h"
#include "archs/cpu/cpu_hal.h"

KContainer::KContainer() : KObject(KObjectType::CONTAINER), head(nullptr), rw_lock(nullptr) {
    rw_lock = rwlock_create();
    if (!rw_lock) {
        panic_at("KCONTAINER", 0, KERR_MEM_OOM,
                 "KContainer::KContainer — rwlock_create() returned NULL (OOM).");
    }
}

KContainer::~KContainer() {
    if (rw_lock) {
        rwlock_acquire_write(rw_lock);
    } else {
        serial_write("[KCONTAINER] WARN: destructor called with NULL rw_lock!\n");
    }

    ONSBinding* curr = head;
    while (curr) {
        ONSBinding* next = curr->next;
        if (curr->obj) {
            kobject_unref(curr->obj);
        }
        kfree(curr);
        curr = next;
    }
    head = nullptr;

    if (rw_lock) {
        rwlock_release_write(rw_lock);
        rwlock_destroy(rw_lock);
        rw_lock = nullptr;
    }
}

error_t KContainer::bind(const char* name, KObject* obj) {
    if (!name || !obj) return KOM_ERR_INVALID_TYPE;
    if (strlen(name) >= 64) return KOM_ERR_NO_MEMORY;

    ScopedWriteLock swl(rw_lock);

    ONSBinding* curr = head;
    while (curr) {
        if (strcmp(curr->name, name) == 0) return KOM_ERR_COLLISION;
        curr = curr->next;
    }

    ONSBinding* b = static_cast<ONSBinding*>(kmalloc(sizeof(ONSBinding)));
    if (!b) return KOM_ERR_NO_MEMORY;
    
    strncpy(b->name, name, 63);
    b->name[63] = '\0';

    b->obj = obj;
    kobject_ref(obj);

    b->next = head;
    head    = b;

    return KOM_OK;
}

error_t KContainer::unbind(const char* name) {
    if (!name) return KOM_ERR_INVALID_TYPE;

    ScopedWriteLock swl(rw_lock);

    ONSBinding* curr = head;
    ONSBinding* prev = nullptr;

    while (curr) {
        if (strcmp(curr->name, name) == 0) {
            if (prev) prev->next = curr->next;
            else      head = curr->next;

            KObject* target = curr->obj;
            kfree(curr);
            kobject_unref(target);
            return KOM_OK;
        }
        prev = curr;
        curr = curr->next;
    }

    return KOM_ERR_NOT_FOUND;
}

KObject* KContainer::lookup(const char* name) {
    if (!name) return nullptr;

    ScopedReadLock srl(rw_lock);

    ONSBinding* curr = head;
    while (curr) {
        if (strcmp(curr->name, name) == 0) {
            KObject* target = curr->obj;
            kobject_ref(target);
            return target;
        }
        curr = curr->next;
    }

    return nullptr;
}

bool KContainer::enumerate(uint32_t index, char* out_name, KObjectType* out_type) {
    if (!out_name || !out_type) return false;

    ScopedReadLock srl(rw_lock);

    ONSBinding* curr  = head;
    uint32_t    idx   = 0;

    while (curr) {
        if (idx == index) {
            strncpy(out_name, curr->name, 63);
            out_name[63] = '\0';
            *out_type = curr->obj ? curr->obj->type : KObjectType::PROCESS;
            return true;
        }
        curr = curr->next;
        idx++;
    }

    return false;
}

error_t KContainer::create_child(const char* name, KObjectType type) {
    if (type == KObjectType::CONTAINER) {
        KContainer* new_c = new KContainer();
        error_t res = this->bind(name, new_c);
        kobject_unref(new_c);
        return res;
    }
    return KOM_ERR_UNSUPPORTED;
}