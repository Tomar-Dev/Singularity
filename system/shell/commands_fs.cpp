// system/shell/commands_fs.cpp
#include "system/shell/shell.hpp"
#include "libc/stdio.h"
#include "libc/string.h"
#include "archs/kom/common/provider_glue.hpp"
#include "archs/kom/common/kblob.hpp"
#include "archs/kom/common/kcontainer.hpp"
#include "system/device/partition.h"
#include "kernel/config.h"
#include "system/console/console.h"
#include "archs/kom/kom_aal.h"
#include "archs/memory/kheap.h"
#include "system/ffi/ffi.h"

extern "C" {
    int gpt_create_partition(const char* dev_name, uint32_t size_mb, const char* name, const char* type, char* out_created_name);
    int gpt_delete_partition(const char* part_name);
    int rust_gpt_init_disk(const char* dev_name);

    void console_set_auto_flush(bool enabled);
    void rust_device_print_disks(void);
    void rust_device_print_parts(void);
}

int Shell::cmd_ls(const char* arg) {
    console_set_auto_flush(false); 

    char fullPath[256];
    if (arg && arg[0] != '\0') { resolveAbsolutePath(arg, fullPath); }
    else { resolveAbsolutePath(".", fullPath); }

    KObject* obj = ons_resolve(fullPath);

    if (!obj) {
        printf("ls: %s: No such file or directory\n", fullPath);
    } else if (obj->type == KObjectType::CONTAINER) {
        printf("Contents of %s:\n", fullPath);
        KContainer* dir = (KContainer*)obj;
        char name[64];
        KObjectType t;
        uint32_t idx = 0;

        while (dir->enumerate(idx++, name, &t)) {
            if (t == KObjectType::CONTAINER) { console_set_color(CONSOLE_COLOR_LIGHT_BLUE, CONSOLE_COLOR_BLACK); }
            else if (t == KObjectType::BLOCK_DEVICE) { console_set_color(CONSOLE_COLOR_YELLOW, CONSOLE_COLOR_BLACK); }
            else if (t == KObjectType::CHAR_DEVICE) { console_set_color(CONSOLE_COLOR_LIGHT_CYAN, CONSOLE_COLOR_BLACK); }
            else { console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK); }
            printf("  %s\n", name);
        }
        console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    } else {
        char name_buf[64];
        ons_resolve_parent(fullPath, name_buf);
        printf("%s\n", name_buf);
    }

    if (obj) { kobject_unref(obj); } else { /* Null Bypass */ }
    console_set_auto_flush(true); 
    return 0;
}

int Shell::cmd_cat(const char* arg) {
    if (!arg || arg[0] == '\0') { printf("Usage: cat <file1> [file2...]\n"); return 1; } else { /* Safe */ }
    console_set_auto_flush(false); 

    char args_copy[256];
    strncpy(args_copy, arg, 255);
    args_copy[255] = '\0';

    char* p = args_copy;
    char* token;
    int ret = 0;

    while (*p) {
        while (*p == ' ') { p++; }
        if (*p == '\0') { break; } else { /* Token Valid */ }
        token = p;
        while (*p && *p != ' ') { p++; }
        if (*p) { *p = '\0'; p++; } else { /* End reached */ }

        char fullPath[256];
        resolveAbsolutePath(token, fullPath);

        KObject* obj = ons_resolve(fullPath);
        if (!obj) {
            printf("cat: %s: No such file or directory\n", token);
            ret = 1;
        } else if (obj->type == KObjectType::CONTAINER) {
            printf("cat: %s: Is a directory\n", token);
            ret = 1;
            kobject_unref(obj);
        } else if (obj->type == KObjectType::BLOB) {
            KBlob* blob = (KBlob*)obj;
            size_t sz = (size_t)blob->getSize();
            if (sz == 0) { sz = 4096; } else { /* Size loaded */ }
            if (sz > 1024 * 1024) {
                printf("cat: %s: File too large to display\n", token);
                ret = 1;
            } else {
                uint8_t* buf = (uint8_t*)kmalloc(sz + 1);
                if (buf) {
                    size_t read_bytes = 0;
                    blob->read(0, buf, sz, &read_bytes);
                    buf[read_bytes] = '\0';
                    printf("%s", buf);
                    if (read_bytes > 0 && buf[read_bytes - 1] != '\n') { printf("\n"); } else { /* Native LF */ }
                    kfree(buf);
                } else {
                    printf("cat: Out of memory\n");
                    ret = 1;
                }
            }
            kobject_unref(obj);
        } else {
            kobject_unref(obj);
        }
    }

    console_set_auto_flush(true); 
    return ret;
}

