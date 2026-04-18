// archs/kom/common/provider_glue.hpp
#ifndef PROVIDER_GLUE_HPP
#define PROVIDER_GLUE_HPP

#include "archs/kom/kom_aal.h"
#include "system/device/device.h"

#ifdef __cplusplus
struct CDirent {
    char name[128];
    uint32_t inode;
};

class ProviderBlob : public KBlob {
private:
    void* rust_node;
public:
    ProviderBlob(const char* name, void* node);
    ~ProviderBlob() override;

    error_t read(uint64_t offset, void* buffer, size_t count, size_t* bytes_read) override;
    error_t write(uint64_t offset, const void* buffer, size_t count, size_t* bytes_written) override;
    error_t read_vector(uint64_t offset, kiovec_t* vectors, int vector_count, size_t* total_read) override;
    error_t write_vector(uint64_t offset, kiovec_t* vectors, int vector_count, size_t* total_written) override;
};

class ProviderContainer : public KContainer {
private:
    void* rust_node;
public:
    ProviderContainer(void* node);
    ~ProviderContainer() override;

    KObject* lookup(const char* name) override;
    bool enumerate(uint32_t index, char* out_name, KObjectType* out_type) override;
    error_t create_child(const char* name, KObjectType type) override;
};
#endif // __cplusplus

#ifdef __cplusplus
extern "C" {
#endif

void kom_mount_provider(const char* dev_name, const char* target_path, const char* provider_type);
void kom_mkfs_provider(const char* dev_name, const char* provider_type, const char* label);
uint64_t kom_probe_free_space(void* dev_ptr);

#ifdef __cplusplus
}
#endif

#endif