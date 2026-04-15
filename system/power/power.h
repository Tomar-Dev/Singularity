// system/power/power.h
#ifndef POWER_H
#define POWER_H

#ifdef __cplusplus
extern "C" {
#endif

void system_reboot();

void system_shutdown(const char* reason);

void print_shutdown(const char* msg);

#ifdef __cplusplus
}
#endif

#endif
