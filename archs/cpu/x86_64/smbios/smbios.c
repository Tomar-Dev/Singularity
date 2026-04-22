// drivers/smbios/smbios.c
#include "archs/cpu/cpu_hal.h"
#include "libc/string.h"
#include "libc/stdio.h"
#include "archs/cpu/x86_64/core/multiboot.h"
#include "memory/paging.h"
#include "drivers/uefi/uefi.h" // YENİ
#include <stdbool.h>

static uint64_t smbios_table_addr    = 0;
static uint32_t smbios_table_len     = 0;
static int      smbios_version_major = 0;
static int      smbios_version_minor = 0;
static int      smbios_num_structs   = 0;

static void* smbios_table_virt = NULL;

static char smbios_bios_vendor[64]      = {0};
static char smbios_bios_version[64]     = {0};
static char smbios_sys_manufacturer[64] = {0};
static char smbios_sys_product[64]      = {0};
static char smbios_board_product[64]    = {0};
static char smbios_cpu_socket[64]       = {0};

static const char* safe_str(const char* s) {
    if (s && s[0] != '\0') {
        return s;
    } else {
        return "Unknown";
    }
}

static inline const uint8_t* smbios_table_end(void) {
    return (const uint8_t*)smbios_table_virt + smbios_table_len;
}

static uint8_t smbios_checksum(const uint8_t* data, size_t len) {
    if (!data || len == 0) {
        return 1; 
    } else {
        uint8_t sum = 0;
        for (size_t i = 0; i < len; i++) {
            sum = (uint8_t)(sum + data[i]);
        }
        return sum;
    }
}

static bool verify_smbios_checksum(const uint8_t* data, size_t len) {
    if (!data || len == 0) {
        return false;
    } else {
        return (smbios_checksum(data, len) == 0);
    }
}

static void* smbios_advance_struct(const uint8_t* ptr, const uint8_t* end) {
    if (!ptr || ptr >= end) {
        return NULL;
    } else {
        uint8_t length = ptr[1];
        if (length == 0) {
            return NULL;
        } else {
            const uint8_t* p = ptr + length;
            
            while (p < end) {
                if (*p == 0) {
                    if ((p + 1) < end && *(p + 1) == 0) {
                        return (void*)(p + 2);
                    } else {
                        // Keep parsing
                    }
                } else {
                    // Character string
                }
                p++;
            }
            return NULL;
        }
    }
}

static uint8_t* smbios_advance_to_next(const uint8_t* current) {
    if (!current || !smbios_table_virt) {
        return NULL;
    } else {
        return (uint8_t*)smbios_advance_struct(current, smbios_table_end());
    }
}

static const char* get_smbios_string(const struct smbios_header* hdr, uint8_t str_id) {
    if (!hdr || str_id == 0) {
        return NULL;
    } else {
        const uint8_t* t_end = smbios_table_end();
        const uint8_t* ptr   = (const uint8_t*)hdr + hdr->length;

        if (ptr >= t_end) {
            return NULL;
        } else {
            for (uint8_t i = 1; i < str_id; i++) {
                if (ptr >= t_end) { return NULL; } else { /* Valid */ }
                
                if (*ptr == 0) {
                    ptr++;
                } else {
                    while (ptr < t_end && *ptr != 0) { ptr++; }
                    if (ptr >= t_end) { return NULL; } else { /* Valid */ }
                    ptr++;
                }
            }

            if (ptr >= t_end || *ptr == 0) {
                return NULL;
            } else {
                return (const char*)ptr;
            }
        }
    }
}

static bool smbios_map_table(void) {
    if (smbios_table_addr == 0 || smbios_table_len == 0) {
        return false;
    } else {
        smbios_table_virt = ioremap(smbios_table_addr, smbios_table_len, PAGE_PRESENT | PAGE_NX); // SEC-002 ReadOnly 
        if (!smbios_table_virt) {
            serial_write("[SMBIOS] ioremap failed for structure table.\n");
            return false;
        } else {
            char msg[96];
            snprintf(msg, sizeof(msg), "[SMBIOS] Table mapped RO: virt=0x%lx phys=0x%lx len=%u\n",
                     (uint64_t)smbios_table_virt, smbios_table_addr, smbios_table_len);
            serial_write(msg);
            return true;
        }
    }
}

