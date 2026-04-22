// drivers/acpi/acpi.c
#include "archs/cpu/cpu_hal.h"
#include "libc/string.h"
#include "libc/stdio.h"
#include "archs/cpu/x86_64/memory/paging.h"
#include "system/power/power.h"
#include "archs/cpu/x86_64/interrupts/isr.h"
#include "archs/cpu/x86_64/core/multiboot.h"
#include "kernel/debug.h"

extern void rust_pcie_init_ecam(uint64_t mcfg_phys_base, uint8_t start_bus, uint8_t end_bus);
extern void rust_pcie_enumerate(void);
extern uint64_t get_uptime_ns();

// RUST FFI BRIDGE
typedef struct {
    uint16_t slp_typ_a;
    uint16_t slp_typ_b;
    bool found;
} acpi_sleep_data_t;

extern void rust_acpi_parse_dsdt(const uint8_t* dsdt_ptr, uint32_t dsdt_len, acpi_sleep_data_t* s5_out, acpi_sleep_data_t* s3_out);

#define MULTIBOOT_TAG_TYPE_ACPI_OLD 14
#define MULTIBOOT_TAG_TYPE_ACPI_NEW 15

uint32_t ioapic_address = 0;
uint8_t ioapic_id = 0;
uint8_t acpi_cpu_count = 0;
uint8_t acpi_cpu_ids[32];

static uint32_t iso_remap[16]; 
static rsdp_t* rsdp = NULL;
static struct rsdt* rsdt = NULL;
static struct xsdt* xsdt = NULL;
static struct fadt* fadt = NULL;
static struct madt* madt = NULL;

static int acpi_version = 0;
static bool use_xsdt = false;

static acpi_sleep_data_t s5_data = {0, 0, false};
static acpi_sleep_data_t s3_data = {0, 0, false};

uint32_t acpi_get_pm_timer_port() {
    if (fadt) {
        return fadt->pm_tmr_blk;
    } else {
        return 0;
    }
}

bool acpi_check_power_button_status() {
    if (!fadt) { return false; } else { /* Proceed */ }
    uint16_t status = hal_io_inw(fadt->pm1a_evt_blk);
    if (fadt->pm1b_evt_blk) {
        status |= hal_io_inw(fadt->pm1b_evt_blk);
    } else {
        // No PM1B Block
    }
    
    if (status & ACPI_PM1_PWRBTN_STS) {
        hal_io_outw(fadt->pm1a_evt_blk, ACPI_PM1_PWRBTN_STS);
        if (fadt->pm1b_evt_blk) {
            hal_io_outw(fadt->pm1b_evt_blk, ACPI_PM1_PWRBTN_STS);
        } else {
            // Handled
        }
        return true;
    } else {
        return false;
    }
}

static bool check_checksum(uint8_t* ptr, int len) {
    uint8_t sum = 0;
    for (int i = 0; i < len; i++) sum += ptr[i];
    return sum == 0;
}

static void* map_and_check_table(uint64_t phys_addr, const char* signature) {
    if (phys_addr == 0) { return NULL; } else { /* Proceed */ }

    acpi_header_t* header = (acpi_header_t*)ioremap(phys_addr, sizeof(acpi_header_t), PAGE_PRESENT | PAGE_NX);
    if (!header) { return NULL; } else { /* Proceed */ }

    if (header->length < sizeof(acpi_header_t)) {
        iounmap(header, sizeof(acpi_header_t));
        return NULL;
    } else {
        // Valid header length
    }

    bool match = (memcmp(header->signature, signature, 4) == 0);
    uint32_t len = header->length;
    
    iounmap(header, sizeof(acpi_header_t));

    if (!match) { return NULL; } else { /* Proceed */ }
    
    return ioremap(phys_addr, len, PAGE_PRESENT | PAGE_NX);
}

void* acpi_find_table(const char* signature) {
    if (use_xsdt && xsdt) {
        if (xsdt->h.length >= sizeof(xsdt->h)) {
            int entries = (xsdt->h.length - sizeof(xsdt->h)) / 8;
            for (int i = 0; i < entries; i++) {
                uint64_t phys_addr = xsdt->pointer_to_other_sdt[i];
                void* table = map_and_check_table(phys_addr, signature);
                if (table) { return table; } else { /* Not found, continue loop */ }
            }
        } else {
            // Invalid XSDT length
        }
    } else if (rsdt) {
        if (rsdt->h.length >= sizeof(rsdt->h)) {
            int entries = (rsdt->h.length - sizeof(rsdt->h)) / 4;
            for (int i = 0; i < entries; i++) {
                uint64_t phys_addr = (uint64_t)rsdt->pointer_to_other_sdt[i];
                void* table = map_and_check_table(phys_addr, signature);
                if (table) { return table; } else { /* Not found, continue loop */ }
            }
        } else {
            // Invalid RSDT length
        }
    } else {
        klog(LOG_ERROR, "[ACPI] FATAL: Neither XSDT nor RSDT are valid. Cannot parse tables!");
    }
    return NULL;
}

