// kernel/config.h
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#define SINGULARITY_SYS_NAME  "Singularity"
#define SINGULARITY_SYS_VER   "v6.9.4"
#define SINGULARITY_SHELL_VER "v1.0.0"
#define SINGULARITY_SYS_ARCH  "x86_64 SMP"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int timezone;
    bool fast_boot;
    int log_level;
    bool lockdown;
} kernel_config_t;

extern kernel_config_t kconfig;

void config_init(const char* cmdline);

#define FPU_DEFAULT_MXCSR 0x1F80

#ifdef __cplusplus
}
#endif

#endif