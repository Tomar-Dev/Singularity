// archs/kom/common/kdevice.cpp
#include "archs/kom/common/kdevice.hpp"
#include "libc/string.h"
#include "libc/stdio.h"
KDevice::~KDevice() {
    // FIX: ons_unregister(); kaldirildi. Cunku ONS'den kaldirilan cihaz kobject_unref tetikler.
    // Eger cihaz zaten yok ediliyorsa (refcount==0), ons_unregister yapilirsa tekrar delete tetiklenir ve recursive crash olusur.
}

void KDevice::ons_register(const char* path) {
    if (!path) return;
    
    strncpy(dev_path, path, 127);
    dev_path[127] = '\0';
    
    int res = ons_bind(dev_path, this);
    
    if (res != 0) {
        printf("[KOM] Error: Failed to bind device to ONS path: %s (Err: %d)\n", dev_path, res);
    }
}

void KDevice::ons_unregister() {
    if (dev_path[0] != '\0') {
        ons_unbind(dev_path);
        dev_path[0] = '\0';
    }
}
