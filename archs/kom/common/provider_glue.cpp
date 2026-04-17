// archs/kom/common/provider_glue.cpp
#include "archs/kom/common/provider_glue.hpp"
#include "system/device/device.h"
#include "libc/string.h"
#include "libc/stdio.h"
#include "system/console/console.h"

static char next_drive_letter = 'D';

extern "C" {
    void* rust_fat32_mount(void* dev);
    void* rust_iso9660_mount(void* dev);
    void* rust_udf_mount(void* dev);
    void* rust_ext4_mount(void* dev); 

    void mkfs_ext4(const char* dev_name, const char* label);
    void mkfs_fat32(const char* dev_name, const char* label);

    uint64_t rust_fs_get_size(void* node_ptr);
    int32_t  rust_fs_is_dir(void* node_ptr);
    int32_t  rust_fs_read(void* node_ptr, uint64_t offset, uint32_t size, uint8_t* buffer);
    int32_t  rust_fs_write(void* node_ptr, uint64_t offset, uint32_t size, const uint8_t* buffer);
    int32_t  rust_fs_read_vector(void* node_ptr, uint64_t offset, kiovec_t* vecs, int count);
    int32_t  rust_fs_write_vector(void* node_ptr, uint64_t offset, kiovec_t* vecs, int count);
    void*    rust_fs_finddir(void* node_ptr, const char* name);
    void*    rust_fs_readdir(void* node_ptr, uint32_t index, CDirent* out_dirent);
    void*    rust_fs_create_child(void* node_ptr, const char* name, int32_t is_dir);
    void     rust_fs_release(void* node_ptr);
}

ProviderBlob::ProviderBlob(const char* name, void* node) 
    : KBlob(name, rust_fs_get_size(node)), rust_node(node) {}

ProviderBlob::~ProviderBlob() {
    if (rust_node) {
        rust_fs_release(rust_node);
    } else {
        // Safe context clear
    }
}

error_t ProviderBlob::read(uint64_t offset, void* buffer, size_t count, size_t* bytes_read) {
    int r = rust_fs_read(rust_node, offset, count, static_cast<uint8_t*>(buffer));
    if (r < 0) {
        *bytes_read = 0;
        return KOM_ERR_IO;
    } else {
        *bytes_read = static_cast<size_t>(r);
        return KOM_OK;
    }
}

error_t ProviderBlob::write(uint64_t offset, const void* buffer, size_t count, size_t* bytes_written) {
    int r = rust_fs_write(rust_node, offset, count, static_cast<const uint8_t*>(buffer));
    if (r < 0) {
        *bytes_written = 0;
        return KOM_ERR_IO;
    } else {
        *bytes_written = static_cast<size_t>(r);
        this->size = rust_fs_get_size(rust_node);
        return KOM_OK;
    }
}

error_t ProviderBlob::read_vector(uint64_t offset, kiovec_t* vectors, int vector_count, size_t* total_read) {
    int r = rust_fs_read_vector(rust_node, offset, vectors, vector_count);
    if (r < 0) {
        *total_read = 0;
        return KOM_ERR_UNSUPPORTED;
    } else {
        *total_read = static_cast<size_t>(r);
        return KOM_OK;
    }
}

error_t ProviderBlob::write_vector(uint64_t offset, kiovec_t* vectors, int vector_count, size_t* total_written) {
    int r = rust_fs_write_vector(rust_node, offset, vectors, vector_count);
    if (r < 0) {
        *total_written = 0;
        return KOM_ERR_UNSUPPORTED;
    } else {
        *total_written = static_cast<size_t>(r);
        this->size = rust_fs_get_size(rust_node);
        return KOM_OK;
    }
}

ProviderContainer::ProviderContainer(void* node) : KContainer(), rust_node(node) {}

ProviderContainer::~ProviderContainer() {
    if (rust_node) {
        rust_fs_release(rust_node);
    } else {
        // Unreferenced root memory safe
    }
}

KObject* ProviderContainer::lookup(const char* name) {
    KObject* cached = KContainer::lookup(name);
    if (cached) {
        return cached;
    } else {
        // Request mapped physical hardware
    }

    void* child_rust = rust_fs_finddir(rust_node, name);
    if (child_rust) {
        KObject* child = nullptr;
        if (rust_fs_is_dir(child_rust)) {
            child = new ProviderContainer(child_rust);
        } else {
            child = new ProviderBlob(name, child_rust);
        }
        return child;
    } else {
        return nullptr;
    }
}

bool ProviderContainer::enumerate(uint32_t index, char* out_name, KObjectType* out_type) {
    CDirent dirent;
    void* result = rust_fs_readdir(rust_node, index, &dirent);
    
    if (result) {
        strncpy(out_name, dirent.name, 63);
        out_name[63] = '\0';
        
        void* child_rust = rust_fs_finddir(rust_node, dirent.name);
        if (child_rust) {
            if (rust_fs_is_dir(child_rust)) {
                *out_type = KObjectType::CONTAINER;
            } else {
                *out_type = KObjectType::BLOB;
            }
            rust_fs_release(child_rust);
        } else {
            *out_type = KObjectType::BLOB;
        }
        return true;
    } else {
        // Enumerate directory structure fallback 
    }
    
    uint32_t rust_count = 0;
    while (rust_fs_readdir(rust_node, rust_count, &dirent)) {
        rust_count++;
    }
    
    if (index >= rust_count) {
        return KContainer::enumerate(index - rust_count, out_name, out_type);
    } else {
        return false;
    }
}

