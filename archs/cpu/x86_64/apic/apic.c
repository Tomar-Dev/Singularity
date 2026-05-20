// archs/cpu/x86_64/apic/apic.c
#include "archs/cpu/x86_64/apic/apic.h"
#include "archs/cpu/x86_64/memory/paging.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "archs/cpu/cpu_hal.h"
#include "archs/cpu/x86_64/sync/spinlock.h"
#include "archs/cpu/x86_64/core/msr.h"
#include "archs/cpu/x86_64/core/cpuid.h"
#include "kernel/debug.h"

static uint64_t lapic_base_virt = 0; 
static bool x2apic_mode = false; 

static volatile uint32_t ticks_per_ms = 0;
static spinlock_t timer_lock = {0, 0, {0}};
static volatile bool timer_calibrated = false;
static bool use_tsc_deadline = false;

static uint32_t apic_read(uint32_t reg) {
    if (x2apic_mode) {
        return (uint32_t)rdmsr(MSR_X2APIC_BASE + (reg >> 4));
    } else {
        return *((volatile uint32_t*)(lapic_base_virt + reg));
    }
}

static void apic_write(uint32_t reg, uint32_t value) {
    if (x2apic_mode) {
        wrmsr(MSR_X2APIC_BASE + (reg >> 4), value);
    } else {
        *((volatile uint32_t*)(lapic_base_virt + reg)) = value;
    }
}

void apic_send_ipi(uint8_t apic_id, uint32_t vector) {
    if (apic_id == 0xFF) {
        return;
    } else {
        // Valid APIC ID
    }

    if (x2apic_mode) {
        uint64_t icr_val = ((uint64_t)apic_id << 32) | vector | 0x4000;
        wrmsr(MSR_X2APIC_ICR, icr_val);
    } else {
        int timeout = 100000;
        while (apic_read(LAPIC_ICR_LOW) & (1 << 12)) {
            if (--timeout == 0) {
                serial_printf("[APIC-FAIL] Send IPI Stuck! Dest: %d\n", apic_id);
                return;
            } else {
                // Keep waiting
            }
            hal_cpu_relax();
        }
        apic_write(LAPIC_ICR_HIGH, (uint32_t)apic_id << 24);
        apic_write(LAPIC_ICR_LOW, vector | 0x4000);
    }
}

void apic_broadcast_ipi(uint32_t vector) {
    if (x2apic_mode) {
        uint64_t icr_val = 0x000C4000 | vector;
        wrmsr(MSR_X2APIC_ICR, icr_val);
    } else {
        int timeout = 100000;
        while (apic_read(LAPIC_ICR_LOW) & (1 << 12)) {
            if (--timeout == 0) {
                return;
            } else {
                // Keep waiting
            }
            hal_cpu_relax();
        }
        apic_write(LAPIC_ICR_LOW, 0x000C4000 | vector);
    }
}

static void pit_wait_calibration(uint32_t ms) {
    extern void pit_lock_acquire(uint64_t* flags);
    extern void pit_lock_release(uint64_t flags);
    
    uint64_t flags;
    pit_lock_acquire(&flags);

    uint32_t count = (1193182 * ms) / 1000;
    if (count > 0xFFFF) {
        count = 0xFFFF;
    } else {
        // Valid count
    }

    uint8_t orig_61 = hal_io_inb(0x61);
    hal_io_outb(0x61, (orig_61 & 0xFD) | 1); 
    hal_io_outb(0x43, 0xB0);
    hal_io_outb(0x42, count & 0xFF);
    hal_io_outb(0x42, (count >> 8) & 0xFF);
    
    while ((hal_io_inb(0x61) & 0x20) == 0) {
        hal_cpu_relax();
    }
    
    hal_io_outb(0x61, orig_61);
    pit_lock_release(flags);
}

static void apic_clear_isr() {
    if (x2apic_mode) {
        return;
    } else {
        // Legacy APIC
    }
    
    uint32_t isr_val = apic_read(LAPIC_ISR);
    if (isr_val != 0) {
        apic_write(LAPIC_EOI, 0);
    } else {
        // Clean
    }
}

static bool check_enable_x2apic() {
    uint32_t a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1));
    
    if (c & (1 << 21)) { 
        uint64_t base = rdmsr(MSR_IA32_APIC_BASE);
        base |= (1 << 10); 
        base |= (1 << 11); 
        wrmsr(MSR_IA32_APIC_BASE, base);
        x2apic_mode = true;
        return true;
    } else {
        return false;
    }
}

void apic_enable_local() {
    if (!x2apic_mode) {
        check_enable_x2apic();
    } else {
        uint64_t base = rdmsr(MSR_IA32_APIC_BASE);
        if (!(base & (1 << 11))) {
            base |= (1 << 10) | (1 << 11);
            wrmsr(MSR_IA32_APIC_BASE, base);
        } else {
            // Already enabled
        }
    }

    if (!x2apic_mode && lapic_base_virt == 0) {
        return;
    } else {
        // Valid base
    }

    apic_write(LAPIC_ERROR, 0);
    apic_write(LAPIC_ERROR, 0);
    apic_clear_isr();
    apic_write(LAPIC_SVR, apic_read(LAPIC_SVR) | SVR_ENABLE | SPURIOUS_VECTOR);
    apic_write(LAPIC_LINT0, 0x10000); 
    apic_write(LAPIC_LINT1, 0x10000);   
    apic_write(LAPIC_TPR, 0); 
    apic_write(LAPIC_EOI, 0);
}

