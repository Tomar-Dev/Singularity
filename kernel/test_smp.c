// kernel/test_smp.c
#include "kernel/test_framework.h"
#include "archs/cpu/x86_64/smp/smp.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "drivers/serial/serial.h"
TEST(smp_fpu_consistency) {
    uint8_t cpu_id = get_current_cpu_id();
    serial_printf("   [TEST-SMP] CPU %d FPU Check... ", cpu_id);
    
    float a = 1.5f;
    float b = 2.5f;
    float c = a * b;
    
    if ((c > 3.74f && c < 3.76f)) {
        serial_printf("OK\n");
    } else {
        serial_printf("FAIL (Got: %d)\n", (int)c);
        panic_at(__FILE__, __LINE__, KERR_CPU_EX, "AP FPU Calculation Failed");
    }
}

void run_smp_tests() {
    test_smp_fpu_consistency();
}