static void smbios_preparse_identity(void) {
    if (!smbios_table_virt) { return; } else { /* Valid */ }

    const uint8_t* tp    = (const uint8_t*)smbios_table_virt;
    const uint8_t* t_end = smbios_table_end();
    int count = 0;
    int limit = (smbios_num_structs > 0) ? smbios_num_structs : 512;

    while (tp && tp < t_end && count < limit) {
        const struct smbios_header* hdr = (const struct smbios_header*)tp;

        if (hdr->length < 4 || hdr->type == 127) {
            break;
        } else if ((tp + hdr->length) > t_end) {
            break;
        } else {
            switch (hdr->type) {
                case 0: {
                    const struct smbios_type0* t = (const struct smbios_type0*)hdr;
                    const char* v = get_smbios_string(hdr, t->vendor_str_id);
                    const char* r = get_smbios_string(hdr, t->version_str_id);
                    if (v) { strncpy(smbios_bios_vendor,  v, 63); smbios_bios_vendor[63]  = '\0'; } else { /* Null */ }
                    if (r) { strncpy(smbios_bios_version, r, 63); smbios_bios_version[63] = '\0'; } else { /* Null */ }
                    break;
                }
                case 1: {
                    const struct smbios_type1* t = (const struct smbios_type1*)hdr;
                    const char* m = get_smbios_string(hdr, t->manufacturer_str_id);
                    const char* p = get_smbios_string(hdr, t->product_name_str_id);
                    if (m) { strncpy(smbios_sys_manufacturer, m, 63); smbios_sys_manufacturer[63] = '\0'; } else { /* Null */ }
                    if (p) { strncpy(smbios_sys_product,      p, 63); smbios_sys_product[63]      = '\0'; } else { /* Null */ }
                    break;
                }
                case 2: {
                    const struct smbios_type2* t = (const struct smbios_type2*)hdr;
                    const char* p = get_smbios_string(hdr, t->product_name_str_id);
                    if (p) { strncpy(smbios_board_product, p, 63); smbios_board_product[63] = '\0'; } else { /* Null */ }
                    break;
                }
                case 4: {
                    const struct smbios_type4* t = (const struct smbios_type4*)hdr;
                    const char* s = get_smbios_string(hdr, t->socket_designation_str_id);
                    if (s && smbios_cpu_socket[0] == '\0') {
                        strncpy(smbios_cpu_socket, s, 63);
                        smbios_cpu_socket[63] = '\0';
                    } else { /* Null or already assigned */ }
                    break;
                }
                default:
                    break;
            }

            uint8_t* next = smbios_advance_to_next(tp);
            if (!next || next <= tp) {
                break;
            } else {
                tp = next;
                count++;
            }
        }
    }
}