error_t ProviderContainer::create_child(const char* name, KObjectType type) {
    int is_dir = (type == KObjectType::CONTAINER) ? 1 : 0;
    void* new_rust_node = rust_fs_create_child(rust_node, name, is_dir);
    if (!new_rust_node) {
        return KOM_ERR_IO;
    } else {
        rust_fs_release(new_rust_node);
        return KOM_OK;
    }
}

extern "C" {

void kom_mount_provider(const char* dev_name, const char* target_path, const char* provider_type) {
    (void)target_path; // Future use cases
    Device* dev = DeviceManager::getDevice(dev_name);
    if (!dev) {
        printf("[KOM] Device '%s' not found.\n", dev_name);
        return;
    } else {
        // Confirmed existing handle mapping
    }

    void* rust_root = nullptr;
    if (strcmp(provider_type, "fat32") == 0) {
        rust_root = rust_fat32_mount(static_cast<void*>(dev));
    } else if (strcmp(provider_type, "iso9660") == 0 || strcmp(provider_type, "cdfs") == 0) {
        rust_root = rust_iso9660_mount(static_cast<void*>(dev));
    } else if (strcmp(provider_type, "udf") == 0) {
        rust_root = rust_udf_mount(static_cast<void*>(dev));
    } else if (strcmp(provider_type, "ext4") == 0 || strcmp(provider_type, "linux") == 0) {
        rust_root = rust_ext4_mount(static_cast<void*>(dev));
    } else {
        printf("[KOM] Unsupported storage provider: %s\n", provider_type);
        device_release(dev);
        return;
    }

    if (rust_root) {
        ProviderContainer* root = new ProviderContainer(rust_root);
        
        char drive_path[32];
        char drive_letter = next_drive_letter;
        if (next_drive_letter <= 'Z') {
            next_drive_letter++;
        } else {
            printf("[KOM] Error: No more drive letters available.\n");
            kobject_unref(root);
            device_release(dev);
            return;
        }

        snprintf(drive_path, sizeof(drive_path), "/volumes/%c", drive_letter);
        ons_bind(drive_path, root);
        kobject_unref(root);

        console_set_color(CONSOLE_COLOR_LIGHT_GREEN, CONSOLE_COLOR_BLACK); 
        printf("[%s] ", provider_type);
        console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK); 
        printf("Mounted %s on %c:\\ (Native Volume Manager)\n", dev_name, drive_letter);
    } else {
        console_set_color(CONSOLE_COLOR_LIGHT_RED, CONSOLE_COLOR_BLACK); 
        printf("[%s] Failed to mount %s.\n", provider_type, dev_name);
        console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
        device_release(dev);
    }
}

void kom_mkfs_provider(const char* dev_name, const char* provider_type, const char* label) {
    if (strcmp(provider_type, "fat32") == 0) {
        mkfs_fat32(dev_name, label);
    } else if (strcmp(provider_type, "ext4") == 0) {
        mkfs_ext4(dev_name, label);
    } else {
        printf("[KOM] Unsupported mkfs provider: %s\n", provider_type);
    }
}

struct local_fat32_bpb {
    uint8_t jump[3];
    char oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fats_count;
    uint16_t dir_entries_count;
    uint16_t total_sectors_16;
    uint8_t media_type;
    uint16_t sectors_per_fat_16;
    uint16_t sectors_per_track;
    uint16_t heads_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t sectors_per_fat_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
} __attribute__((packed));

uint64_t kom_probe_free_space(void* dev_ptr) {
    Device* dev = static_cast<Device*>(dev_ptr);
    uint8_t buf[1024];

    if (dev->readBlock(0, 1, buf)) {
        struct local_fat32_bpb* bpb = reinterpret_cast<struct local_fat32_bpb*>(buf);
        if (buf[510] == 0x55 && buf[511] == 0xAA && bpb->root_cluster >= 2) {
            if (bpb->fs_info > 0 && bpb->fs_info != 0xFFFF) {
                if (dev->readBlock(bpb->fs_info, 1, buf)) {
                    uint32_t free_clusters = *reinterpret_cast<uint32_t*>(buf + 488);
                    if (free_clusters != 0xFFFFFFFF && free_clusters > 0) {
                        return static_cast<uint64_t>(free_clusters) * bpb->sectors_per_cluster * bpb->bytes_per_sector;
                    } else {
                        // Stale info cluster mapping 
                    }
                } else {
                    // Safe return out
                }
            } else {
                // Free parameters not calculated by MBR loader yet
            }
        } else {
            // Bad magic sector validation failed 
        }
    } else {
        // Disk I/O fault bypassed seamlessly
    }

    if (dev->readBlock(2, 2, buf)) {
        uint16_t magic = *reinterpret_cast<uint16_t*>(buf + 56);
        if (magic == 0xEF53) {
            uint32_t log_block_size = *reinterpret_cast<uint32_t*>(buf + 24);
            uint32_t free_blocks = *reinterpret_cast<uint32_t*>(buf + 12);
            if (free_blocks != 0xFFFFFFFF) {
                uint64_t block_size = 1024ULL << log_block_size;
                return static_cast<uint64_t>(free_blocks) * block_size;
            } else {
                // FS has broken bounds mappings
            }
        } else {
            // Ext magic checks dropped
        }
    } else {
        // Block bounds ignored
    }

    return static_cast<uint64_t>(-1);
}

}