int Shell::cmd_cd(const char* arg) {
    if (!arg || arg[0] == '\0') {
        strncpy(currentPath, "C:\\", 255);
        currentPath[255] = '\0';
        return 0;
    } else {
        char targetPath[256];
        resolveAbsolutePath(arg, targetPath);

        KObject* obj = ons_resolve(targetPath);
        if (obj && obj->type == KObjectType::CONTAINER) {
            strncpy(currentPath, targetPath, 255);
            currentPath[255] = '\0';
            int len = (int)strlen(currentPath);
            if (currentPath[len - 1] != '\\' && currentPath[len - 1] != '/' && len < 255) {
                strncat(currentPath, "\\", 2);
            } else {
                // Correctly terminated
            }
            kobject_unref(obj);
            return 0;
        } else {
            printf("cd: Invalid directory: %s\n", arg);
            if (obj) { kobject_unref(obj); } else { /* Freed implicitly */ }
            return 1;
        }
    }
}

int Shell::cmd_mkdir(const char* arg) {
    if (!arg || arg[0] == '\0') { printf("Usage: mkdir <n>\n"); return 1; } else { /* Bounds Pass */ }
    char fullPath[256];
    resolveAbsolutePath(arg, fullPath);
    char filename[128];

    KObject* parent = ons_resolve_parent(fullPath, filename);
    if (parent && parent->type == KObjectType::CONTAINER) {
        error_t res = ((KContainer*)parent)->create_child(filename, KObjectType::CONTAINER);
        if (res == KOM_ERR_UNSUPPORTED) {
            KContainer* nc = new KContainer();
            res = ((KContainer*)parent)->bind(filename, nc);
            kobject_unref(nc);
        } else {
            // Supported natively by driver 
        }
        if (res == KOM_OK) {
            printf("Directory '%s' created.\n", filename);
            kobject_unref(parent);
            return 0;
        } else {
            printf("Error: Could not create directory.\n");
        }
    } else {
        printf("Error: Parent directory not found.\n");
    }
    if (parent) { kobject_unref(parent); } else { /* Escaped */ }
    return 1;
}

int Shell::cmd_touch(const char* arg) {
    if (!arg || arg[0] == '\0') { printf("Usage: touch <n>\n"); return 1; } else { /* Pass */ }
    char fullPath[256];
    resolveAbsolutePath(arg, fullPath);
    char filename[128];

    KObject* parent = ons_resolve_parent(fullPath, filename);
    if (parent && parent->type == KObjectType::CONTAINER) {
        error_t res = ((KContainer*)parent)->create_child(filename, KObjectType::BLOB);
        if (res == KOM_ERR_UNSUPPORTED) {
            KVolatileBlob* vb = new KVolatileBlob(filename);
            res = ((KContainer*)parent)->bind(filename, vb);
            kobject_unref(vb);
        } else {
            // Supported natively by driver 
        }
        if (res == KOM_OK) {
            printf("File '%s' created.\n", filename);
            kobject_unref(parent);
            return 0;
        } else {
            printf("Error: Could not create file.\n");
        }
    } else {
        printf("Error: Parent directory not found.\n");
    }
    if (parent) { kobject_unref(parent); } else { /* Valid */ }
    return 1;
}