void init_smbios(void* multiboot_addr) {
    void* eps_ptr = NULL;

    if (multiboot_addr) {
        struct multiboot_tag* tag;
        uint8_t* ptr  = (uint8_t*)multiboot_addr + 8;
        for (tag = (struct multiboot_tag*)ptr;
             tag->type != MULTIBOOT_TAG_TYPE_END;
             tag = (struct multiboot_tag*)((uint8_t*)tag + ((tag->size + 7) & ~7)))
        {
            if (tag->size < sizeof(struct multiboot_tag)) {
                break;
            } else {
                if (tag->type == MULTIBOOT_TAG_TYPE_SMBIOS) {
                    struct multiboot_tag_smbios* st = (struct multiboot_tag_smbios*)tag;
                    eps_ptr = (void*)st->tables;
                    break;
                } else { /* Keep looking */ }
            }
        }
    } else {
        // MultiBoot not available
    }

    // BUG-001 FIX: UEFI Configuration Table Fallback!
    if (!eps_ptr && uefi_available()) {
        efi_guid_t smbios3_guid = { 0xf2fd1544, 0x9794, 0x4a2c, {0x99, 0x2e, 0xe5, 0xbb, 0xcf, 0x20, 0xe3, 0x94} };
        efi_guid_t smbios2_guid = { 0xeb9d2d31, 0x2d88, 0x11d3, {0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d} };
        
        eps_ptr = uefi_get_configuration_table(&smbios3_guid);
        if (!eps_ptr) {
            eps_ptr = uefi_get_configuration_table(&smbios2_guid);
        } else {
            // SMBIOS 3.0 found
        }
        
        if (eps_ptr) {
            serial_write("[SMBIOS] Reacquired Entry Point via UEFI Configuration Table Fallback.\n");
        } else {
            // Still null
        }
    } else {
        // Either found via MB or UEFI unavailable
    }

    if (!eps_ptr) {
        serial_write("[SMBIOS] Entry point not found via Multiboot or UEFI. Aborting.\n");
        return;
    } else {
        if (memcmp(eps_ptr, "_SM3_", 5) == 0) {
            const struct smbios3_entry_point* ep3 = (const struct smbios3_entry_point*)eps_ptr;
            if (!verify_smbios_checksum((const uint8_t*)ep3, ep3->length)) {
                serial_write("[SMBIOS] WARN: SMBIOS 3.0 checksum mismatch.\n");
            } else { /* Checksum Pass */ }
            smbios_table_addr    = ep3->table_address;
            smbios_table_len     = ep3->table_max_size;
            smbios_version_major = ep3->major_ver;
            smbios_version_minor = ep3->minor_ver;
            smbios_num_structs   = -1;
        } else if (memcmp(eps_ptr, "_SM_", 4) == 0) {
            const struct smbios_entry_point* ep2 = (const struct smbios_entry_point*)eps_ptr;
            if (!verify_smbios_checksum((const uint8_t*)ep2, 16) || !verify_smbios_checksum((const uint8_t*)ep2 + 10, 15)) {
                serial_write("[SMBIOS] WARN: SMBIOS 2.x checksum mismatch.\n");
            } else { /* Checksum Pass */ }
            smbios_table_addr    = (uint64_t)ep2->table_address;
            smbios_table_len     = ep2->table_length;
            smbios_version_major = ep2->major_ver;
            smbios_version_minor = ep2->minor_ver;
            smbios_num_structs   = ep2->number_of_structures;
        } else {
            serial_write("[SMBIOS] Unknown entry point signature. Aborting.\n");
            return;
        }

        if (smbios_version_major == 0 || smbios_version_major > 10) {
            smbios_version_major = 2;
            smbios_version_minor = 0;
        } else {
            // Valid version format
        }

        if (smbios_table_addr == 0 || smbios_table_len == 0) {
            serial_write("[SMBIOS] Table address or length is zero. Aborting.\n");
            return;
        } else {
            if (!smbios_map_table()) {
                smbios_table_virt = (void*)smbios_table_addr; // Physical Identity Map Bypass
            } else {
                // Safely mapped
            }

            char log[128];
            snprintf(log, sizeof(log), "[SMBIOS] v%d.%d ready. phys=0x%lx len=%u\n",
                     smbios_version_major, smbios_version_minor, smbios_table_addr, smbios_table_len);
            serial_write(log);

            smbios_preparse_identity();
        }
    }
}

