// system/security/stack_protector.c
#include "system/security/stack_protector.h"
#include "libc/stdio.h"
#include "kernel/debug.h"
#include "system/security/rng.h" 
__attribute__((used, visibility("default"))) uintptr_t __stack_chk_guard = 0;

static uint8_t emergency_panic_stack[8192] __attribute__((aligned(16)));

__attribute__((no_stack_protector))
void init_stack_protector() {
    __stack_chk_guard = get_secure_random();
    if (__stack_chk_guard == 0) {
        __stack_chk_guard = 0xDEADBEEF0000FFFF;
    }
}

// FIX: 'static' kaldırıldı ve 'used' eklendi.
__attribute__((noreturn, noinline, used, no_stack_protector)) 
void trigger_panic_safe(void) {
    panic_at("STACK_CHECK", 0, KERR_STACK_SMASH, "Stack Buffer Overflow Detected! (Canary Mismatch)");
    for(;;) hal_cpu_halt();
}

__attribute__((naked, noreturn, used, no_stack_protector)) 
void __stack_chk_fail(void) {
    __asm__ volatile(
        "cli \n\t"
        "mov %0, %%rsp \n\t"
        "xor %%rbp, %%rbp \n\t"
        "call trigger_panic_safe \n\t"
        : 
        : "r" (&emergency_panic_stack[8192 - 16]) 
        : "memory"
    );
}
