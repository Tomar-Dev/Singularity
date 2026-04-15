// archs/kom/common/kdevice.hpp
#ifndef KDEVICE_HPP
#define KDEVICE_HPP

#include "archs/kom/common/kobject.hpp"
#include "archs/kom/common/ons.hpp"
typedef uint32_t error_t;

enum KDeviceQueryID {
    KDQ_BLOCK_COUNT,
    KDQ_BLOCK_SIZE,
    KDQ_SECTOR_SIZE,
    KDQ_MEDIA_TYPE,
    KDQ_CHAR_BAUD,
    KDQ_CHAR_MODE,
    KDQ_GPU_VRAM_SIZE,
    KDQ_GPU_MAX_RES,
    KDQ_NET_MAC,
    KDQ_NET_MTU
};

enum KDeviceControlID {
    KDC_FLUSH,
    KDC_POWER_STATE,
    KDC_GPU_SETMODE,
    KDC_CHAR_SETMODE,
    KDC_GPU_PRESENT
};

class KDevice : public KObject {
protected:
    char dev_path[128];

public:
    KDevice(KObjectType specific_type) : KObject(specific_type) {
        dev_path[0] = '\0';
    }
    
    virtual ~KDevice() override;

    virtual error_t query(KDeviceQueryID id, void* out, size_t out_len) = 0;
    virtual error_t control(KDeviceControlID id, const void* in, size_t in_len) = 0;
    virtual void on_remove() {}

    void ons_register(const char* path);
    void ons_unregister();
};

#endif
