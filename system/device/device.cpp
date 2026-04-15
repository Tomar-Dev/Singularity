// system/device/device.cpp
#include "system/device/device.h"
#include "libc/string.h"
#include "libc/stdio.h"
#include "system/device/partition.h"
#include "kernel/config.h"
#include "system/ffi/ffi.h"
extern "C" void print_status(const char* prefix, const char* msg, const char* status);

#define MAX_EMERGENCY_DEVICES 64
static Device* emergency_devices[MAX_EMERGENCY_DEVICES];
static volatile int emergency_device_count = 0;

static volatile uint32_t sata_disk_count = 0;
static volatile uint32_t nvme_disk_count = 0;

static void register_emergency_device(Device* dev) {
    if (!dev) { return; }
    int idx = __atomic_fetch_add(&emergency_device_count, 1, __ATOMIC_SEQ_CST);
    if (idx < MAX_EMERGENCY_DEVICES) {
        kobject_ref(static_cast<KObject*>(dev)); 
        emergency_devices[idx] = dev;
    }
}

static KObjectType get_kobj_type(DeviceType t) {
    if (t == DEV_BLOCK || t == DEV_PARTITION) { return KObjectType::BLOCK_DEVICE; }
    if (t == DEV_DISPLAY) { return KObjectType::GPU_DEVICE; }
    return KObjectType::CHAR_DEVICE;
}

Device::Device(const char* devName, DeviceType devType)
    : KDevice(get_kobj_type(devType)), type(devType), capacityBytes(0)
{
    setName(devName);
    setModel("Generic Device");
    mountPoint[0] = '\0';
    writeProtected = (type == DEV_BLOCK || type == DEV_PARTITION);

    vfs_lock = rwlock_create();

    if (type == DEV_BLOCK) {
        register_emergency_device(this);
    }
}

Device::~Device() {
    if (vfs_lock) { rwlock_destroy(vfs_lock); }
}

error_t Device::query(KDeviceQueryID id, void* out, size_t out_len) {
    if (!out) { return 3; }
    switch (id) {
        case KDQ_BLOCK_COUNT:
            if (out_len == sizeof(uint64_t)) {
                *static_cast<uint64_t*>(out) = capacityBytes / getBlockSize();
                return KOM_OK;
            }
            break;
        case KDQ_BLOCK_SIZE:
        case KDQ_SECTOR_SIZE:
            if (out_len == sizeof(uint32_t)) {
                *static_cast<uint32_t*>(out) = getBlockSize();
                return KOM_OK;
            }
            break;
        default:
            return 2;
    }
    return 2;
}

error_t Device::control(KDeviceControlID id, const void* in, size_t in_len) {
    (void)in; (void)in_len;
    if (id == KDC_FLUSH) { return KOM_OK; }
    return 2;
}

int Device::ioctl(uint32_t request, void* arg) {
    (void)request; (void)arg;
    return -1;
}

void Device::unlockWrite(uint32_t magic) {
    if (kconfig.lockdown) {
        printf("[SECURITY] Kernel Lockdown is ACTIVE. Write-unlock request for '%s' DENIED.\n", name);
        return;
    }
    if (magic == DEVICE_MAGIC_UNLOCK) {
        writeProtected = false;
    } else {
        printf("[SECURITY] Invalid magic token to unlock device '%s'.\n", name);
    }
}

int Device::read(char* buffer, int size)                              { (void)buffer; (void)size;            return 0; }
int Device::write(const char* buffer, int size)                       { (void)buffer; (void)size;            return 0; }
int Device::readBlock(uint64_t lba, uint32_t count, void* buffer)     { (void)lba; (void)count; (void)buffer; return 0; }
int Device::writeBlock(uint64_t lba, uint32_t count, const void* buf) { (void)lba; (void)count; (void)buf;   return 0; }

int Device::read_vector(uint64_t lba, kiovec_t* vectors, int vector_count) {
    uint64_t current_lba = lba;
    uint32_t block_size  = getBlockSize();
    if (block_size == 0) { return 0; }

    int total = 0;
    for (int i = 0; i < vector_count; i++) {
        if (vectors[i].size == 0 || vectors[i].size % block_size != 0) { return 0; }
        uint32_t count = static_cast<uint32_t>(vectors[i].size / block_size);
        if (readBlock(current_lba, count, vectors[i].virt_addr) != 1) { return 0; }
        current_lba += count;
        total       += static_cast<int>(count);
    }
    return total;
}

