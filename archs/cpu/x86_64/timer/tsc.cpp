// archs/cpu/x86_64/timer/tsc.cpp
#include "archs/cpu/x86_64/timer/tsc.h"
#include "system/device/device.h"
#include "libc/stdio.h"
#include "drivers/serial/serial.h"
#include "system/process/process.h"
#include "archs/cpu/x86_64/core/cpuid.h"
#include "archs/cpu/cpu_hal.h"
#include "drivers/uefi/uefi.h" 

class TSCDriver : public Device {
public:
    TSCDriver();
    int init() override;
    int read(char* buffer, int size) override { (void)buffer; (void)size; return 0; }
    int write(const char* buffer, int size) override { (void)buffer; (void)size; return 0; }
};

extern "C" {
    void print_status(const char* prefix, const char* msg, const char* status);
    uint32_t acpi_get_pm_timer_port(void); 
}

static TSCDriver* g_tsc = nullptr;

static uint64_t tsc_frequency = 0;
static uint64_t boot_tsc = 0;

static bool pm_timer_reliable_calibration() {
    uint32_t pm_port = acpi_get_pm_timer_port();
    if (!pm_port) { return false; }

    uint32_t t1 = hal_io_inl(pm_port) & 0xFFFFFF;
    uint32_t t2 = 0; 
    
    while ((t2 = (hal_io_inl(pm_port) & 0xFFFFFF)) == t1) { hal_cpu_relax(); }
    
    uint64_t start_tsc = tsc_read_asm();
    t1 = t2;
    
    uint32_t target_delta = 35795;
    while (true) {
        t2 = hal_io_inl(pm_port) & 0xFFFFFF;
        uint32_t delta = 0;
        if (t2 >= t1) { 
            delta = t2 - t1; 
        } else { 
            delta = 0x1000000 - t1 + t2; 
        }
        if (delta >= target_delta) { break; } else { hal_cpu_relax(); }
    }
    
    uint64_t end_tsc = tsc_read_asm();
    uint32_t actual_delta = 0;
    if (t2 >= t1) { 
        actual_delta = t2 - t1; 
    } else { 
        actual_delta = 0x1000000 - t1 + t2; 
    }
    
    tsc_frequency = ((end_tsc - start_tsc) * 3579545ULL) / actual_delta;
    return true;
}

static void pit_reliable_calibration() {
    hal_io_outb(0x43, 0x34); 
    hal_io_outb(0x40, 0xFF);
    hal_io_outb(0x40, 0xFF);

    uint64_t start_tsc = tsc_read_asm();
    
    uint32_t start_tick = 0xFFFF;
    uint32_t current_tick = 0;
    uint32_t elapsed = 0;
    uint64_t timeout_cycles = 100000000;

    while (elapsed < 59659 && timeout_cycles-- > 0) {
        hal_io_outb(0x43, 0x00); 
        uint8_t lo = hal_io_inb(0x40);
        uint8_t hi = hal_io_inb(0x40);
        current_tick = (hi << 8) | lo;
        
        if (current_tick <= start_tick) {
            elapsed += (start_tick - current_tick);
        } else {
            elapsed += (start_tick + (0x10000 - current_tick));
        }
        start_tick = current_tick;
    }

    if (timeout_cycles == 0) {
        tsc_frequency = 2000000000ULL;
        serial_write("[TSC] Hardware failure: PIT calibration timed out. Forcing 2GHz.\n");
        return;
    }

    uint64_t end_tsc = tsc_read_asm();
    tsc_frequency = ((end_tsc - start_tsc) * 1193182ULL) / elapsed;
}

TSCDriver::TSCDriver() : Device("System Timer (TSC)", DEV_UNKNOWN) {
    g_tsc = this;
}

