// archs/cpu/x86_64/vendors/generic.c
#include "archs/cpu/x86_64/core/cpuid.h"
#include "libc/stdio.h"
#include "drivers/serial/serial.h"
static int generic_get_temp() {
    return 0; 
}

static void generic_init() {
}

cpu_driver_t cpu_driver_generic = {
    .driver_name = "Generic x86_64 Driver",
    .init = generic_init,
    .get_temp = generic_get_temp
};