int Device::write_vector(uint64_t lba, kiovec_t* vectors, int vector_count) {
    if (isWriteProtected()) {
        printf("[DEV] Blocked vector write to '%s' (Lockdown).\n", name);
        return 0;
    }

    uint64_t current_lba = lba;
    uint32_t block_size  = getBlockSize();
    if (block_size == 0) { return 0; }

    int total = 0;
    for (int i = 0; i < vector_count; i++) {
        if (vectors[i].size == 0 || vectors[i].size % block_size != 0) { return 0; }
        uint32_t count = static_cast<uint32_t>(vectors[i].size / block_size);
        if (writeBlock(current_lba, count, vectors[i].virt_addr) != 1) { return 0; }
        current_lba += count;
        total       += static_cast<int>(count);
    }
    return total;
}

uint64_t Device::getStartLBA() const { return 0; }

void Device::setName(const char* newName) {
    strncpy(name, newName, 31);
    name[31] = '\0';
}

void Device::setModel(const char* newModel) {
    strncpy(model, newModel, 63);
    model[63] = '\0';
}

void Device::setMountPoint(const char* mp) {
    strncpy(mountPoint, mp, 63);
    mountPoint[63] = '\0';
}

void DeviceManager::registerDevice(Device* dev) {
    if (!dev) { return; }

    char path[128];
    snprintf(path, sizeof(path), "/devices/%s", dev->getName());

    dev->ons_register(path);
}

Device* DeviceManager::getDevice(const char* name) {
    char path[128];
    snprintf(path, sizeof(path), "/devices/%s", name);

    KObject* obj = ons_resolve(path);
    if (!obj) { return nullptr; }

    Device* d = nullptr;
    if (obj->type == KObjectType::BLOCK_DEVICE ||
        obj->type == KObjectType::CHAR_DEVICE  ||
        obj->type == KObjectType::GPU_DEVICE)
    {
        d = static_cast<Device*>(obj);
    }
    
    return d;
}

int DeviceManager::getDeviceCount() {
    uint32_t idx = 0;
    char name[64];
    uint8_t type;
    while (ons_enumerate("/devices", idx, name, &type)) { idx++; }
    return static_cast<int>(idx);
}

Device* DeviceManager::getDeviceByIndex(int index) {
    char name[64];
    uint8_t type;
    if (ons_enumerate("/devices", static_cast<uint32_t>(index), name, &type)) {
        return getDevice(name);
    }
    return nullptr;
}

void DeviceManager::getNextSataName(char* buffer) {
    uint32_t c = __atomic_fetch_add(&sata_disk_count, 1, __ATOMIC_SEQ_CST);
    sprintf(buffer, "disk%u", c);
}

void DeviceManager::getNextNvmeName(char* buffer) {
    uint32_t c = __atomic_fetch_add(&nvme_disk_count, 1, __ATOMIC_SEQ_CST);
    sprintf(buffer, "nvme%u", c);
}

