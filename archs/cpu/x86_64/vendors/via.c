// archs/cpu/x86_64/vendors/via.c
#include "archs/cpu/x86_64/core/cpuid.h"
#include "archs/cpu/x86_64/core/msr.h"
#include "libc/stdio.h"
#include "archs/cpu/cpu_hal.h"
static int via_get_temp() {
    if (!cpu_info.has_msr) return -1;
    uint64_t msr = rdmsr(0x19C);
    if (msr & (1ULL << 31)) {
        return 100 - ((msr >> 16) & 0x7F);
    }
    return 0;
}

static void via_init() {
    serial_write("[CPU] Applying VIA/Centaur specific optimizations...\n");
    if (cpu_info.has_padlock) {
        serial_write("[CPU] VIA PadLock Security Engine detected.\n");
    }
}

cpu_driver_t cpu_driver_via = {
    .driver_name = "VIA/Zhaoxin Native Driver",
    .init = via_init,
    .get_temp = via_get_temp
};