static void acpi_sci_handler(registers_t* regs) {
    (void)regs;
    if (!fadt) { return; } else { /* Proceed */ }
    uint16_t status = hal_io_inw(fadt->pm1a_evt_blk);
    if (fadt->pm1b_evt_blk) {
        status |= hal_io_inw(fadt->pm1b_evt_blk); 
    } else {
        // No PM1B
    }

    if (status & ACPI_PM1_PWRBTN_STS) {
        hal_io_outw(fadt->pm1a_evt_blk, ACPI_PM1_PWRBTN_STS);
        if (fadt->pm1b_evt_blk) {
            hal_io_outw(fadt->pm1b_evt_blk, ACPI_PM1_PWRBTN_STS); 
        } else {
            // No PM1B
        }
        
        if (get_uptime_ns() > 2000000000ULL) {
            apic_send_eoi();
            system_shutdown("ACPI Power Button (Hardware Event)");
        } else {
            serial_write("[ACPI] Ignored phantom Power Button press during early boot.\n");
        }
    } else {
        hal_io_outw(fadt->pm1a_evt_blk, status);
        if (fadt->pm1b_evt_blk) {
            hal_io_outw(fadt->pm1b_evt_blk, status); 
        } else {
            // No PM1B
        }
    }
}

static void acpi_enable_power_button() {
    if (!fadt) { return; } else { /* Proceed */ }
    uint32_t pm1a_enable_reg = fadt->pm1a_evt_blk + (fadt->pm1_evt_len / 2);
    hal_io_outw(pm1a_enable_reg, hal_io_inw(pm1a_enable_reg) | ACPI_PM1_PWRBTN_EN);
    if (fadt->pm1b_evt_blk) {
        uint32_t pm1b_enable_reg = fadt->pm1b_evt_blk + (fadt->pm1_evt_len / 2);
        hal_io_outw(pm1b_enable_reg, hal_io_inw(pm1b_enable_reg) | ACPI_PM1_PWRBTN_EN);
    } else {
        // No PM1B
    }
}

static int acpi_enable() {
    if (!fadt) { return -1; } else { /* Proceed */ }
    if ((hal_io_inb(fadt->pm1a_cnt_blk) & 1) == 0) {
        if (fadt->smi_cmd != 0 && fadt->acpi_enable != 0) {
            hal_io_outb(fadt->smi_cmd, fadt->acpi_enable);
            for(volatile int i = 0; i < 300000; i++);
            if ((hal_io_inb(fadt->pm1a_cnt_blk) & 1) == 0) {
                return -1; 
            } else {
                // ACPI Enabled successfully
            }
            return 0;
        } else {
            // Hardware doesn't support legacy ACPI mode transitions
        }
    } else {
        // Already enabled
    }
    return 0;
}

static int init_dsdt() {
    if (!fadt) { return -1; } else { /* Proceed */ }
    
    uint64_t dsdt_phys = 0;
    if (use_xsdt && fadt->x_dsdt != 0) {
        dsdt_phys = fadt->x_dsdt;
    } else {
        dsdt_phys = fadt->dsdt;
    }
    
    acpi_header_t* header = (acpi_header_t*)ioremap(dsdt_phys, sizeof(acpi_header_t), PAGE_PRESENT | PAGE_NX);
    if (!header) { return -1; } else { /* Proceed */ }
    
    uint32_t len = header->length;
    bool sig_ok = (memcmp(header->signature, "DSDT", 4) == 0);
    iounmap(header, sizeof(acpi_header_t));
    
    if (!sig_ok || len < 4) { return -1; } else { /* Proceed */ }

    uint8_t* dsdt_data = (uint8_t*)ioremap(dsdt_phys, len, PAGE_PRESENT | PAGE_NX);
    if (!dsdt_data) { return -1; } else { /* Proceed */ }

    // DSDT Parsing delegated to Memory-Safe Rust module
    rust_acpi_parse_dsdt(dsdt_data, len, &s5_data, &s3_data);
    
    iounmap(dsdt_data, len);
    return 0;
}

