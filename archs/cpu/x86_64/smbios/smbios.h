// drivers/smbios/smbios.h
#ifndef SMBIOS_H
#define SMBIOS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct smbios_entry_point {
    char     anchor[4];
    uint8_t  checksum;
    uint8_t  length;
    uint8_t  major_ver;
    uint8_t  minor_ver;
    uint16_t max_struct_size;
    uint8_t  entry_point_rev;
    char     formatted_area[5];
    char     intermediate_anchor[5];
    uint8_t  intermediate_checksum;
    uint16_t table_length;
    uint32_t table_address;
    uint16_t number_of_structures;
    uint8_t  bcd_revision;
} __attribute__((packed));

struct smbios3_entry_point {
    char     anchor[5];
    uint8_t  checksum;
    uint8_t  length;
    uint8_t  major_ver;
    uint8_t  minor_ver;
    uint8_t  doc_rev;
    uint8_t  entry_point_rev;
    uint8_t  reserved;
    uint32_t table_max_size;
    uint64_t table_address;
} __attribute__((packed));

struct smbios_header {
    uint8_t  type;
    uint8_t  length;
    uint16_t handle;
} __attribute__((packed));

struct smbios_type0 {
    struct smbios_header h;
    uint8_t  vendor_str_id;
    uint8_t  version_str_id;
    uint16_t start_segment;
    uint8_t  release_date_str_id;
    uint8_t  rom_size;
    uint64_t characteristics;
    uint8_t  ext[2];
} __attribute__((packed));

struct smbios_type1 {
    struct smbios_header h;
    uint8_t  manufacturer_str_id;
    uint8_t  product_name_str_id;
    uint8_t  version_str_id;
    uint8_t  serial_number_str_id;
    uint8_t  uuid[16];
    uint8_t  wakeup_type;
} __attribute__((packed));

struct smbios_type2 {
    struct smbios_header h;
    uint8_t  manufacturer_str_id;
    uint8_t  product_name_str_id;
    uint8_t  version_str_id;
    uint8_t  serial_number_str_id;
    uint8_t  asset_tag_str_id;
    uint8_t  feature_flags;
    uint8_t  location_in_chassis_str_id;
    uint16_t chassis_handle;
    uint8_t  board_type;
} __attribute__((packed));

struct smbios_type3 {
    struct smbios_header h;
    uint8_t  manufacturer_str_id;
    uint8_t  type;
    uint8_t  version_str_id;
    uint8_t  serial_number_str_id;
    uint8_t  asset_tag_str_id;
    uint8_t  bootup_state;
    uint8_t  power_supply_state;
    uint8_t  thermal_state;
    uint8_t  security_status;
} __attribute__((packed));

struct smbios_type4 {
    struct smbios_header h;
    uint8_t  socket_designation_str_id;
    uint8_t  processor_type;
    uint8_t  processor_family;
    uint8_t  manufacturer_str_id;
    uint64_t processor_id;
    uint8_t  version_str_id;
    uint8_t  voltage;
    uint16_t external_clock;
    uint16_t max_speed;
    uint16_t current_speed;
} __attribute__((packed));

struct smbios_type17 {
    struct smbios_header h;
    uint16_t physical_memory_array_handle;
    uint16_t memory_error_information_handle;
    uint16_t total_width;
    uint16_t data_width;
    uint16_t size;                    // Size in MB; 0x7FFF = use extended_size
    uint8_t  form_factor;
    uint8_t  device_set;
    uint8_t  device_locator_str_id;
    uint8_t  bank_locator_str_id;
    uint8_t  memory_type;
    uint16_t type_detail;
    uint16_t speed;
    uint8_t  manufacturer_str_id;
    uint8_t  serial_number_str_id;
    uint8_t  asset_tag_str_id;
    uint8_t  part_number_str_id;
    uint8_t  attributes;
    uint32_t extended_size;           // In MB; valid when size == 0x7FFF
    uint16_t configured_clock_speed;
} __attribute__((packed));

#define SMBIOS_MEMTYPE_DDR2   0x12
#define SMBIOS_MEMTYPE_DDR3   0x18
#define SMBIOS_MEMTYPE_DDR4   0x1A
#define SMBIOS_MEMTYPE_LPDDR  0x1B
#define SMBIOS_MEMTYPE_LPDDR2 0x1C
#define SMBIOS_MEMTYPE_LPDDR3 0x1D
#define SMBIOS_MEMTYPE_LPDDR4 0x1E
#define SMBIOS_MEMTYPE_DDR5   0x22
#define SMBIOS_MEMTYPE_LPDDR5 0x23

typedef struct {
    char     type_str[16];
    uint32_t speed_mhz;
    uint32_t total_width;
    uint32_t data_width;
    int      slot_count;
    int      active_slots;
} ram_hw_info_t;

void init_smbios(void* multiboot_addr);

void smbios_print_full_info(void);

void smbios_get_ram_info(ram_hw_info_t* info);

bool smbios_get_identity(char* buf, size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif
