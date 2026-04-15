// system/device/device.h
#ifndef DEVICE_H
#define DEVICE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// AAL MIGRATION: KOM mimarisi artık AAL üzerinden soyutlanmıştır.
#include "archs/kom/kom_aal.h" 

#ifdef __cplusplus
#include "system/sync/rwlock.h" 
#endif

#define DEVICE_MAGIC_UNLOCK 0x1337BEEF

typedef enum {
    DEV_UNKNOWN = 0,
    DEV_KEYBOARD,
    DEV_MOUSE,
    DEV_DISPLAY,
    DEV_BLOCK,      
    DEV_PARTITION,  
    DEV_SERIAL,
    DEV_CHAR        
} DeviceType;

#ifdef __cplusplus

class Device : public KDevice {
protected:
    char name[32];
    char model[64]; 
    char mountPoint[64]; 
    DeviceType type;
    uint64_t capacityBytes; 
    
    bool writeProtected;
    rwlock_t vfs_lock; 

public:
    Device(const char* devName, DeviceType devType);
    virtual ~Device() override; 

    __attribute__((used, noinline)) virtual int init() = 0;
    
    __attribute__((used)) virtual int read(char* buffer, int size);
    __attribute__((used)) virtual int write(const char* buffer, int size);
    
    __attribute__((used)) virtual int readBlock(uint64_t lba, uint32_t count, void* buffer);
    __attribute__((used)) virtual int writeBlock(uint64_t lba, uint32_t count, const void* buffer);
    
    __attribute__((used)) virtual int read_vector(uint64_t lba, kiovec_t* vectors, int vector_count);
    __attribute__((used)) virtual int write_vector(uint64_t lba, kiovec_t* vectors, int vector_count);

    virtual int readOffset(uint64_t offset, uint32_t size, uint8_t* buffer) {
        (void)offset;
        return read((char*)buffer, size);
    }
    
    virtual int writeOffset(uint64_t offset, uint32_t size, const uint8_t* buffer) {
        (void)offset;
        return write((const char*)buffer, size);
    }

    error_t query(KDeviceQueryID id, void* out, size_t out_len) override;
    error_t control(KDeviceControlID id, const void* in, size_t in_len) override;

    virtual int ioctl(uint32_t request, void* arg);

    __attribute__((used)) virtual int shutdown() { return 0; }
    __attribute__((used)) virtual void emergencyStop() { }
    
    virtual uint64_t getStartLBA() const;
    virtual uint32_t getBlockSize() const { return 512; }
    
    virtual uint64_t getFreeSpace() { return (uint64_t)-1; }

    const char* getName() const { return name; }
    const char* getModel() const { return model; } 
    const char* getMountPoint() const { return mountPoint; } 
    rwlock_t getVFSLock() const { return vfs_lock; } 
    
    DeviceType getType() const { return type; }
    uint64_t getCapacity() const { return capacityBytes; }
    
    void setName(const char* newName);
    void setModel(const char* newModel); 
    void setMountPoint(const char* mp); 
    void setCapacity(uint64_t bytes) { capacityBytes = bytes; }

    bool isWriteProtected() const { return writeProtected; }
    void lockWrite() { writeProtected = true; }
    void unlockWrite(uint32_t magic);
};

namespace DeviceManager {
    void registerDevice(Device* dev); 
    Device* getDevice(const char* name);
    Device* getDeviceByIndex(int index);
    int getDeviceCount();
    void getNextSataName(char* buffer);
    void getNextNvmeName(char* buffer);
}

typedef Device* device_handle_t;

#else

struct Device_Opaque;
typedef struct Device_Opaque* device_handle_t;

#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef device_handle_t device_t; 

void device_manager_init(void);
void device_print_disks(void); 
void device_print_parts(void); 
void device_manager_shutdown(void);

device_t device_get(const char* name);
void device_release(device_t dev); 
device_t device_get_first_block(void); 
int device_read_block(device_t dev, uint64_t lba, uint32_t count, void* buffer);
int device_write_block(device_t dev, uint64_t lba, uint32_t count, const void* buffer);

int32_t cpp_device_read_vector(void* dev, uint64_t lba, kiovec_t* vectors, int vector_count);
int32_t cpp_device_write_vector(void* dev, uint64_t lba, kiovec_t* vectors, int vector_count);

const char* device_get_name(device_t dev);
const char* device_get_model(device_t dev); 

uint64_t cpp_device_get_free_space(void* dev); 

void init_virtual_devices(void);

#ifdef __cplusplus
}
#endif

#endif