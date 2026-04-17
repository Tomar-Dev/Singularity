// drivers/uefi/uefi.h
#ifndef UEFI_H
#define UEFI_H

#include <stdint.h>
#include <stdbool.h>
#include "system/device/device.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t  data4[8];
} efi_guid_t;

typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  pad1;
    uint32_t nanosecond;
    int16_t  timezone;    
    uint8_t  daylight;
    uint8_t  pad2;
} efi_time_t;

#define EFI_UNSPECIFIED_TIMEZONE 0x07FF

typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
} efi_table_header_t;

// EFI System Table signature: "IBI SYST" (0x5453595320494249)
#define EFI_SYSTEM_TABLE_SIGNATURE  0x5453595320494249ULL
#define EFI_RUNTIME_SERVICES_SIGNATURE 0x56524553544E5552ULL

typedef struct {
    efi_table_header_t hdr;
    void* get_time;
    void* set_time;
    void* get_wakeup_time;
    void* set_wakeup_time;
    void* set_virtual_address_map;
    void* convert_pointer;
    void* get_variable;
    void* get_next_variable_name;
    void* set_variable;
    void* get_next_high_monotonic_count;
    void* reset_system;
    void* update_capsule;
    void* query_capsule_capabilities;
    void* query_variable_info;
} efi_runtime_services_t;

// BUG-001 FIX: EFI Configuration Table Structures Added
typedef struct {
    efi_guid_t vendor_guid;
    void*      vendor_table;
} efi_configuration_table_t;

typedef struct {
    efi_table_header_t hdr;
    void*    firmware_vendor;
    uint32_t firmware_revision;
    void*    console_in_handle;
    void*    con_in;
    void*    console_out_handle;
    void*    con_out;
    void*    standard_error_handle;
    void*    std_err;
    void*    runtime_services;
    void*    boot_services;
    uint64_t number_of_table_entries;
    void*    configuration_table; // Points to efi_configuration_table_t array
} efi_system_table_t;

#define EFI_RESET_COLD     0
#define EFI_RESET_WARM     1
#define EFI_RESET_SHUTDOWN 2

#define EFI_VARIABLE_NON_VOLATILE                          0x00000001
#define EFI_VARIABLE_BOOTSERVICE_ACCESS                    0x00000002
#define EFI_VARIABLE_RUNTIME_ACCESS                        0x00000004
#define EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS            0x00000010
#define EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS 0x00000020

uint64_t uefi_call_wrapper(void* func,
                            uint64_t arg1, uint64_t arg2,
                            uint64_t arg3, uint64_t arg4);

uint64_t uefi_call_wrapper5(void* func,
                             uint64_t arg1, uint64_t arg2,
                             uint64_t arg3, uint64_t arg4, uint64_t arg5);

void init_uefi(void* multiboot_addr);

bool uefi_available(void);

void uefi_reset_system(int type);

int uefi_get_time(efi_time_t* time);

int uefi_set_time(efi_time_t* time);

int uefi_get_variable(const char* name, efi_guid_t* guid,
                       uint32_t* attributes,
                       uint64_t* data_size, void* data);

int uefi_set_variable(const char* name, efi_guid_t* guid,
                       uint32_t attributes,
                       uint64_t data_size, void* data);

void* uefi_get_configuration_table(efi_guid_t* target_guid);

#ifdef __cplusplus
}

class UEFIDriver : public Device {
private:
    efi_system_table_t*    st;
    efi_runtime_services_t* rt;

    void utf8_to_utf16(const char* src, uint16_t* dst, size_t max_len);

public:
    UEFIDriver(uint64_t st_phys);
    ~UEFIDriver() override;

    int init() override;

    void resetSystem(int type);

    int getTime(efi_time_t* time);

    int setTime(efi_time_t* time);

    int getVariable(const char* name, efi_guid_t* guid,
                    uint32_t* attributes,
                    uint64_t* data_size, void* data);

    int setVariable(const char* name, efi_guid_t* guid,
                    uint32_t attributes,
                    uint64_t data_size, void* data);
                    
    void* getConfigurationTable(efi_guid_t* target_guid);
};

#endif

#endif