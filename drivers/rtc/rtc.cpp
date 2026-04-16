// drivers/rtc/rtc.cpp
#include "drivers/rtc/rtc.hpp"
#include "kernel/config.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "drivers/uefi/uefi.h"
#include "drivers/serial/serial.h"
#include "archs/cpu/cpu_hal.h"
#include "archs/cpu/x86_64/sync/spinlock.h"
#include "drivers/timer/tsc.h" 

extern "C" void print_status(const char* prefix, const char* msg, const char* status);

static RTCDriver* g_rtc = nullptr;
static spinlock_t rtc_lock = {0, 0, {0}}; 

#define CMOS_ADDR_PORT  0x70
#define CMOS_DATA_PORT  0x71

#define CMOS_REG_SECONDS   0x00
#define CMOS_REG_MINUTES   0x02
#define CMOS_REG_HOURS     0x04
#define CMOS_REG_DAY       0x07
#define CMOS_REG_MONTH     0x08
#define CMOS_REG_YEAR      0x09
#define CMOS_REG_STATUS_A  0x0A
#define CMOS_REG_STATUS_B  0x0B

static uint8_t cmos_read(uint8_t reg) {
    hal_io_outb(CMOS_ADDR_PORT, reg);
    hal_cpu_relax();
    uint8_t val = hal_io_inb(CMOS_DATA_PORT);
    return val;
}

static inline uint8_t bcd_to_bin(uint8_t bcd) {
    return (uint8_t)(((bcd >> 4) & 0x0F) * 10 + (bcd & 0x0F));
}

static inline bool cmos_is_updating(void) {
    return (cmos_read(CMOS_REG_STATUS_A) & 0x80) != 0;
}

static bool cmos_wait_for_stable(void) {
    for (int attempt = 0; attempt < 2; attempt++) {
        int timeout = 1000000; 
        while (timeout-- > 0 && !cmos_is_updating()) {
            hal_cpu_relax();
        }
        timeout = 100000;
        while (timeout-- > 0 && cmos_is_updating()) {
            hal_cpu_relax();
        }
        if (timeout > 0) {
            return true;
        }
    }
    return false;
}

static void cmos_get_time_raw(rtc_time_t* out) {
    if (!out) return;

    uint8_t sec1, min1, hr1, day1, mon1;
    uint16_t yr1;
    uint8_t sec2, min2, hr2, day2, mon2;
    uint16_t yr2;

    uint64_t flags = spinlock_acquire(&rtc_lock);

    do {
        cmos_wait_for_stable();
        sec1 = cmos_read(CMOS_REG_SECONDS);
        min1 = cmos_read(CMOS_REG_MINUTES);
        hr1  = cmos_read(CMOS_REG_HOURS);
        day1 = cmos_read(CMOS_REG_DAY);
        mon1 = cmos_read(CMOS_REG_MONTH);
        yr1  = cmos_read(CMOS_REG_YEAR);

        cmos_wait_for_stable();
        sec2 = cmos_read(CMOS_REG_SECONDS);
        min2 = cmos_read(CMOS_REG_MINUTES);
        hr2  = cmos_read(CMOS_REG_HOURS);
        day2 = cmos_read(CMOS_REG_DAY);
        mon2 = cmos_read(CMOS_REG_MONTH);
        yr2  = cmos_read(CMOS_REG_YEAR);
    } while (sec1 != sec2 || min1 != min2 || hr1 != hr2 || day1 != day2 || mon1 != mon2 || yr1 != yr2);

    uint8_t sec = sec2, min = min2, hr = hr2;
    uint8_t day = day2, mon = mon2;
    uint16_t yr = yr2;

    uint8_t status_b = cmos_read(CMOS_REG_STATUS_B);
    spinlock_release(&rtc_lock, flags);

    bool is_binary   = (status_b & 0x04) != 0;
    bool is_24hr     = (status_b & 0x02) != 0;

    if (!is_binary) {
        sec = bcd_to_bin(sec);
        min = bcd_to_bin(min);
        day = bcd_to_bin(day);
        mon = bcd_to_bin(mon);
        yr  = bcd_to_bin((uint8_t)yr);

        if (!is_24hr) {
            uint8_t pm = hr & 0x80;
            hr = bcd_to_bin(hr & 0x7F);
            if (pm && hr != 12) {
                hr = (uint8_t)(hr + 12);
            } else if (!pm && hr == 12) {
                hr = 0;
            }
        } else {
            hr = bcd_to_bin(hr);
        }
    } else {
        if (!is_24hr) {
            uint8_t pm = hr & 0x80;
            hr &= 0x7F;
            if (pm && hr != 12) {
                hr = (uint8_t)(hr + 12);
            } else if (!pm && hr == 12) {
                hr = 0;
            }
        }
    }

    if (yr >= 70) {
        out->year = (uint16_t)(1900 + yr);
    } else {
        out->year = (uint16_t)(2000 + yr);
    }

    out->second = sec;
    out->minute = min;
    out->hour   = hr;
    out->day    = (day  >= 1  && day  <= 31) ? day  : 1;
    out->month  = (mon  >= 1  && mon  <= 12) ? mon  : 1;
}