int Shell::cmd_write(const char* arg) {
    if (!arg || arg[0] == '\0') { printf("Usage: write <filename> <text...>\n"); return 1; } else { /* Proceed */ }

    char argcopy[256];
    strncpy(argcopy, arg, 255);
    argcopy[255] = '\0';

    char* filename_arg = argcopy;
    char* text_arg = nullptr;
    char* p = filename_arg;
    while (*p && *p != ' ') { p++; }
    if (*p == ' ') {
        *p = '\0';
        text_arg = p + 1;
    } else {
        // Missing parameter fields
    }

    if (!text_arg) { printf("Usage: write <filename> <text...>\n"); return 1; } else { /* Executing stream buffer */ }

    char fullPath[256];
    resolveAbsolutePath(filename_arg, fullPath);

    KObject* obj = ons_resolve(fullPath);
    if (obj) {
        if (obj->type == KObjectType::CONTAINER) {
            printf("Error: Cannot write to directory.\n");
            kobject_unref(obj);
            return 1;
        } else if (obj->type == KObjectType::BLOB) {
            size_t written = 0;
            ((KBlob*)obj)->write(0, text_arg, strlen(text_arg), &written);
            if (written > 0) {
                printf("Written %lu bytes to %s\n", (uint64_t)written, filename_arg);
                kobject_unref(obj);
                return 0;
            } else {
                printf("Error: Write failed.\n");
            }
        } else {
            // Unhandled object
        }
        kobject_unref(obj);
        return 1;
    } else {
        printf("Error: File not found.\n");
        return 1;
    }
}

int Shell::cmd_ramdisk(const char* arg) {
    (void)arg;
    printf("Creating Volatile Container on /ramdisk...\n");
    KContainer* ramfs_root = new KContainer();
    ons_bind("/ramdisk", ramfs_root);
    kobject_unref(ramfs_root);
    printf("Volatile Storage mapped at /ramdisk (Native KOM)\n");
    return 0;
}

int Shell::cmd_automount(const char* arg) {
    (void)arg;
    printf("Scanning for mountable partitions...\n");
    char name[64];
    uint8_t type;
    uint32_t idx = 0;
    int mounted = 0;
    
    while (ons_enumerate("/devices", idx++, name, &type)) {
        char path[128];
        snprintf(path, sizeof(path), "/devices/%s", name);
        KObject* obj = ons_resolve(path);
        if (!obj) { continue; } else { /* Loaded active Object */ }
        
        if (obj->type == KObjectType::BLOCK_DEVICE) {
            Device* d = (Device*)obj;
            if (d->getType() == DEV_PARTITION || d->getType() == DEV_BLOCK) {
                uint32_t fs_val = rust_device_detect_fs((void*)d);
                
                if (fs_val != 0 && fs_val != 5) {
                    const char* fs_type = "";
                    if (fs_val == 1) { fs_type = "fat32"; }
                    else if (fs_val == 2) { fs_type = "ext4"; }
                    else if (fs_val == 3) { fs_type = "iso9660"; }
                    else if (fs_val == 4) { fs_type = "udf"; } else { /* Unrecognized structure string */ }

                    printf("Found %s on %s, mounting...\n", fs_type, name);
                    kom_mount_provider(name, "", fs_type);
                    mounted++;
                } else {
                    // Unknown or formatting error
                }
            } else {
                // Not mapped storage block
            }
        } else {
            // Ignored
        }
        kobject_unref(obj);
    }
    
    printf("Automount complete. %d volumes mapped to Drive Letters.\n", mounted);
    return 0;
}

