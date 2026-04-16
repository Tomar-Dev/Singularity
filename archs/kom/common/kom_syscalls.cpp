// archs/kom/common/kom_syscalls.cpp
#include "archs/kom/common/kobject.hpp"
#include "archs/kom/common/ons.hpp"
#include "archs/kom/common/kom.h"
#include "libc/stdio.h"
#include "kernel/debug.h"
inline void* operator new(size_t, void* p) { return p; }

static HandleTable* kernel_handle_table = nullptr;
static uint8_t kom_memory_buffer[sizeof(HandleTable)];

extern "C" {

void kom_init() {
    kernel_handle_table = new (kom_memory_buffer) HandleTable();
    kernel_handle_table->init();
}

uint64_t kom_open(const char* path, uint16_t requested_caps) {
    if (!kernel_handle_table) return 0;
    
    KObject* target = ons_resolve(path);
    if (!target) return 0;
    
    handle_t h = kernel_handle_table->alloc(target, requested_caps);
    
    kobject_unref(target);
    
    return h;
}

int kom_close(uint64_t handle) {
    if (!kernel_handle_table) return KOM_ERR_STALE_HANDLE;
    kernel_handle_table->close(handle);
    return KOM_OK;
}

uint64_t kom_duplicate(uint64_t handle, uint16_t requested_caps) {
    if (!kernel_handle_table) return 0;
    return kernel_handle_table->dup(handle, requested_caps);
}

}