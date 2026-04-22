// archs/cpu/x86_64/syscall/syscall.c
#include "archs/cpu/x86_64/syscall/syscall.h"
#include "archs/cpu/x86_64/idt/idt.h"
#include "drivers/video/framebuffer.h"
#include "system/console/console.h" 
#include "archs/cpu/x86_64/core/msr.h" 
#include "archs/cpu/cpu_hal.h"
#include <stddef.h> 
#include "kernel/debug.h"
#include "libc/string.h" 
#include "archs/cpu/x86_64/core/cpuid.h"
#include "archs/cpu/x86_64/memory/paging.h" 

extern void syscall_entry(); 

typedef void (*syscall_handler_t)(registers_t*);
syscall_handler_t syscall_routines[256] = {NULL};

static bool is_valid_user_ptr(const void* ptr, size_t size) {
    uintptr_t addr = (uintptr_t)ptr;
    
    if (addr == 0 || size == 0) {
        return false;
    } else {
        // Valid base
    }
    
    if (addr > UINTPTR_MAX - size) {
        return false;
    } else {
        // No integer overflow
    }
    
    if ((addr + size) > KERNEL_SPACE_BASE) {
        return false;
    } else {
        // Below kernel boundary
    }
    
    if (addr > 0x00007FFFFFFFFFFF) {
        return false;
    } else {
        // Canonical address
    }
    
    for (uintptr_t p = (addr & ~0xFFFULL); p < addr + size; p += 4096) {
        if (get_physical_address(p) == 0) {
            return false;
        } else {
            // Mapped
        }
    }
    
    return true;
}

static void sys_write(registers_t* regs) {
    char* str = (char*)regs->rdi;
    
    if (!is_valid_user_ptr(str, 1)) {
        serial_write("[SYSCALL] Security Violation: Invalid Base Pointer passed to sys_write\n");
        return;
    } else {
        // Valid pointer
    }
    
    char safe_buf[256];
    size_t i = 0;
    
    if (cpu_info.has_smap) {
        stac();
    } else {
        // SMAP not supported
    }
    
    while (i < 255) {
        if ((((uint64_t)str + i) & 0xFFF) == 0) {
            if (!is_valid_user_ptr(str + i, 1)) {
                break;
            } else {
                // Within page bounds
            }
        } else {
            // Within page bounds
        }
        
        char c = str[i];
        if (c == '\0') {
            break;
        } else {
            // Valid character
        }
        safe_buf[i++] = c;
    }
    
    if (cpu_info.has_smap) {
        clac();
    } else {
        // SMAP not supported
    }
    
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
    } else {
        // Already registered
    }

    uint64_t efer = rdmsr(MSR_EFER);
    if (!(efer & 1)) {
        efer |= 1; 
        wrmsr(MSR_EFER, efer);
    } else {
        // SCE already enabled
    }

    uint64_t star = ((uint64_t)0x0013 << 48) | ((uint64_t)0x08 << 32);
    wrmsr(0xC0000081, star);
    wrmsr(0xC0000082, (uint64_t)syscall_entry);
    wrmsr(0xC0000084, 0x200 | 0x100 | 0x400); 
}