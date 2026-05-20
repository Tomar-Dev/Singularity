// archs/kom/common/kom_syscalls.cpp
#include "archs/kom/common/kobject.hpp"
#include "archs/kom/common/ons.hpp"
#include "archs/kom/common/kom.h"
#include "archs/kom/common/kchannel.hpp"
#include "archs/kom/common/kport.hpp"
#include "libc/stdio.h"
#include "kernel/debug.h"
#include "memory/kheap.h" // FIX: kmalloc ve kfree için eklendi

inline void* operator new(size_t, void* p) { return p; }

static HandleTable* kernel_handle_table = nullptr;

alignas(HandleTable) static uint8_t kom_memory_buffer[sizeof(HandleTable)];

extern "C" {

void kom_init() {
    kernel_handle_table = new (kom_memory_buffer) HandleTable();
    kernel_handle_table->init();
}

uint64_t kom_open(const char* path, uint16_t requested_caps) {
    if (!kernel_handle_table) { return 0; }
    
    KObject* target = ons_resolve(path);
    if (!target) { return 0; }
    
    handle_t h = kernel_handle_table->alloc(target, requested_caps);
    kobject_unref(target);
    
    return h;
}

int kom_close(uint64_t handle) {
    if (!kernel_handle_table) { return KOM_ERR_STALE_HANDLE; }
    kernel_handle_table->close(handle);
    return KOM_OK;
}

uint64_t kom_duplicate(uint64_t handle, uint16_t requested_caps) {
    if (!kernel_handle_table) { return 0; }
    return kernel_handle_table->dup(handle, requested_caps);
}

int kom_channel_create(uint64_t* out_handle_a, uint64_t* out_handle_b) {
    if (!kernel_handle_table || !out_handle_a || !out_handle_b) return KOM_ERR_INVALID_TYPE;

    KChannelEndpoint* ep_a = nullptr;
    KChannelEndpoint* ep_b = nullptr;

    error_t err = kchannel_create(&ep_a, &ep_b);
    if (err != KOM_OK) return err;

    *out_handle_a = kernel_handle_table->alloc(ep_a, KAP_READ | KAP_WRITE | KAP_XFER);
    *out_handle_b = kernel_handle_table->alloc(ep_b, KAP_READ | KAP_WRITE | KAP_XFER);

    kobject_unref(ep_a);
    kobject_unref(ep_b);

    return KOM_OK;
}

int kom_channel_write(uint64_t handle, const void* data, uint32_t data_size, const uint64_t* handles, uint32_t handle_count) {
    if (!kernel_handle_table) return KOM_ERR_STALE_HANDLE;

    KObject* obj = kernel_handle_table->resolve(handle, KAP_WRITE);
    if (!obj) return KOM_ERR_CAP_DENIED;

    if (obj->type != KObjectType::CHANNEL) {
        kobject_unref(obj);
        return KOM_ERR_INVALID_TYPE;
    }

    // YENİ: Move Semantics (Handle'ları transfer et ve gönderenden sil)
    handle_t* moved_handles = nullptr;
    if (handle_count > 0 && handles) {
        moved_handles = (handle_t*)kmalloc(handle_count * sizeof(handle_t));
        if (!moved_handles) {
            kobject_unref(obj);
            return KOM_ERR_NO_MEMORY;
        }
        for (uint32_t i = 0; i < handle_count; i++) {
            moved_handles[i] = kernel_handle_table->transfer(handles[i]);
        }
    }

    KChannelEndpoint* ep = (KChannelEndpoint*)obj;
    error_t err = ep->write(data, data_size, moved_handles, handle_count);
    
    if (moved_handles) kfree(moved_handles);
    kobject_unref(obj);
    return err;
}

int kom_channel_read(uint64_t handle, void* data, uint32_t* data_size, uint64_t* handles, uint32_t* handle_count) {
    if (!kernel_handle_table) return KOM_ERR_STALE_HANDLE;

    KObject* obj = kernel_handle_table->resolve(handle, KAP_READ);
    if (!obj) return KOM_ERR_CAP_DENIED;

    if (obj->type != KObjectType::CHANNEL) {
        kobject_unref(obj);
        return KOM_ERR_INVALID_TYPE;
    }

    KChannelEndpoint* ep = (KChannelEndpoint*)obj;
    error_t err = ep->read(data, data_size, handles, handle_count);
    
    kobject_unref(obj);
    return err;
}

int kom_port_create(uint64_t* out_handle) {
    if (!kernel_handle_table || !out_handle) return KOM_ERR_INVALID_TYPE;

    KPort* port = new KPort();
    if (!port) return KOM_ERR_NO_MEMORY;

    *out_handle = kernel_handle_table->alloc(port, KAP_READ | KAP_WRITE | KAP_WAIT);
    kobject_unref(port);

    return KOM_OK;
}

int kom_port_queue(uint64_t handle, const kom_port_packet_t* packet) {
    if (!kernel_handle_table) return KOM_ERR_STALE_HANDLE;

    KObject* obj = kernel_handle_table->resolve(handle, KAP_WRITE);
    if (!obj) return KOM_ERR_CAP_DENIED;

    if (obj->type != KObjectType::PORT) {
        kobject_unref(obj);
        return KOM_ERR_INVALID_TYPE;
    }

    KPort* port = (KPort*)obj;
    error_t err = port->queue(packet);
    
    kobject_unref(obj);
    return err;
}

int kom_port_wait(uint64_t handle, kom_port_packet_t* out_packet) {
    if (!kernel_handle_table) return KOM_ERR_STALE_HANDLE;

    KObject* obj = kernel_handle_table->resolve(handle, KAP_WAIT | KAP_READ);
    if (!obj) return KOM_ERR_CAP_DENIED;

    if (obj->type != KObjectType::PORT) {
        kobject_unref(obj);
        return KOM_ERR_INVALID_TYPE;
    }

    KPort* port = (KPort*)obj;
    error_t err = port->wait(out_packet);
    
    kobject_unref(obj);
    return err;
}

}