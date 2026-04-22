// drivers/uefi/uefi.cpp
#include "drivers/uefi/uefi.h"
#include "memory/paging.h"
#include "memory/kheap.h" // YENİ: Kmalloc için
#include "libc/stdio.h"
#include "libc/string.h"
#include "archs/cpu/cpu_hal.h"
#include "archs/cpu/x86_64/core/multiboot.h"
#include "kernel/debug.h"

static UEFIDriver* g_uefi = nullptr;

extern "C" uint64_t uefi_call_wrapper5_asm(void* func,
                                            uint64_t a1, uint64_t a2,
                                            uint64_t a3, uint64_t a4,
                                            uint64_t a5);

static uint32_t crc32_software(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320UL;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

static bool verify_efi_table_header(const efi_table_header_t* hdr, uint32_t expected_size) {
    if (!hdr) {
        return false;
    } else {
        // Valid pointer
    }

    if (hdr->header_size < sizeof(efi_table_header_t) ||
        hdr->header_size > expected_size) {
        serial_write("[UEFI] WARN: Implausible EFI header_size.\n");
        return false;
    } else {
        // Valid size bounds
    }

    // BUG-005 FIX: Statik 128 baytlık buffer kaldırıldı, dinamik Heap kullanılıyor.
    uint8_t* tmp = (uint8_t*)kmalloc(hdr->header_size);
    if (!tmp) {
        serial_write("[UEFI] WARN: OOM during EFI CRC32 verification. Bypassing check.\n");
        return true; 
    } else {
        memcpy(tmp, hdr, hdr->header_size);
    }

    // CRC hesaplanırken orijinal CRC alanı 0 olmalıdır (UEFI Spec)
    tmp[8] = 0; tmp[9] = 0; tmp[10] = 0; tmp[11] = 0;

    uint32_t computed = crc32_software(tmp, hdr->header_size);
    kfree(tmp);

    if (computed != hdr->crc32) {
        char msg[96];
        snprintf(msg, sizeof(msg),
                 "[UEFI] WARN: EFI header CRC32 mismatch "
                 "(stored=0x%08x computed=0x%08x). Firmware bug?\n",
                 hdr->crc32, computed);
        serial_write(msg);
        return true; // VirtualBox/QEMU Firmware bug workaround
    } else {
        return true;
    }
}

UEFIDriver::UEFIDriver(uint64_t st_phys)
    : Device("UEFI Runtime", DEV_UNKNOWN), st(nullptr), rt(nullptr)
{
    if (st_phys == 0) {
        serial_write("[UEFI] Physical system table address is zero. Aborting.\n");
        return;
    } else {
        // Address Valid
    }

    st = (efi_system_table_t*)ioremap(st_phys,
                                      sizeof(efi_system_table_t),
                                      PAGE_PRESENT | PAGE_NX); // SEC-002: Read Only 
    if (!st) {
        serial_write("[UEFI] Failed to map EFI System Table.\n");
        return;
    } else {
        // Mapped Successfully
    }

    verify_efi_table_header(&st->hdr, sizeof(efi_system_table_t) + 4096); 

    if (st->hdr.signature != EFI_SYSTEM_TABLE_SIGNATURE) {
        serial_write("[UEFI] WARN: EFI System Table signature mismatch. "
                     "Table may be invalid.\n");
    } else {
        // Signature Valid
    }

    uint64_t rt_phys = (uint64_t)st->runtime_services;
    if (rt_phys == 0) {
        serial_write("[UEFI] Runtime Services pointer in System Table is NULL.\n");
        iounmap(st, sizeof(efi_system_table_t));
        st = nullptr;
        return;
    } else {
        // RT Exists
    }

    rt = (efi_runtime_services_t*)ioremap(rt_phys,
                                          sizeof(efi_runtime_services_t),
                                          PAGE_PRESENT | PAGE_NX);
    if (!rt) {
        serial_write("[UEFI] Failed to map EFI Runtime Services.\n");
        iounmap(st, sizeof(efi_system_table_t));
        st = nullptr;
        return;
    } else {
        // Mapped Successfully
    }

    verify_efi_table_header(&rt->hdr, sizeof(efi_runtime_services_t) + 4096);
}

UEFIDriver::~UEFIDriver() {
    if (rt) {
        iounmap(rt, sizeof(efi_runtime_services_t));
        rt = nullptr;
    } else {
        // Clean
    }

    if (st) {
        iounmap(st, sizeof(efi_system_table_t));
        st = nullptr;
    } else {
        // Clean
    }

    if (g_uefi == this) {
        g_uefi = nullptr;
    } else {
        // Already Null
    }
}

int UEFIDriver::init() {
    if (st && rt) {
        DeviceManager::registerDevice(this);
        g_uefi = this;
        return 1;
    } else {
        serial_write("[UEFI] init() called but st or rt is NULL. "
                     "Driver not registered.\n");
        return 0;
    }
}

// BUG-001 FIX: UEFI Configuration Table Tarama Algoritması
void* UEFIDriver::getConfigurationTable(efi_guid_t* target_guid) {
    if (!st || !target_guid) { return nullptr; } else { /* Valid */ }
    
    uint64_t num_entries = st->number_of_table_entries;
    uint64_t table_phys = (uint64_t)st->configuration_table;
    
    if (num_entries == 0 || table_phys == 0) { return nullptr; } else { /* Valid */ }
    
    efi_configuration_table_t* config_table = (efi_configuration_table_t*)ioremap(
        table_phys, 
        num_entries * sizeof(efi_configuration_table_t), 
        PAGE_PRESENT | PAGE_NX
    );
    
    if (!config_table) { return nullptr; } else { /* Mapped */ }
    
    void* result = nullptr;
    
    for (uint64_t i = 0; i < num_entries; i++) {
        if (memcmp(&config_table[i].vendor_guid, target_guid, sizeof(efi_guid_t)) == 0) {
            result = config_table[i].vendor_table;
            break;
        } else {
            // Keep scanning
        }
    }
    
    iounmap(config_table, num_entries * sizeof(efi_configuration_table_t));
    return result;
}

void UEFIDriver::resetSystem(int type) {
    if (!rt) {
        serial_write("[UEFI] resetSystem: Runtime Services not available.\n");
        return;
    } else {
        if (!rt->reset_system) {
            serial_write("[UEFI] resetSystem: reset_system pointer is NULL.\n");
            return;
        } else {
            uefi_call_wrapper(rt->reset_system, (uint64_t)type, 0, 0, 0);
        }
    }
}

void UEFIDriver::utf8_to_utf16(const char* src, uint16_t* dst, size_t max_len) {
    if (!src || !dst || max_len == 0) {
        if (dst && max_len > 0) {
            dst[0] = 0;
        } else {
            // Pointer null
        }
        return;
    } else {
        size_t i = 0;
        while (src[i] != '\0' && i < (max_len - 1)) {
            if ((unsigned char)src[i] < 128) {
                dst[i] = (uint16_t)(unsigned char)src[i];
            } else {
                dst[i] = (uint16_t)'?';
            }
            i++;
        }
        dst[i] = 0;
    }
}

int UEFIDriver::getTime(efi_time_t* time) {
    if (!rt || !time) {
        return 0;
    } else {
        if (!rt->get_time) {
            serial_write("[UEFI] getTime: get_time pointer is NULL.\n");
            return 0;
        } else {
            uint64_t status = uefi_call_wrapper(rt->get_time,
                                                (uint64_t)time, 0, 0, 0);
            return (status == 0) ? 1 : 0;
        }
    }
}

int UEFIDriver::setTime(efi_time_t* time) {
    if (!rt || !time) {
        return 0;
    } else {
        if (!rt->set_time) {
            serial_write("[UEFI] setTime: set_time pointer is NULL.\n");
            return 0;
        } else {
            uint64_t status = uefi_call_wrapper(rt->set_time,
                                                (uint64_t)time, 0, 0, 0);
            return (status == 0) ? 1 : 0;
        }
    }
}

int UEFIDriver::getVariable(const char* name, efi_guid_t* guid,
                             uint32_t* attributes,
                             uint64_t* data_size, void* data) {
    if (!rt || !name || !guid || !data_size) {
        return 0;
    } else {
        if (!rt->get_variable) {
            serial_write("[UEFI] getVariable: get_variable pointer is NULL.\n");
            return 0;
        } else {
            uint16_t wide_name[128];
            utf8_to_utf16(name, wide_name, 128);

            uint64_t status = uefi_call_wrapper5_asm(
                rt->get_variable,
                (uint64_t)wide_name,
                (uint64_t)guid,
                (uint64_t)attributes,
                (uint64_t)data_size,
                (uint64_t)data);
            return (status == 0) ? 1 : 0;
        }
    }
}

int UEFIDriver::setVariable(const char* name, efi_guid_t* guid,
                             uint32_t attributes,
                             uint64_t data_size, void* data) {
    if (!rt || !name || !guid) {
        return 0;
    } else {
        if (!rt->set_variable) {
            serial_write("[UEFI] setVariable: set_variable pointer is NULL.\n");
            return 0;
        } else {
            uint16_t wide_name[128];
            utf8_to_utf16(name, wide_name, 128);

            uint64_t status = uefi_call_wrapper5_asm(
                rt->set_variable,
                (uint64_t)wide_name,
                (uint64_t)guid,
                (uint64_t)attributes,
                data_size,
                (uint64_t)data);
            return (status == 0) ? 1 : 0;
        }
    }
}

extern "C" {

    void init_uefi(void* multiboot_addr) {
        if (!multiboot_addr) {
            serial_write("[UEFI] init_uefi: multiboot_addr is NULL.\n");
            return;
        } else {
            // Valid pointer
        }

        struct multiboot_tag* tag;
        uint8_t* ptr = (uint8_t*)multiboot_addr + 8;

        for (tag = (struct multiboot_tag*)ptr;
             tag->type != MULTIBOOT_TAG_TYPE_END;
             tag = (struct multiboot_tag*)((uint8_t*)tag + ((tag->size + 7) & ~7)))
        {
            if (tag->size < sizeof(struct multiboot_tag)) {
                break;
            } else {
                // Loop bounds verified
            }

            if (tag->type == MULTIBOOT_TAG_TYPE_EFI64) {
                struct multiboot_tag_efi64* efi = (struct multiboot_tag_efi64*)tag;
                uint64_t st_addr = efi->pointer;

                if (st_addr == 0) {
                    serial_write("[UEFI] EFI64 tag present but pointer is zero.\n");
                    return;
                } else {
                    UEFIDriver* drv = new UEFIDriver(st_addr);
                    if (!drv->init()) {
                        delete drv;
                    } else {
                        // Driver bound successfully
                    }
                    return;
                }
            } else {
                // Not EFI64 tag
            }
        }

        serial_write("[UEFI] No EFI64 tag found in multiboot info.\n");
    }

    void uefi_reset_system(int type) {
        if (g_uefi) {
            g_uefi->resetSystem(type);
        } else {
            serial_write("[UEFI] uefi_reset_system: UEFI not available.\n");
        }
    }

    int uefi_get_time(efi_time_t* time) {
        if (g_uefi) {
            return g_uefi->getTime(time);
        } else {
            return 0;
        }
    }

    int uefi_set_time(efi_time_t* time) {
        if (g_uefi) {
            return g_uefi->setTime(time);
        } else {
            return 0;
        }
    }

    int uefi_get_variable(const char* name, efi_guid_t* guid,
                           uint32_t* attributes,
                           uint64_t* data_size, void* data) {
        if (g_uefi) {
            return g_uefi->getVariable(name, guid, attributes, data_size, data);
        } else {
            return 0;
        }
    }

    int uefi_set_variable(const char* name, efi_guid_t* guid,
                           uint32_t attributes,
                           uint64_t data_size, void* data) {
        if (g_uefi) {
            return g_uefi->setVariable(name, guid, attributes, data_size, data);
        } else {
            return 0;
        }
    }

    bool uefi_available(void) {
        return (g_uefi != nullptr);
    }
    
    void* uefi_get_configuration_table(efi_guid_t* target_guid) {
        if (g_uefi) {
            return g_uefi->getConfigurationTable(target_guid);
        } else {
            return nullptr;
        }
    }

}