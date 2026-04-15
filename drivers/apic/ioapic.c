// drivers/apic/ioapic.c
#include "drivers/apic/ioapic.h"
#include "drivers/acpi/acpi.h"
#include "memory/paging.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "drivers/serial/serial.h"
#include "archs/cpu/x86_64/sync/spinlock.h"
extern void print_status(const char* prefix, const char* msg, const char* status);

static volatile uint32_t* ioapic_regsel = NULL;
static volatile uint32_t* ioapic_win    = NULL;
static uint8_t  ioapic_max_redir        = 0;
static spinlock_t ioapic_lock           = {0, 0, {0}};

static uint32_t ioapic_read(uint8_t reg) {
    if (!ioapic_regsel || !ioapic_win) {
        return 0xFFFFFFFF;
    } else {
        *ioapic_regsel = (uint32_t)reg;
        __asm__ volatile("" ::: "memory");
        return *ioapic_win;
    }
}

static void ioapic_write(uint8_t reg, uint32_t value) {
    if (!ioapic_regsel || !ioapic_win) {
        serial_write("[IOAPIC] ERROR: ioapic_write called before init.\n");
        return;
    } else {
        *ioapic_regsel = (uint32_t)reg;
        __asm__ volatile("" ::: "memory");
        *ioapic_win = value;
        __asm__ volatile("" ::: "memory");
    }
}

static uint64_t ioapic_read_redir(uint8_t irq) {
    uint32_t lo = ioapic_read((uint8_t)(IOREDTBL + 2 * irq));
    uint32_t hi = ioapic_read((uint8_t)(IOREDTBL + 2 * irq + 1));
    return ((uint64_t)hi << 32) | lo;
}

static void ioapic_write_redir(uint8_t irq, uint64_t value) {
    uint32_t hi = (uint32_t)(value >> 32);
    uint32_t lo = (uint32_t)(value & 0xFFFFFFFF);
    ioapic_write((uint8_t)(IOREDTBL + 2 * irq), lo | (1 << 16));
    __asm__ volatile("" ::: "memory");
    ioapic_write((uint8_t)(IOREDTBL + 2 * irq + 1), hi);
    __asm__ volatile("" ::: "memory");
    ioapic_write((uint8_t)(IOREDTBL + 2 * irq), lo);
}

void init_ioapic(void) {
    spinlock_init(&ioapic_lock);

    if (ioapic_address == 0) {
        serial_write("[IOAPIC] ERROR: No IO-APIC physical address from ACPI.\n");
        return;
    } else {
    }

    void* mapped = ioremap(ioapic_address, 4096,
                            PAGE_PRESENT | PAGE_WRITE | PAGE_PCD);
    if (!mapped) {
        serial_write("[IOAPIC] CRITICAL: ioremap failed for IO-APIC!\n");
        return;
    } else {
    }

    ioapic_regsel = (volatile uint32_t*)((uint8_t*)mapped + IOREGSEL);
    ioapic_win    = (volatile uint32_t*)((uint8_t*)mapped + IOWIN);

    uint32_t ver = ioapic_read(IOAPICVER);
    if (ver == 0xFFFFFFFF) {
        serial_write("[IOAPIC] ERROR: IO-APIC version register read failed.\n");
        iounmap(mapped, 4096);
        ioapic_regsel = NULL;
        ioapic_win    = NULL;
        return;
    } else {
        ioapic_max_redir = (uint8_t)((ver >> 16) & 0xFF);
    }

    char buf[128];
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    snprintf(buf, sizeof(buf),
             "[IOAPIC] Initialized at Virt 0x%lx (Max Redir: %d)\n",
             (uint64_t)mapped, ioapic_max_redir);
    serial_write(buf);

    char msg[64];
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    snprintf(msg, sizeof(msg), "Mapped & Active. Max IRQs: %d",
             ioapic_max_redir + 1);
    print_status("[ IOAP ]", msg, "INFO");

    uint64_t flags = spinlock_acquire(&ioapic_lock);
    for (int i = 0; i <= ioapic_max_redir; i++) {
        ioapic_write_redir((uint8_t)i, (uint64_t)0x00010000);
    }
    spinlock_release(&ioapic_lock, flags);
}

void ioapic_set_entry(uint8_t irq, uint8_t vector) {
    ioapic_set_entry_full(irq, vector, 0, 0);
}

void ioapic_set_entry_full(uint8_t irq, uint8_t vector,
                            uint8_t dest, uint32_t delivery_flags) {
    if (!ioapic_regsel || !ioapic_win) {
        serial_write("[IOAPIC] ERROR: ioapic_set_entry called before init.\n");
        return;
    } else {
    }

    if (irq > ioapic_max_redir) {
        char warn[96];
        // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
        snprintf(warn, sizeof(warn),
                 "[IOAPIC] WARN: IRQ %d exceeds max redirection entry %d. "
                 "Request ignored.\n",
                 irq, ioapic_max_redir);
        serial_write(warn);
        return;
    } else {
    }

    if (vector < 0x20 || vector == 0xFF) {
        char warn[96];
        // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
        snprintf(warn, sizeof(warn),
                 "[IOAPIC] WARN: Vector 0x%02X is reserved or invalid. "
                 "Request ignored.\n", vector);
        serial_write(warn);
        return;
    } else {
    }

    uint64_t entry = (uint64_t)vector
                   | ((uint64_t)(delivery_flags & 0x1F700) )
                   | ((uint64_t)dest << 56);

    uint64_t lock_flags = spinlock_acquire(&ioapic_lock);
    ioapic_write_redir(irq, entry | (1ULL << 16));
    ioapic_write_redir(irq, entry); 
    spinlock_release(&ioapic_lock, lock_flags);

    char buf[64];
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    snprintf(buf, sizeof(buf),
             "[IOAPIC] Route IRQ %d -> Vector %d (dest CPU %d)\n",
             irq, vector, dest);
    serial_write(buf);
}

void ioapic_mask_irq(uint8_t irq) {
    if (!ioapic_regsel || !ioapic_win) {
        return;
    } else if (irq > ioapic_max_redir) {
        return;
    } else {
        uint64_t lock_flags = spinlock_acquire(&ioapic_lock);
        uint64_t entry = ioapic_read_redir(irq);
        entry |= (1ULL << 16);
        ioapic_write_redir(irq, entry | (1ULL << 16));
        ioapic_write_redir(irq, entry);
        spinlock_release(&ioapic_lock, lock_flags);
    }
}

void ioapic_unmask_irq(uint8_t irq) {
    if (!ioapic_regsel || !ioapic_win) {
        return;
    } else if (irq > ioapic_max_redir) {
        return;
    } else {
        uint64_t lock_flags = spinlock_acquire(&ioapic_lock);
        uint64_t entry = ioapic_read_redir(irq);
        entry &= ~(1ULL << 16);
        ioapic_write_redir(irq, entry | (1ULL << 16));
        ioapic_write_redir(irq, entry);
        spinlock_release(&ioapic_lock, lock_flags);
    }
}

uint8_t ioapic_get_max_redir(void) {
    return ioapic_max_redir;
}