RTCDriver::RTCDriver() : Device("System Clock", DEV_UNKNOWN) {
    g_rtc = this;
}

int RTCDriver::init(void) {
    DeviceManager::registerDevice(this);
    return 1;
}

void RTCDriver::getTime(rtc_time_t* time) {
    if (!time) return;

    bool got_time = false;

    if (uefi_available()) {
        efi_time_t efi_t;
        memset(&efi_t, 0, sizeof(efi_t));
        if (uefi_get_time(&efi_t)) {
            time->second = efi_t.second;
            time->minute = efi_t.minute;
            time->hour   = efi_t.hour;
            time->day    = efi_t.day;
            time->month  = efi_t.month;
            time->year   = efi_t.year;
            got_time = true;
        } else {
            serial_write("[RTC] UEFI GetTime() failed. Falling back to CMOS.\n");
        }
    }

    if (!got_time) {
        cmos_get_time_raw(time);
    }

    int tz = kconfig.timezone;
    if (tz < -12 || tz > 14) tz = 0;

    if (tz != 0) {
        int new_hour = (int)time->hour + tz;
        if (new_hour >= 24) time->hour = (uint8_t)(new_hour - 24);
        else if (new_hour < 0) time->hour = (uint8_t)(new_hour + 24);
        else time->hour = (uint8_t)new_hour;
    }
}

void RTCDriver::delaySeconds(uint32_t seconds) {
    if (seconds == 0) return;
    timer_sleep((uint64_t)seconds * 250); 
}

void RTCDriver::getFormattedTime(char* buffer) {
    if (!buffer) return;
    rtc_time_t t;
    memset(&t, 0, sizeof(t));
    getTime(&t);
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    sprintf(buffer, "%02d:%02d:%02d", t.hour, t.minute, t.second);
}

extern "C" {

    void rtc_init(void) {
        RTCDriver* rtc = new RTCDriver();
        if (!rtc->init()) {
            serial_write("[RTC] Driver init returned failure.\n");
            delete rtc;
            g_rtc = nullptr;
        }
    }

    void rtc_get_time(rtc_time_t* time) {
        if (g_rtc && time) {
            g_rtc->getTime(time);
        } else if (!g_rtc) {
            serial_write("[RTC] rtc_get_time: driver not initialised.\n");
        }
    }

    void rtc_delay_seconds(uint32_t seconds) {
        if (g_rtc) {
            g_rtc->delaySeconds(seconds);
        }
    }

    void rtc_wait_one_second(void) {
        uint64_t start_tsc = tsc_read_asm();
        uint64_t freq = get_tsc_freq();
        if (freq == 0) freq = 2000000000ULL;
        uint64_t timeout_cycles = freq;
        
        uint8_t start_sec = cmos_read(CMOS_REG_SECONDS);
        while (cmos_read(CMOS_REG_SECONDS) == start_sec) {
            if ((tsc_read_asm() - start_tsc) > timeout_cycles) break;
            hal_cpu_relax();
        }
    }

    void rtc_get_formatted_time(char* buffer) {
        if (!buffer) return;
        if (g_rtc) {
            g_rtc->getFormattedTime(buffer);
        } else {
            // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
            sprintf(buffer, "00:00:00");
        }
    }
}