void smbios_get_ram_info(ram_hw_info_t* info) {
    if (!info) {
        return;
    } else {
        strncpy(info->type_str, "Unknown", sizeof(info->type_str) - 1);
        info->type_str[sizeof(info->type_str) - 1] = '\0';
        info->speed_mhz    = 0;
        info->total_width  = 0;
        info->data_width   = 0;
        info->slot_count   = 0;
        info->active_slots = 0;

        if (!smbios_table_virt || smbios_table_len == 0) {
            return;
        } else {
            const uint8_t* tp    = (const uint8_t*)smbios_table_virt;
            const uint8_t* t_end = smbios_table_end();
            int count = 0;
            int limit = (smbios_num_structs > 0) ? smbios_num_structs : 512;

            while (tp && tp < t_end && count < limit) {
                const struct smbios_header* hdr = (const struct smbios_header*)tp;
                if (hdr->length < 4 || hdr->type == 127) {
                    break;
                } else if ((tp + hdr->length) > t_end) {
                    break;
                } else {
                    if (hdr->type == 17 && hdr->length >= 0x1C) {
                        const struct smbios_type17* mem = (const struct smbios_type17*)hdr;
                        info->slot_count++;

                        if (mem->size > 0 && mem->size != 0xFFFF) {
                            if (mem->size == 0x7FFF) { info->speed_mhz = mem->extended_size; } else { /* Retained */ }
                            info->active_slots++;
                            if (info->speed_mhz == 0) { info->speed_mhz = mem->configured_clock_speed; } else { /* Verified */ }
                            if (info->total_width == 0) {
                                info->total_width = mem->total_width;
                                info->data_width  = mem->data_width;
                            } else { /* Verified */ }

                            if (strcmp(info->type_str, "Unknown") == 0) {
                                switch (mem->memory_type) {
                                    case SMBIOS_MEMTYPE_DDR2:   { strncpy(info->type_str, "DDR2",   15); break; }
                                    case SMBIOS_MEMTYPE_DDR3:   { strncpy(info->type_str, "DDR3",   15); break; }
                                    case SMBIOS_MEMTYPE_DDR4:   { strncpy(info->type_str, "DDR4",   15); break; }
                                    case SMBIOS_MEMTYPE_LPDDR:  { strncpy(info->type_str, "LPDDR",  15); break; }
                                    case SMBIOS_MEMTYPE_LPDDR2: { strncpy(info->type_str, "LPDDR2", 15); break; }
                                    case SMBIOS_MEMTYPE_LPDDR3: { strncpy(info->type_str, "LPDDR3", 15); break; }
                                    case SMBIOS_MEMTYPE_LPDDR4: { strncpy(info->type_str, "LPDDR4", 15); break; }
                                    case SMBIOS_MEMTYPE_DDR5:   { strncpy(info->type_str, "DDR5",   15); break; }
                                    case SMBIOS_MEMTYPE_LPDDR5: { strncpy(info->type_str, "LPDDR5", 15); break; }
                                    default:                    { strncpy(info->type_str, "Unknown",15); break; }
                                }
                                info->type_str[15] = '\0';
                            } else {
                                // Already set
                            }
                        } else {
                            // Unpopulated slot
                        }
                    } else {
                        // Different struct
                    }

                    uint8_t* next = smbios_advance_to_next(tp);
                    if (!next || next <= tp) {
                        break;
                    } else {
                        tp = next;
                        count++;
                    }
                }
            }
        }
    }
}

void smbios_print_full_info(void) {
    if (!smbios_table_virt) {
        printf("[SMBIOS] No SMBIOS data available.\n");
        return;
    } else {
        printf("\n========== SMBIOS HARDWARE IDENTITY ==========\n");
        printf("SMBIOS Version  : %d.%d\n", smbios_version_major, smbios_version_minor);
        printf("BIOS Vendor     : %s\n", safe_str(smbios_bios_vendor));
        printf("BIOS Version    : %s\n", safe_str(smbios_bios_version));
        printf("System Mfr      : %s\n", safe_str(smbios_sys_manufacturer));
        printf("System Product  : %s\n", safe_str(smbios_sys_product));
        printf("Board Product   : %s\n", safe_str(smbios_board_product));
        printf("CPU Socket      : %s\n", safe_str(smbios_cpu_socket));

        ram_hw_info_t ram = { .type_str = {0}, .speed_mhz = 0, .total_width = 0, .data_width = 0, .slot_count = 0, .active_slots = 0 };
        smbios_get_ram_info(&ram);
        printf("RAM Type        : %s @ %u MHz\n", safe_str(ram.type_str), ram.speed_mhz);
        printf("RAM Slots       : %d total, %d active\n", ram.slot_count, ram.active_slots);
        printf("RAM Bus Width   : %u-bit data / %u-bit total\n", ram.data_width, ram.total_width);
        printf("===============================================\n");
    }
}

bool smbios_get_identity(char* buf, size_t buf_len) {
    if (!buf || buf_len == 0) {
        return false;
    } else {
        if (!smbios_table_virt) {
            strncpy(buf, "Unknown/Unknown", buf_len - 1);
            buf[buf_len - 1] = '\0';
            return false;
        } else {
            snprintf(buf, buf_len, "%s / %s",
                     smbios_sys_manufacturer[0] ? smbios_sys_manufacturer : "Unknown",
                     smbios_sys_product[0]      ? smbios_sys_product      : "Unknown");
            return true;
        }
    }
}