int Shell::cmd_mount(const char* arg) {
    char argcopy[256];
    if (arg) {
        strncpy(argcopy, arg, 255);
        argcopy[255] = '\0';
    } else {
        argcopy[0] = '\0';
    }

    char* dev_name = argcopy;
    char* target_path = nullptr;
    char* fs_type = nullptr;

    if (dev_name[0] != '\0') {
        char* p = dev_name;
        while (*p && *p != ' ') { p++; }
        if (*p == ' ') {
            *p = '\0';
            target_path = p + 1;
            p = target_path;
            while (*p && *p != ' ') { p++; }
            if (*p == ' ') {
                *p = '\0';
                fs_type = p + 1;
            } else {
                // Passed
            }
        } else {
            // Passed
        }
    } else {
        dev_name = nullptr;
    }

    if (!dev_name) {
        printf("Usage: mount <dev> [path][fs_type]\nIf path is omitted, NT Drive Letter (e.g. D:) will be mapped.\n");
        return 1;
    } else {
        // Valid execution length
    }

    Device* dev_obj = DeviceManager::getDevice(dev_name);
    if (!dev_obj) {
        printf("Device '%s' not found.\n", dev_name);
        return 1;
    } else {
        // Active
    }

    char auto_path[128];
    if (!target_path || target_path[0] == '\0') {
        auto_path[0] = '\0'; 
        target_path = auto_path;
    } else {
        // Mounted correctly on specific paths
    }

    if (!fs_type || fs_type[0] == '\0') {
        uint32_t fs_val = rust_device_detect_fs((void*)dev_obj);
        if (fs_val == 0 || fs_val == 5) {
            printf("Auto-detection failed. Could not determine filesystem on '%s'.\n", dev_name);
            device_release(dev_obj);
            return 1;
        } else {
            if (fs_val == 1) { fs_type = (char*)"fat32"; }
            else if (fs_val == 2) { fs_type = (char*)"ext4"; }
            else if (fs_val == 3) { fs_type = (char*)"iso9660"; }
            else if (fs_val == 4) { fs_type = (char*)"udf"; } else { /* Clean */ }
            printf("Auto-detected Filesystem: %s\n", fs_type);
        }
    } else {
        // Overwritten by manual FS parameter override
    }

    kom_mount_provider(dev_name, target_path, fs_type);
    device_release(dev_obj);
    return 0;
}

int Shell::cmd_mkfs(const char* arg) {
    if (kconfig.lockdown) {
        console_set_color(CONSOLE_COLOR_LIGHT_RED, CONSOLE_COLOR_BLACK);
        printf("\n[SECURITY] DISK FORMATTING BLOCKED!\n");
        console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
        printf("MeowOS is in Strict Lockdown mode. Reboot with 'lockdown=0' to format.\n\n");
        return 1;
    } else {
        // Executing format array
    }

    char argcopy[256];
    if (arg) {
        strncpy(argcopy, arg, 255);
        argcopy[255] = '\0';
    } else {
        argcopy[0] = '\0';
    }

    char* dev = argcopy;
    char* type = nullptr;
    char* label = nullptr;
    if (dev[0] != '\0') {
        char* p = dev;
        while (*p && *p != ' ') { p++; }
        if (*p == ' ') {
            *p = '\0';
            type = p + 1;
            p = type;
            while (*p && *p != ' ') { p++; }
            if (*p == ' ') {
                *p = '\0';
                label = p + 1;
            } else {
                // Invalid sequence
            }
        } else {
            // Invalid argument length
        }
    } else {
        dev = nullptr;
    }

    if (!dev || !type || !label) {
        printf("Usage: mkfs <device> <type> <label>\n");
        return 1;
    } else {
        printf("Formatting %s as %s...\n", dev, type);
        kom_mkfs_provider(dev, type, label);
    }
    return 0;
}

