// archs/cpu/x86_64/core/multiboot.h
#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include <stdint.h>

#define MULTIBOOT_TAG_TYPE_END              0
#define MULTIBOOT_TAG_TYPE_CMDLINE          1
#define MULTIBOOT_TAG_TYPE_BOOT_LOADER_NAME 2
#define MULTIBOOT_TAG_TYPE_MODULE           3
#define MULTIBOOT_TAG_TYPE_BASIC_MEMINFO    4
#define MULTIBOOT_TAG_TYPE_BOOTDEV          5
#define MULTIBOOT_TAG_TYPE_MMAP             6
#define MULTIBOOT_TAG_TYPE_VBE              7
#define MULTIBOOT_TAG_TYPE_FRAMEBUFFER      8
#define MULTIBOOT_TAG_TYPE_ELF_SECTIONS     9
#define MULTIBOOT_TAG_TYPE_APM              10
#define MULTIBOOT_TAG_TYPE_EFI32            11
#define MULTIBOOT_TAG_TYPE_EFI64            12

// BUG-FIX: SMBIOS tag is 13, NOT 15.
#define MULTIBOOT_TAG_TYPE_SMBIOS           13

#define MULTIBOOT_TAG_TYPE_ACPI_OLD         14
#define MULTIBOOT_TAG_TYPE_ACPI_NEW         15

struct multiboot_tag {
    uint32_t type;
    uint32_t size;
};

struct multiboot_tag_string {
    uint32_t type;
    uint32_t size;
    char string[];
};

struct multiboot_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t zero;
} __attribute__((packed));

struct multiboot_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    struct multiboot_mmap_entry entries[];
};

struct multiboot_tag_basic_meminfo {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower;
    uint32_t mem_upper;
};

struct multiboot_tag_module {
    uint32_t type;
    uint32_t size;
    uint32_t mod_start;
    uint32_t mod_end;
    char cmdline[];
};

struct multiboot_tag_smbios {
    uint32_t type;
    uint32_t size;
    uint8_t  major;
    uint8_t  minor;
    uint8_t  reserved[6];
    uint8_t  tables[];
} __attribute__((packed));

struct multiboot_tag_efi64 {
    uint32_t type;
    uint32_t size;
    uint64_t pointer;
};

struct multiboot_tag_old_acpi {
    uint32_t type;
    uint32_t size;
    uint8_t  rsdp[];
};

struct multiboot_tag_new_acpi {
    uint32_t type;
    uint32_t size;
    uint8_t  rsdp[];
};

struct multiboot_tag_framebuffer_common {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint16_t reserved;
};

struct multiboot_tag_framebuffer {
    struct multiboot_tag_framebuffer_common common;

    union {
        struct {
            uint16_t framebuffer_palette_num_colors;
        };
        struct {
            uint8_t framebuffer_red_field_position;
            uint8_t framebuffer_red_mask_size;
            uint8_t framebuffer_green_field_position;
            uint8_t framebuffer_green_mask_size;
            uint8_t framebuffer_blue_field_position;
            uint8_t framebuffer_blue_mask_size;
        };
    };
};

#endif
