// system/device/virtual_dev.cpp
#include "system/device/virtual_dev.hpp"
#include "system/security/rng.h"
#include "libc/string.h"
#include "memory/kheap.h"
int ZeroDevice::read(char* buffer, int size) {
    memset(buffer, 0, size);
    return size;
}

int RandomDevice::read(char* buffer, int size) {
    for (int i = 0; i < size; i += 8) {
        uint64_t r = get_secure_random();
        int copy = (size - i) < 8 ? (size - i) : 8;
        memcpy(buffer + i, &r, copy);
    }
    return size;
}

extern "C" void init_virtual_devices() {
    DeviceManager::registerDevice(new NullDevice());
    DeviceManager::registerDevice(new ZeroDevice());
    DeviceManager::registerDevice(new RandomDevice());
}