int Shell::cmd_fdisk(const char* arg) {
    if (kconfig.lockdown) {
        console_set_color(CONSOLE_COLOR_LIGHT_RED, CONSOLE_COLOR_BLACK);
        printf("\n[SECURITY] DISK PARTITIONING BLOCKED!\n");
        console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
        printf("MeowOS is in Strict Lockdown mode. Reboot with 'lockdown=0' to use fdisk.\n\n");
        return 1;
    } else {
        // Validating fdisk procedure
    }

    char argcopy[256];
    if (arg) {
        strncpy(argcopy, arg, 255);
        argcopy[255] = '\0';
    } else {
        argcopy[0] = '\0';
    }

    char* op = argcopy;
    char* params = nullptr;
    if (op[0] != '\0') {
        char* p = op;
        while (*p && *p != ' ') { p++; }
        if (*p == ' ') {
            *p = '\0';
            params = p + 1;
        } else {
            // No parameters provided
        }
    } else {
        op = nullptr;
    }

    if (!op || op[0] == '\0') {
        printf("Usage:\n  fdisk init <disk>\n  fdisk create <disk> <size_mb> <n>[type]\n  fdisk delete <partition>\n");
        return 1;
    } else if (strcmp(op, "init") == 0) {
        if (!params || params[0] == '\0') { printf("Usage: fdisk init <disk>\n"); return 1; } else { /* Pass */ }

        printf("Initializing disk %s with a new GPT table...\n", params);
        if (rust_gpt_init_disk(params) == 1) {
            printf("Disk initialized successfully.\n");
            return 0;
        } else {
            printf("Failed to initialize disk.\n");
            return 1;
        }
    } else if (strcmp(op, "create") == 0) {
        char* dev_name = params;
        char* size_str = nullptr;
        char* name_str = nullptr;
        const char* type_str = "ext4";

        if (dev_name) {
            char* p = dev_name;
            while (*p && *p != ' ') { p++; }
            if (*p == ' ') { *p = '\0'; size_str = p + 1; } else { /* Parameter invalid */ }
        } else { /* Passed */ }
        if (size_str) {
            char* p = size_str;
            while (*p && *p != ' ') { p++; }
            if (*p == ' ') { *p = '\0'; name_str = p + 1; } else { /* Skipped */ }
        } else { /* Passed */ }
        if (name_str) {
            char* p = name_str;
            while (*p && *p != ' ') { p++; }
            if (*p == ' ') { *p = '\0'; type_str = p + 1; } else { /* Clean */ }
        } else { /* Executed standard parsing checks */ }

        if (dev_name && size_str && name_str) {
            uint64_t size_mb = 0;
            for (int k = 0; size_str[k]; k++) {
                if (size_str[k] >= '0' && size_str[k] <= '9') {
                    size_mb = size_mb * 10 + (uint64_t)(size_str[k] - '0');
                } else {
                    break;
                }
            }
            if (size_mb > 0 && size_mb <= 0xFFFFFFFFULL) {
                char new_part_name[32];
                memset(new_part_name, 0, 32);
                int ret = gpt_create_partition(dev_name, (uint32_t)size_mb,
                                              name_str, type_str, new_part_name);
                if (ret && new_part_name[0] != '\0') {
                    if (strcmp(type_str, "fat32") == 0) {
                        kom_mkfs_provider(new_part_name, "fat32", name_str);
                    } else if (strcmp(type_str, "ext4") == 0) {
                        kom_mkfs_provider(new_part_name, "ext4", name_str);
                    } else {
                        // Native raw GPT space
                    }
                } else {
                    // Internal function crash blocked
                }
                return (ret == 1) ? 0 : 1;
            } else if (size_mb == 0) {
                printf("Invalid size: must be greater than 0 MB.\n");
            } else {
                printf("Error: Partition size too large for this command.\n");
            }
        } else {
            printf("Missing parameters.\n");
        }
        return 1;
    } else if (strcmp(op, "delete") == 0) {
        if (params && params[0] != '\0') {
            int ret = gpt_delete_partition(params);
            return (ret == 1) ? 0 : 1;
        } else {
            printf("Usage: fdisk delete <partition>\n");
            return 1;
        }
    } else {
        printf("Unknown fdisk subcommand: %s\n", op);
        return 1;
    }
}

int Shell::cmd_disks(const char* arg) {
    (void)arg;
    console_set_auto_flush(false);
    rust_device_print_disks();
    console_set_auto_flush(true);
    return 0;
}

int Shell::cmd_parts(const char* arg) {
    (void)arg;
    console_set_auto_flush(false);
    rust_device_print_parts();
    console_set_auto_flush(true);
    return 0;
}
