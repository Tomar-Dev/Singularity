// system/device/virtio_glue.cpp
#include "system/device/device.h"
#include "drivers/storage/virtio/virtio.hpp"
extern "C" {
    void init_virtio_glue() {
        init_virtio();
    }
}