uint32_t acpi_remap_irq(uint8_t irq) {
    if (irq >= 16) { return irq; } else { return iso_remap[irq]; }
}

void init_apic_acpi() {
    madt = (struct madt*)acpi_find_table("APIC");
    if (!madt) {
        serial_write("[ACPI] Critical: MADT (APIC) table not found!\n");
        return;
    } else {
        // Valid
    }

    if (madt->h.length < sizeof(struct madt)) {
        return; 
    } else {
        // Valid
    }

    for (int i = 0; i < 16; i++) { iso_remap[i] = i; }

    uint32_t lapic_addr = madt->local_apic_addr;
    uint8_t* ptr = (uint8_t*)madt + sizeof(struct madt);
    uint8_t* end = (uint8_t*)madt + madt->h.length;

    acpi_cpu_count = 0;

    while (ptr < end) {
        struct madt_entry_header* header = (struct madt_entry_header*)ptr;
        if (header->length == 0) { break; } else { /* Valid */ }

        if (header->type == MADT_TYPE_LAPIC) {
            struct madt_lapic* lapic = (struct madt_lapic*)ptr;
            if (lapic->flags & 1) {
                if (acpi_cpu_count < 32) {
                    acpi_cpu_ids[acpi_cpu_count] = lapic->apic_id;
                    acpi_cpu_count++;
                } else {
                    // Reached max supported CPUs
                }
            } else {
                // Disabled core
            }
        }
        else if (header->type == MADT_TYPE_IOAPIC) {
            struct madt_ioapic* io = (struct madt_ioapic*)ptr;
            ioapic_address = io->ioapic_addr;
            ioapic_id = io->ioapic_id;
        }
        else if (header->type == MADT_TYPE_ISO) {
            struct madt_iso* iso = (struct madt_iso*)ptr;
            if (iso->irq_source < 16) {
                iso_remap[iso->irq_source] = iso->gsi; 
            } else {
                // Out of range ISA interrupt
            }
        }
        else {
            // Other MADT structures ignored
        }
        ptr += header->length;
    }
    
    if (acpi_cpu_count == 0) {
        acpi_cpu_count = 1;
        acpi_cpu_ids[0] = 0;
    } else {
        // CPUs mapped
    }
    
    init_apic(lapic_addr);
}

