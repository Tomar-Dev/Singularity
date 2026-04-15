// kernel/config.c
#include "kernel/config.h"
#include "libc/string.h"
#include "libc/stdio.h"
#include "drivers/serial/serial.h"

kernel_config_t kconfig = {
    .timezone = 3,
    .fast_boot = false,
    .log_level = 0,
    .lockdown = true
};

void config_init(const char* cmdline) {
    if (!cmdline || cmdline[0] == '\0') {
        serial_write("[CONFIG] No cmdline provided. Using safe defaults.\n");
        return;
    }

    serial_printf("[CONFIG] Kernel Cmdline: %s\n", cmdline);

    char buf[512];
    strncpy(buf, cmdline, 511);
    buf[511] = '\0';

    char* p = buf;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        char* token = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) {
            *p = '\0';
            p++;
        }

        char* eq = strchr(token, '=');
        if (eq) {
            *eq = '\0';
            char* key = token;
            char* val = eq + 1;

            if (strcmp(key, "timezone") == 0) {
                int tz = 0, sign = 1;
                char* vp = val;
                if (*vp == '-') { sign = -1; vp++; }
                while (*vp >= '0' && *vp <= '9') {
                    tz = tz * 10 + (*vp - '0');
                    vp++;
                }
                tz *= sign;
                if (tz >= -12 && tz <= 14) kconfig.timezone = tz;
            }
            else if (strcmp(key, "fast_boot") == 0) {
                kconfig.fast_boot = (strcmp(val, "1") == 0 || strcmp(val, "true") == 0);
            }
            else if (strcmp(key, "lockdown") == 0) {
                kconfig.lockdown = (strcmp(val, "1") == 0 || strcmp(val, "true") == 0);
            }
            else if (strcmp(key, "log_level") == 0) {
                kconfig.log_level = val[0] - '0';
            }
        }
    }

    serial_printf("[CONFIG] Applied: TZ=%d, FastBoot=%d, Lockdown=%d, LogLevel=%d\n",
                  kconfig.timezone, kconfig.fast_boot, kconfig.lockdown, kconfig.log_level);
}