void init_apic(uint32_t physical_addr) {
    lapic_base_virt = (uint64_t)ioremap(physical_addr, 4096, PAGE_PRESENT | PAGE_WRITE | PAGE_PCD);
    
    if (!lapic_base_virt) {
        serial_write("[APIC] Critical: Failed to map Local APIC!\n");
        return;
    } else {
        // Mapped
    }

    if (check_enable_x2apic()) {
        serial_write("[APIC] x2APIC Mode Enabled (MSR Based).\n");
    } else {
        serial_write("[APIC] Using Legacy xAPIC (MMIO Based).\n");
    }

    apic_enable_local();
    
    uint32_t a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1));
    if (c & (1 << 24)) {
        use_tsc_deadline = true;
        serial_write("[APIC] TSC-Deadline Mode Supported.\n");
    } else {
        // Legacy timer
    }
    
    spinlock_init(&timer_lock);
    
    extern void pit_lock_init();
    pit_lock_init(); 
    
    char buf[128];
    snprintf(buf, sizeof(buf), "[APIC] Initialized. Phys: 0x%x\n", physical_addr);
    serial_write(buf);
}

void apic_finalize() {
}

bool is_apic_enabled() { 
    return (lapic_base_virt != 0 || x2apic_mode); 
}

void apic_send_eoi() {
    if (is_apic_enabled()) {
        apic_write(LAPIC_EOI, 0);
    } else {
        // Disabled
    }
}

void apic_timer_init(uint32_t frequency) {
    if (!x2apic_mode && lapic_base_virt == 0) {
        return;
    } else {
        // Valid
    }
    
    uint64_t flags = spinlock_acquire(&timer_lock);
    
    if (!timer_calibrated) {
        apic_write(LAPIC_TDCR, 0x03); 
        apic_write(LAPIC_TICR, 0xFFFFFFFF); 
        
        uint32_t start_count = apic_read(LAPIC_TCCR);
        pit_wait_calibration(50); 
        uint32_t end_count = apic_read(LAPIC_TCCR);
        
        uint32_t ticks_in_50ms = start_count - end_count;
        ticks_per_ms = ticks_in_50ms / 50; 
        
        if (ticks_per_ms == 0) {
            ticks_per_ms = 1000; 
        } else {
            // Calibrated
        }
        
        __atomic_store_n(&timer_calibrated, true, __ATOMIC_RELEASE);
        
        apic_write(LAPIC_TIMER, 0x10000);
        
        char buf[64];
        snprintf(buf, sizeof(buf), "[APIC] Calibrated via PIT. Ticks/ms: %d\n", ticks_per_ms);
        serial_write(buf);
    } else {
        // Already calibrated
    }
    
    spinlock_release(&timer_lock, flags);

    while (!__atomic_load_n(&timer_calibrated, __ATOMIC_ACQUIRE)) {
        hal_cpu_relax();
    }
    
    if (ticks_per_ms == 0) {
        ticks_per_ms = 1000;
    } else {
        // Valid
    }

    if (use_tsc_deadline) {
        apic_write(LAPIC_TIMER, 32 | APIC_TIMER_TSC_DEADLINE);
        apic_timer_oneshot(10000); 
    } else {
        uint32_t ticks_target = (ticks_per_ms * 1000) / frequency;
        
        uint32_t lvt_val = 32 | APIC_TIMER_PERIODIC;
        lvt_val &= ~(0x10000);
        
        apic_write(LAPIC_TDCR, 0x03);
        apic_write(LAPIC_TIMER, lvt_val); 
        apic_write(LAPIC_TICR, ticks_target);
    }
}

void apic_timer_oneshot(uint64_t us) {
    if (use_tsc_deadline) {
        uint32_t lo, hi;
        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
        uint64_t current_tsc = ((uint64_t)hi << 32) | lo;
        
        uint64_t target_tsc = current_tsc + ((ticks_per_ms * us) / 1000ULL); 
        wrmsr(MSR_IA32_TSC_DEADLINE, target_tsc);
        
    } else {
        uint32_t ticks = (ticks_per_ms * us) / 1000;
        
        uint32_t lvt_val = 32 | APIC_TIMER_ONE_SHOT;
        lvt_val &= ~(0x10000);
        
        apic_write(LAPIC_TDCR, 0x03);
        apic_write(LAPIC_TIMER, lvt_val);
        apic_write(LAPIC_TICR, ticks);
    }
}

uint8_t get_apic_id() {
    if (x2apic_mode) {
        return (uint8_t)rdmsr(MSR_X2APIC_BASE + (LAPIC_ID >> 4));
    } else {
        if (lapic_base_virt == 0) {
            return 0;
        } else {
            return (apic_read(LAPIC_ID) >> 24) & 0xFF;
        }
    }
}

uint64_t apic_get_msi_address(uint8_t cpu_id) {
    return 0xFEE00000 | ((uint64_t)cpu_id << 12);
}

uint32_t apic_get_msi_data(uint8_t vector) {
    return vector & 0xFF; 
}