void init_acpi(void* multiboot_addr) {
    uint64_t rsdp_phys = 0;
    bool found_via_tag = false;

    if (multiboot_addr) {
        struct multiboot_tag* tag;
        uint8_t* ptr = (uint8_t*)multiboot_addr + 8;

        for (tag = (struct multiboot_tag*)ptr; 
             tag->type != MULTIBOOT_TAG_TYPE_END; 
             tag = (struct multiboot_tag*)((uint8_t*)tag + ((tag->size + 7) & ~7))) 
        {
            if (tag->type == MULTIBOOT_TAG_TYPE_ACPI_NEW) { 
                struct multiboot_tag_new_acpi* acpi = (struct multiboot_tag_new_acpi*)tag;
                rsdp_phys = (uint64_t)acpi->rsdp; 
                found_via_tag = true;
                break;
            } else if (tag->type == MULTIBOOT_TAG_TYPE_ACPI_OLD) { 
                struct multiboot_tag_old_acpi* acpi = (struct multiboot_tag_old_acpi*)tag;
                rsdp_phys = (uint64_t)acpi->rsdp; 
                found_via_tag = true;
                break; 
            } else {
                // Keep searching for ACPI tag
            }
        }
    } else {
        // No Multiboot Addr provided
    }

    if (!found_via_tag || rsdp_phys == 0) {
        panic_at("acpi.c", __LINE__, KERR_ACPI_ERROR, "CRITICAL: ACPI RSDP not found in Multiboot2 tags! Legacy BIOS is NOT supported.");
        return;
    } else {
        rsdp = (rsdp_t*)ioremap(rsdp_phys, sizeof(rsdp_t), PAGE_PRESENT | PAGE_NX);
    }

    if (rsdp != NULL) {
        acpi_version = rsdp->revision;
        use_xsdt = (acpi_version >= 2 && rsdp->xsdt_address != 0);
        
        if (use_xsdt) {
            uint64_t xsdt_phys = rsdp->xsdt_address;
            acpi_header_t* hdr = (acpi_header_t*)ioremap(xsdt_phys, sizeof(acpi_header_t), PAGE_PRESENT | PAGE_NX);
            if (hdr) {
                uint32_t len = hdr->length;
                iounmap(hdr, sizeof(acpi_header_t));
                
                if (len >= sizeof(acpi_header_t)) {
                    xsdt = (struct xsdt*)ioremap(xsdt_phys, len, PAGE_PRESENT | PAGE_NX);
                    if (!xsdt || !check_checksum((uint8_t*)xsdt, len)) {
                        if (xsdt) { iounmap(xsdt, len); xsdt = NULL; } else { /* Was null */ }
                        use_xsdt = false;
                    } else {
                        // XSDT parsed safely
                    }
                } else {
                    use_xsdt = false;
                }
            } else {
                use_xsdt = false;
            }
        } else {
            // Using standard RSDT
        }
        
        if (!use_xsdt) {
            uint32_t rsdt_phys = rsdp->rsdt_address;
            acpi_header_t* hdr = (acpi_header_t*)ioremap(rsdt_phys, sizeof(acpi_header_t), PAGE_PRESENT | PAGE_NX);
            if (hdr) {
                uint32_t len = hdr->length;
                iounmap(hdr, sizeof(acpi_header_t));
                
                if (len >= sizeof(acpi_header_t)) {
                    rsdt = (struct rsdt*)ioremap(rsdt_phys, len, PAGE_PRESENT | PAGE_NX);
                    if (!rsdt || !check_checksum((uint8_t*)rsdt, len)) {
                        serial_write("[ACPI] RSDT Invalid!\n"); return;
                    } else {
                        // RSDT parsed safely
                    }
                } else {
                    // Invalid Length
                }
            } else {
                // ioremap failed
            }
        } else {
            // Handled via XSDT
        }

        fadt = (struct fadt*)acpi_find_table("FACP");
        if (!fadt) {
             serial_write("[ACPI] FADT Table not found!\n"); return;
        } else {
            // FADT Valid
        }

        acpi_enable();
        init_dsdt(); 
        init_numa();

        hal_io_outw(fadt->pm1a_evt_blk, 0xFFFF);
        if (fadt->pm1b_evt_blk) {
            hal_io_outw(fadt->pm1b_evt_blk, 0xFFFF); 
        } else {
            // No PM1B
        }
        
        uint8_t sci_vector = 48; 
        register_interrupt_handler(sci_vector, acpi_sci_handler);
        acpi_enable_power_button();
        
        struct mcfg_header* mcfg = (struct mcfg_header*)acpi_find_table("MCFG");
        if (mcfg && mcfg->h.length >= sizeof(struct mcfg_header)) {
            int entries = (mcfg->h.length - sizeof(struct mcfg_header)) / sizeof(struct mcfg_allocation);
            struct mcfg_allocation* allocs = (struct mcfg_allocation*)((uint8_t*)mcfg + sizeof(struct mcfg_header));
            for (int i = 0; i < entries; i++) {
                if (allocs[i].pci_segment_group == 0) {
                    rust_pcie_init_ecam(allocs[i].base_address, allocs[i].start_bus_number, allocs[i].end_bus_number);
                    break;
                } else {
                    // Not root segment
                }
            }
        } else {
            // Legacy MCFG or missing
        }
        
        rust_pcie_enumerate();
    } else {
        // Failed to map RSDP
    }
}

void acpi_power_off() {
    if (!fadt) { return; } else { /* Proceed */ }
    
    uint16_t port = fadt->pm1a_cnt_blk;
    if (!port) { return; } else { /* Proceed */ }
    
    uint16_t val = (s5_data.slp_typ_a | 0x2000);
    if (!s5_data.found) {
        val = 0x3400; // QEMU Q35 Fallback
    } else {
        // Safe standard S5 off
    }
    
    hal_io_outw(port, val);
    
    if (fadt->pm1b_cnt_blk) {
        uint16_t val_b = (s5_data.slp_typ_b | 0x2000);
        if (!s5_data.found) {
            val_b = 0x3400; 
        } else {
            // Safe standard S5 off
        }
        hal_io_outw(fadt->pm1b_cnt_blk, val_b);
    } else {
        // No PM1B
    }
}

void acpi_suspend() {
    if (!s3_data.found || !fadt) { return; } else { /* Proceed */ }
    hal_io_outw(fadt->pm1a_cnt_blk, s3_data.slp_typ_a | 0x2000); 
    if (fadt->pm1b_cnt_blk != 0) {
        hal_io_outw(fadt->pm1b_cnt_blk, s3_data.slp_typ_b | 0x2000); 
    } else {
        // No PM1B
    }
    hal_cpu_halt();
}

int acpi_get_version() { return acpi_version; }