extern "C" {

    __attribute__((used, noinline))
    void device_manager_init() {
        print_status("[ DEVICE ]", "Device Manager Ready", "INFO");
    }

    device_t device_get_first_block(void) {
        char name[64];
        uint8_t type;
        uint32_t idx = 0;
        while (ons_enumerate("/devices", idx++, name, &type)) {
            char path[128];
            snprintf(path, sizeof(path), "/devices/%s", name);
            KObject* obj = ons_resolve(path);
            if (!obj) { continue; }
            if (obj->type == KObjectType::BLOCK_DEVICE) {
                Device* d = static_cast<Device*>(obj);
                if (d->getCapacity() > 0) {
                    return d;
                }
            }
            kobject_unref(obj);
        }
        return nullptr;
    }

    device_t device_get(const char* name) {
        return DeviceManager::getDevice(name);
    }

    device_t device_get_by_index(int index) {
        return DeviceManager::getDeviceByIndex(index);
    }

    void device_release(device_t dev) {
        if (dev) kobject_unref(static_cast<KObject*>(dev));
    }

    device_t device_get_first_of_type(uint32_t dtype) {
        uint32_t idx = 0;
        char name[64];
        uint8_t type;
        while (ons_enumerate("/devices", idx++, name, &type)) {
            Device* d = DeviceManager::getDevice(name);
            if (d && static_cast<uint32_t>(d->getType()) == dtype) { return d; }
            device_release(d);
        }
        return nullptr;
    }

    int device_read_block(device_t dev, uint64_t lba, uint32_t count, void* buffer) {
        return static_cast<Device*>(dev)->readBlock(lba, count, buffer);
    }

    int device_write_block(device_t dev, uint64_t lba, uint32_t count, const void* buffer) {
        return static_cast<Device*>(dev)->writeBlock(lba, count, buffer);
    }

    int32_t cpp_device_read_vector(void* dev, uint64_t lba, kiovec_t* vectors, int vector_count) {
        if (!dev || !vectors) { return 0; }
        return static_cast<Device*>(dev)->read_vector(lba, vectors, vector_count);
    }

    int32_t cpp_device_write_vector(void* dev, uint64_t lba, kiovec_t* vectors, int vector_count) {
        if (!dev || !vectors) { return 0; }
        return static_cast<Device*>(dev)->write_vector(lba, vectors, vector_count);
    }

    const char* device_get_name(device_t dev)  { return static_cast<Device*>(dev)->getName();  }
    const char* device_get_model(device_t dev)  { return static_cast<Device*>(dev)->getModel(); }

    void device_print_disks() { rust_device_print_disks(); }
    void device_print_parts() { rust_device_print_parts(); }

    void device_manager_shutdown() {
        char name[64];
        uint8_t type;
        uint32_t idx = 0;
        while (ons_enumerate("/devices", idx++, name, &type)) {
            Device* d = DeviceManager::getDevice(name);
            if (d) { 
                d->shutdown(); 
                device_release(d);
            }
        }
    }

    void device_manager_emergency_stop(void) {
        int count = emergency_device_count;
        if (count > MAX_EMERGENCY_DEVICES) { count = MAX_EMERGENCY_DEVICES; }
        for (int i = 0; i < count; i++) {
            if (emergency_devices[i]) { emergency_devices[i]->emergencyStop(); }
        }
    }

    const char* cpp_device_get_name(void* dev)       { return static_cast<Device*>(dev)->getName();          }
    const char* cpp_device_get_model(void* dev)       { return static_cast<Device*>(dev)->getModel();         }
    const char* cpp_device_get_mountpoint(void* dev)  { return static_cast<Device*>(dev)->getMountPoint();    }
    uint32_t    cpp_device_get_type(void* dev)        { return static_cast<uint32_t>(static_cast<Device*>(dev)->getType()); }
    uint64_t    cpp_device_get_capacity(void* dev)    { return static_cast<Device*>(dev)->getCapacity();      }

    int32_t cpp_device_read_block(void* dev, uint64_t lba, uint32_t count, uint8_t* buf) {
        return static_cast<Device*>(dev)->readBlock(lba, count, buf);
    }

    void cpp_device_shutdown(void* dev) {
        if (dev) { static_cast<Device*>(dev)->shutdown(); }
    }

    const char* cpp_partition_get_gpt_type(void* dev) {
        if (dev && static_cast<Device*>(dev)->getType() == DEV_PARTITION) {
            return static_cast<PartitionDevice*>(dev)->getGptType();
        }
        return nullptr;
    }

    void device_unlock_write(void* dev, uint32_t magic) {
        if (dev) { static_cast<Device*>(dev)->unlockWrite(magic); }
    }

    void device_lock_write(void* dev) {
        if (dev) { static_cast<Device*>(dev)->lockWrite(); }
    }

    bool cpp_device_is_read_only(void* dev) {
        if (dev) { return static_cast<Device*>(dev)->isWriteProtected(); }
        return true;
    }

    uint32_t cpp_device_get_block_size(void* dev) {
        if (dev) { return static_cast<Device*>(dev)->getBlockSize(); }
        return 0;
    }

    uint64_t cpp_device_get_free_space(void* dev) {
        if (dev) { return static_cast<Device*>(dev)->getFreeSpace(); }
        return static_cast<uint64_t>(-1);
    }

    void cpp_device_destroy(void* dev) {
        if (!dev) { return; }
        Device* d = static_cast<Device*>(dev);
        d->ons_unregister();
        kobject_unref(d);
    }

}