int TSCDriver::init() {
    uint32_t eax=0, ebx=0, ecx=0, edx=0;
    bool freq_found = false;

    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x15));
    if (eax != 0 && ebx != 0 && ecx != 0) {
        tsc_frequency = ((uint64_t)ecx * ebx) / eax;
        freq_found = true;
    } else {
        __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x16));
        if (eax != 0) {
            tsc_frequency = (uint64_t)(eax & 0xFFFF) * 1000000ULL;
            freq_found = true;
        }
    }

    if (!freq_found || tsc_frequency < 1000000) {
        serial_write("[TSC] CPUID failed. Trying UEFI Runtime Services...\n");
        if (uefi_available()) {
            efi_time_t t1, t2;
            if (uefi_get_time(&t1)) {
                uint64_t start_tsc = tsc_read_asm();
                while (uefi_get_time(&t2)) {
                    if (t2.second != t1.second) { break; } else { hal_cpu_relax(); }
                }
                uint64_t end_tsc = tsc_read_asm();
                tsc_frequency = end_tsc - start_tsc;
                freq_found = true;
            } else {
                serial_write("[TSC] UEFI GetTime failed.\n");
            }
        } else {
            serial_write("[TSC] UEFI not available.\n");
        }
    }

    if (!freq_found || tsc_frequency < 1000000) {
        if (pm_timer_reliable_calibration()) {
            serial_write("[TSC] Calibrated via Modern ACPI PM Timer.\n");
            freq_found = true;
        }
    }

    if (!freq_found || tsc_frequency < 1000000) {
        serial_write("[TSC] ACPI PM Timer absent. Falling back to Legacy PIT...\n");
        pit_reliable_calibration();
    }

    if (tsc_frequency < 100000000ULL) {
        tsc_frequency = 2000000000ULL;
        char msg[128];
        snprintf(msg, sizeof(msg), "VM Timer Anomaly! Forced fallback to 2000 MHz");
        print_status("[ TSC  ]", msg, "WARN");
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "Clock synchronized at %lu MHz", tsc_frequency / 1000000);
        print_status("[ TSC  ]", msg, "INFO");
    }

    boot_tsc = tsc_read_asm();
    DeviceManager::registerDevice(this);
    return 1;
}

extern "C" {
    void init_tsc() {
        TSCDriver* driver = new TSCDriver();
        driver->init();
    }
    
    uint64_t get_tsc_freq() { return tsc_frequency; }
    
    uint64_t get_uptime_ns() {
        if (tsc_frequency == 0) { return 0; }
        uint64_t now = tsc_read_asm();
        if (now < boot_tsc) { return 0; }
        
        uint64_t delta = now - boot_tsc;
        uint64_t freq_mhz = tsc_frequency / 1000000;
        if (freq_mhz == 0) { return 0; }

        uint64_t seconds = delta / tsc_frequency;
        uint64_t remainder = delta % tsc_frequency;
        uint64_t ns_part = (remainder * 1000) / freq_mhz;
        
        return (seconds * 1000000000ULL) + ns_part;
    }

    void sleep_ns(uint64_t ns) {
        if (tsc_frequency == 0) { return; }
        uint64_t start = tsc_read_asm();
        uint64_t freq_mhz = tsc_frequency / 1000000;
        uint64_t target_ticks = (ns / 1000ULL) * freq_mhz; 
        while ((tsc_read_asm() - start) < target_ticks) { hal_cpu_relax(); }
    }
    
    void tsc_delay_ms(uint32_t ms) {
        if (tsc_frequency == 0) { return; }
        uint64_t start = tsc_read_asm();
        uint64_t target_ticks = (uint64_t)ms * (tsc_frequency / 1000);
        while ((tsc_read_asm() - start) < target_ticks) { hal_cpu_relax(); }
    }

    void sleep_ms(uint64_t ms) {
        if (scheduler_active && ms >= 10) {
            uint64_t ticks = (ms + 3) / 4;
            task_sleep(ticks);
        } else {
            tsc_delay_ms(ms);
        }
    }

    void timer_sleep(uint64_t ticks) { task_sleep(ticks); }
    
    uint64_t timer_get_ticks() {
        if (tsc_frequency == 0) {
            extern volatile uint64_t system_ticks;
            return system_ticks; 
        }
        uint64_t now = tsc_read_asm();
        if (now < boot_tsc) { return 0; }
        uint64_t delta = now - boot_tsc;
        return (delta * 250) / tsc_frequency;
    }
}