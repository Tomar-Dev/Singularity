// archs/cpu/x86_64/vendors/hygon.c
#include "archs/cpu/x86_64/core/cpuid.h"
#include "archs/cpu/x86_64/core/msr.h"
#include "libc/stdio.h"
#include "drivers/serial/serial.h"
#define MSR_EFER 0xC0000080

static int hygon_get_temp() {
    return -1; 
}

static void hygon_init() {
    serial_write("[CPU] Applying Hygon Dhyana optimizations...\n");
    if (cpu_info.has_msr) {
        uint64_t efer = rdmsr(MSR_EFER);
        efer |= 1; 
        wrmsr(MSR_EFER, efer);
        serial_write("[CPU] Hygon Syscall Instruction Enabled.\n");
    }
}

cpu_driver_t cpu_driver_hygon = {
    .driver_name = "Hygon Dhyana Driver",
    .init = hygon_init,
    .get_temp = hygon_get_temp
};
