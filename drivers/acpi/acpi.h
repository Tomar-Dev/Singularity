// drivers/acpi/acpi.h
#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct acpi_sdt_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

typedef struct acpi_sdt_header acpi_header_t;

struct rsdp_descriptor {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

typedef struct rsdp_descriptor rsdp_t;

struct rsdt {
    struct acpi_sdt_header h;
    uint32_t pointer_to_other_sdt[]; 
} __attribute__((packed));

struct xsdt {
    struct acpi_sdt_header h;
    uint64_t pointer_to_other_sdt[]; 
} __attribute__((packed));

struct fadt {
    struct acpi_sdt_header h;
    uint32_t firmware_ctrl;
    uint32_t dsdt; 
    uint8_t  reserved;
    uint8_t  preferred_power_managment_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;  
    uint8_t  acpi_enable;
    uint8_t  acpi_disable;
    uint8_t  s4bios_req;
    uint8_t  pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk; 
    uint32_t pm1b_cnt_blk;
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t  pm1_evt_len;
    uint8_t  pm1_cnt_len;
    uint8_t  pm2_cnt_len;
    uint8_t  pm_tmr_len;
    uint8_t  gpe0_len;
    uint8_t  gpe1_len;
    uint8_t  gpe1_base;
    uint8_t  cstate_control;
    uint16_t worst_c2_latency;
    uint16_t worst_c3_latency;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t  duty_offset;
    uint8_t  duty_width;
    uint8_t  day_alrm;
    uint8_t  mon_alrm;
    uint8_t  century;
    uint16_t iapc_boot_arch;
    uint8_t  reserved2;
    uint32_t flags;
    struct {
        uint8_t address_space_id;
        uint8_t register_bit_width;
        uint8_t register_bit_offset;
        uint8_t access_size;
        uint64_t address;
    } reset_reg;
    uint8_t  reset_value;
    uint8_t  reserved3[3];
    uint64_t x_firmware_control;
    uint64_t x_dsdt;
} __attribute__((packed));

#define ACPI_PM1_SCI_EN     (1 << 0)
#define ACPI_PM1_PWRBTN_STS (1 << 8)
#define ACPI_PM1_PWRBTN_EN  (1 << 8)
#define ACPI_PM1_SLP_TYP    (1 << 10)
#define ACPI_PM1_SLP_EN     (1 << 13)

struct madt {
    struct acpi_sdt_header h;
    uint32_t local_apic_addr; 
    uint32_t flags;
} __attribute__((packed));

#define MADT_TYPE_LAPIC      0
#define MADT_TYPE_IOAPIC     1
#define MADT_TYPE_ISO        2  
#define MADT_TYPE_NMI        4

struct madt_entry_header {
    uint8_t type;
    uint8_t length;
} __attribute__((packed));

struct madt_lapic {
    struct madt_entry_header h;
    uint8_t processor_id;
    uint8_t apic_id;
    uint32_t flags; 
} __attribute__((packed));

struct madt_ioapic {
    struct madt_entry_header h;
    uint8_t ioapic_id;
    uint8_t reserved;
    uint32_t ioapic_addr;
    uint32_t gsi_base; 
} __attribute__((packed));

struct madt_iso {
    struct madt_entry_header h;
    uint8_t bus_source; 
    uint8_t irq_source; 
    uint32_t gsi;       
    uint16_t flags;     
} __attribute__((packed));

struct mcfg_header {
    struct acpi_sdt_header h;
    uint64_t reserved;
} __attribute__((packed));

struct mcfg_allocation {
    uint64_t base_address;
    uint16_t pci_segment_group;
    uint8_t  start_bus_number;
    uint8_t  end_bus_number;
    uint32_t reserved;
} __attribute__((packed));

extern uint32_t ioapic_address; 
extern uint8_t ioapic_id;
extern uint8_t acpi_cpu_count;
extern uint8_t acpi_cpu_ids[32];

void init_acpi(void* multiboot_addr);

void init_apic_acpi();
int acpi_get_version();
void acpi_power_off();
void acpi_suspend();
uint32_t acpi_remap_irq(uint8_t irq);
void* acpi_find_table(const char* signature);

bool acpi_check_power_button_status();

// MİMARİ YAMASI: PM Timer Portunu Dışarıya Açan Arayüz (Fallback için)
uint32_t acpi_get_pm_timer_port();

#ifdef __cplusplus
}
#endif

#endif