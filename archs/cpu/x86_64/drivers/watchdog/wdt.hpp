// archs/cpu/x86_64/drivers/watchdog/wdt.hpp
#ifndef WDT_HPP
#define WDT_HPP

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void init_wdt();
void wdt_kick();
void wdt_test_crash();

#ifdef __cplusplus
}
#endif

#endif