// archs/cpu/x86_64/syscall/syscall.c
#include "archs/cpu/x86_64/syscall/syscall.h"
#include "archs/cpu/x86_64/idt/idt.h"
#include "drivers/video/framebuffer.h"
#include "system/console/console.h" 
#include "archs/cpu/x86_64/core/msr.h" 
#include "drivers/serial/serial.h"
#include <stddef.h> 
#include "kernel/debug.h"
#include "drivers/apic/apic.h" 
#include "libc/string.h" 
#include "archs/cpu/x86_64/core/cpuid.h"

extern void syscall_entry(); 

typedef void (*syscall_handler_t)(registers_t*);
syscall_handler_t syscall_routines[256] = {NULL};

static bool is_valid_user_ptr(const void* ptr, size_t size) {
    uintptr_t addr = (uintptr_t)ptr;
    
    if (addr == 0) return false;
    if (addr + size < addr) return false;
    if (addr >= KERNEL_SPACE_BASE || (addr + size) > KERNEL_SPACE_BASE) return false;
    if (addr > 0x00007FFFFFFFFFFF) return false;
    
    return true;
}

static void sys_write(registers_t* regs) {
    char* str = (char*)regs->rdi;
    
    if (!is_valid_user_ptr(str, 1)) {
        serial_write("[SYSCALL] Security Violation: Invalid Base Pointer passed to sys_write\n");
        return;
    }
    
    char safe_buf[256];
    size_t i = 0;
    
    // GÜVENLİK YAMASI: SMAP (Supervisor Mode Access Prevention) Bypass Zırhı
    if (cpu_info.has_smap) stac();
    
    while (i < 255) {
        if ((((uint64_t)str + i) & 0xFFF) == 0) {
            if (!is_valid_user_ptr(str + i, 1)) break;
        }
        char c = str[i];
        if (c == '\0') break;
        safe_buf[i++] = c;
    }
    
    if (cpu_info.has_smap) clac();
    
    safe_buf[i] = '\0';
    console_write(safe_buf);
}

void syscall_dispatcher(registers_t* regs) {
    uint64_t call_num = regs->rax; 

    if (call_num < 256 && syscall_routines[call_num] != NULL) {
        syscall_handler_t func = syscall_routines[call_num];
        func(regs);
    } else {
        serial_printf("[SYSCALL] Unknown syscall: %d\n", (int)call_num);
    }
}

void init_syscalls() {
    static volatile int handlers_registered = 0;
    
    if (__atomic_test_and_set(&handlers_registered, __ATOMIC_SEQ_CST) == 0) {
        syscall_routines[SYSCALL_WRITE] = sys_write;
    }

    uint64_t efer = rdmsr(MSR_EFER);
    if (!(efer & 1)) {
        efer |= 1; 
        wrmsr(MSR_EFER, efer);
    }

    uint64_t star = ((uint64_t)0x0013 << 48) | ((uint64_t)0x08 << 32);
    wrmsr(0xC0000081, star);
    wrmsr(0xC0000082, (uint64_t)syscall_entry);
    wrmsr(0xC0000084, 0x200 | 0x100 | 0x400); 
}