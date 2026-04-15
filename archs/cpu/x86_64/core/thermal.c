// archs/cpu/x86_64/core/thermal.c
#include "archs/cpu/x86_64/core/thermal.h"
#include "archs/cpu/x86_64/core/cpuid.h"
int get_cpu_temp() {
    if (current_cpu_driver && current_cpu_driver->get_temp) {
        return current_cpu_driver->get_temp();
    }
    return 0;
}
