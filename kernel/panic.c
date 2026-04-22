// kernel/panic.c
#include "kernel/debug.h"
#include "libc/stdio.h"
#include "libc/string.h" 
#include "drivers/video/framebuffer.h" 
#include "system/console/console.h" 
#include "archs/cpu/cpu_hal.h"
#include "kernel/ksyms.h" 
#include "system/process/process.h" 
#include "drivers/uefi/uefi.h" 
#include "memory/paging.h" 
#include "archs/cpu/x86_64/core/msr.h"
#include "kernel/config.h"

extern void console_set_auto_flush(bool enabled);
extern uint64_t timer_get_ticks();
extern void stdio_force_unlock(); 
extern void console_force_unlock(); 
extern void framebuffer_force_unlock();

extern bool scheduler_active;

static registers_t* global_panic_regs = NULL;
volatile bool global_panic_active = false; 
static volatile int panic_lock = 0;
static char panic_buffer[2048];

static volatile int panic_depth = 0;

static const char* get_error_string(kernel_error_t code) {
    switch (code) {
        case KERR_SUCCESS:             return "SUCCESS";
        case KERR_UNKNOWN:             return "UNKNOWN_FATAL";
        case KERR_CPU_EX:              return "CPU_EXCEPTION";
        case KERR_GP_FAULT:            return "GENERAL_PROTECTION_FAULT";
        case KERR_PAGE_FAULT:          return "PAGE_FAULT";
        case KERR_DOUBLE_FAULT:        return "DOUBLE_FAULT";
        case KERR_DIV_ZERO:            return "DIVIDE_BY_ZERO";
        case KERR_UNHANDLED_IRQ:       return "UNHANDLED_HARDWARE_IRQ";
        case KERR_MEM_OOM:             return "OUT_OF_MEMORY";
        case KERR_MEM_CORRUPT:         return "MEMORY_CORRUPTION";
        case KERR_HEAP_OVERFLOW:       return "HEAP_BUFFER_OVERFLOW";
        case KERR_HEAP_DOUBLE_FREE:    return "HEAP_DOUBLE_FREE";
        case KERR_HEAP_USE_AFTER_FREE: return "HEAP_USE_AFTER_FREE";
        case KERR_SLAB_CORRUPTION:     return "SLAB_LIST_CORRUPTION";
        case KERR_NULL_DEREFERENCE:    return "NULL_POINTER_DEREFERENCE";
        case KERR_OUT_OF_BOUNDS:       return "ARRAY_OUT_OF_BOUNDS";
        case KERR_STACK_SMASH:         return "STACK_SMASHING_DETECTED";
        case KERR_ASSERT_FAIL:         return "ASSERTION_FAILED";
        case KERR_UBSAN:               return "UNDEFINED_BEHAVIOR";
        case KERR_DEADLOCK_DETECTED:   return "SPINLOCK_DEADLOCK_DETECTED";
        case KERR_LOCK_ORDER:          return "INVALID_LOCK_ORDER";
        case KERR_DRIVER_FAIL:         return "DRIVER_HARDWARE_FAILURE";
        case KERR_FS_IO:               return "FILESYSTEM_IO_ERROR";
        case KERR_ACPI_ERROR:          return "ACPI_PARSING_ERROR";
        case KERR_DMA_CORRUPTION:      return "DMA_MEMORY_CORRUPTION";
        case KERR_FS_CORRUPTION:       return "FILESYSTEM_CORRUPTED";
        case KERR_RUST_PANIC:          return "CORE_PANIC";
        case KERR_RUST_OOM:            return "ALLOCATOR_OOM";
        case KERR_RUST_SAFE_MEM:       return "SAFEMEM_VIOLATION";
        case KERR_TEST:                return "MANUAL_TEST_PANIC";
        default:                       return "CRITICAL_SYSTEM_FAILURE";
    }
}

static void panic_print_serial(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(panic_buffer, sizeof(panic_buffer), fmt, args);
    va_end(args);
    dbg_direct(panic_buffer); 
}

void set_panic_registers(registers_t* regs) {
    global_panic_regs = regs;
}

static void panic_raw_reboot(void) {
    panic_print_serial("\n[PANIC] Executing Raw Hardware Reboot...\n");
    printf("\n[PANIC] Rebooting System...\n");
    framebuffer_flush();
    
    uint8_t good = 0x02;
    while (good & 0x02) good = hal_io_inb(0x64);
    hal_io_outb(0x64, 0xFE);
    
    hal_io_outb(0xCF9, 0x06);
    
    if (uefi_available()) uefi_reset_system(1); 
    
    __asm__ volatile ("lidt (%0); int3" : : "r" (0));
    for(;;) hal_cpu_halt();
}

static void panic_raw_shutdown(void) {
    panic_print_serial("\n[PANIC] Executing Raw Hardware Shutdown...\n");
    printf("\n[PANIC] Shutting Down System...\n");
    framebuffer_flush();
    
    acpi_power_off(); 
    
    hal_io_outw(0x604,  0x2000); 
    hal_io_outw(0xB004, 0x2000); 
    hal_io_outw(0x4004, 0x3400); 
    
    if (uefi_available()) uefi_reset_system(2); 
    
    __asm__ volatile ("lidt (%0); int3" : : "r" (0));
    for(;;) hal_cpu_halt();
}

static void panic_raw_diagnostic(void) {
    printf("\n--- SAFE PANIC DIAGNOSTICS ---\n");
    
    uint64_t cr0, cr2, cr3, cr4, efer, lstar;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    efer = rdmsr(0xC0000080);
    lstar = rdmsr(0xC0000082);
    
    console_set_color(CONSOLE_COLOR_YELLOW, CONSOLE_COLOR_LIGHT_BLUE);
    printf("CR0 : 0x%016lx | CR2 : 0x%016lx\n", cr0, cr2);
    printf("CR3 : 0x%016lx | CR4 : 0x%016lx\n", cr3, cr4);
    printf("EFER: 0x%016lx | LSTAR: 0x%016lx\n", efer, lstar);
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_LIGHT_BLUE);
    
    uint64_t up_ms = timer_get_ticks() * 4;
    printf("Panic Uptime: %lu.%03lu sec\n", up_ms / 1000, up_ms % 1000);
    printf("------------------------------\n");
    framebuffer_flush();
}

static void dump_exception_details(registers_t* regs, kernel_error_t code) {
    if (!regs) return;

    if (code == KERR_PAGE_FAULT) {
        uint64_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        
        bool present = regs->err_code & 0x01;
        bool write   = regs->err_code & 0x02;
        bool user    = regs->err_code & 0x04;
        bool rsvd    = regs->err_code & 0x08;
        bool fetch   = regs->err_code & 0x10;

        printf("\n[PAGE FAULT ANALYSIS]\n");
        printf("Faulting Address: 0x%016lx\n", cr2);
        
        if (cr2 < 4096) {
            console_set_color(CONSOLE_COLOR_YELLOW, CONSOLE_COLOR_LIGHT_BLUE);
            printf(">>> EXPLICIT NULL POINTER DEREFERENCE DETECTED <<<\n");
            console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_LIGHT_BLUE);
        } else if (cr2 >= regs->rsp - 8192 && cr2 <= regs->rsp + 8192) {
            console_set_color(CONSOLE_COLOR_YELLOW, CONSOLE_COLOR_LIGHT_BLUE);
            printf(">>> PROBABLE STACK OVERFLOW (Guard Page Hit) <<<\n");
            console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_LIGHT_BLUE);
        } else {
            // General mapped access fault
        }

        printf("Access Type     : %s\n", fetch ? "Instruction Fetch" : (write ? "Write" : "Read"));
        printf("Privilege Level : %s\n", user ? "User Mode (Ring 3)" : "Kernel Mode (Ring 0)");
        printf("Reason          : %s\n", present ? "Protection Violation (Page is read-only or no exec)" 
                                         : "Page Not Present (Not mapped in RAM)");
        if (rsvd) { printf("!!! Reserved Bit Violation in Page Table !!!\n"); } else { /* Clean */ }
        
        panic_print_serial("\n[PAGE FAULT] CR2: 0x%016lx | %s | %s | %s\n", 
                           cr2, write?"Write":"Read", user?"User":"Kernel", present?"ProtViol":"NotPresent");
    }
    else if (code == KERR_GP_FAULT) {
        printf("\n[GP FAULT ANALYSIS]\n");
        if (regs->err_code != 0) {
            bool ext = regs->err_code & 1;
            bool idt = (regs->err_code >> 1) & 1;
            uint16_t index = regs->err_code >> 3;
            printf("Segment Selector Index: 0x%04x (Table: %s)\n", index, idt ? "IDT" : "GDT/LDT");
            if (ext) { printf("Exception originated from an external event.\n"); } else { /* internal */ }
        } else {
            printf("Error Code is 0. Likely an invalid memory reference, unaligned SSE access, or privileged instruction.\n");
        }
    } else {
        // Safe skip
    }
}

static void dump_registers_formatted(registers_t* regs, kernel_error_t code) {
    if (!regs) {
        printf("\n(No Register Context Available)\n");
        return;
    } else {
        // Continue formatting
    }
    
    uint64_t offset = 0;
    const char* rip_sym = ksyms_resolve_symbol(regs->rip, &offset);
    
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

    console_set_color(CONSOLE_COLOR_YELLOW, CONSOLE_COLOR_LIGHT_BLUE); 
    printf("\n--- CPU CONTEXT (Exception) ---\n");
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_LIGHT_BLUE); 
    
    printf("RIP: 0x%016lx\n", regs->rip);
    
    printf("RFLAGS: 0x%016lx  ERR: 0x%016lx  CR3: 0x%016lx\n", regs->rflags, regs->err_code, cr3); 
    printf("RAX: 0x%016lx  RBX: 0x%016lx  RCX: 0x%016lx\n", regs->rax, regs->rbx, regs->rcx);
    printf("RDX: 0x%016lx  RSI: 0x%016lx  RDI: 0x%016lx\n", regs->rdx, regs->rsi, regs->rdi);
    printf("RBP: 0x%016lx  RSP: 0x%016lx\n", regs->rbp, regs->rsp);
    printf("R8 : 0x%016lx  R9 : 0x%016lx  R10: 0x%016lx\n", regs->r8,  regs->r9,  regs->r10);
    printf("R11: 0x%016lx  R12: 0x%016lx  R13: 0x%016lx\n", regs->r11, regs->r12, regs->r13);
    printf("R14: 0x%016lx  R15: 0x%016lx\n", regs->r14, regs->r15);

    console_set_color(CONSOLE_COLOR_YELLOW, CONSOLE_COLOR_LIGHT_BLUE);
    uint64_t rip_phys = get_physical_address(regs->rip);
    uint64_t rsp_phys = get_physical_address(regs->rsp);
    if (rip_phys == 0) {
        printf(" [!] DANGER: RIP is executing UNMAPPED memory!\n");
        panic_print_serial(" [!] DANGER: RIP is executing UNMAPPED memory!\n");
    } else {
        // Executing mapped logic
    }
    if (rsp_phys == 0) {
        printf(" [!] DANGER: RSP points to UNMAPPED memory (Stack destroyed)!\n");
        panic_print_serial(" [!] DANGER: RSP points to UNMAPPED memory (Stack destroyed)!\n");
    } else {
        // Stack valid
    }
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_LIGHT_BLUE);

    dump_exception_details(regs, code);

    panic_print_serial("\n--- CPU CONTEXT ---\n");
    if (rip_sym) { panic_print_serial("RIP: 0x%016lx <%s+%ld>\n", regs->rip, rip_sym, offset); }
    else         { panic_print_serial("RIP: 0x%016lx\n", regs->rip); }
    panic_print_serial("RAX: 0x%016lx  RBX: 0x%016lx  RCX: 0x%016lx\n", regs->rax, regs->rbx, regs->rcx);
    panic_print_serial("RDX: 0x%016lx  RSP: 0x%016lx  RBP: 0x%016lx\n", regs->rdx, regs->rsp, regs->rbp);
    
    print_stack_trace_from(regs->rbp, regs->rip);
}

__attribute__((noreturn))
void panic_at(const char* file, int line, kernel_error_t code, const char* message) {
    hal_interrupts_disable();
    
    uint8_t cpu_id = (uint8_t)hal_cpu_get_id();
    process_t* curr = current_process[cpu_id];
    
    bool is_fatal_memory_error = (code >= KERR_MEM_OOM && code <= KERR_OUT_OF_BOUNDS) || 
                                 (code == KERR_HEAP_DOUBLE_FREE) || 
                                 (code >= KERR_RUST_PANIC && code <= KERR_RUST_SAFE_MEM);
                                 
    bool can_sandbox = true;
    if (curr == NULL || curr->pid == 0) { can_sandbox = false; } else { /* Valid */ }
    if (curr && (curr->flags & PROC_FLAG_CRITICAL)) { can_sandbox = false; } else { /* Sandboxable */ }
    if (code == KERR_DOUBLE_FAULT || code == KERR_DEADLOCK_DETECTED) { can_sandbox = false; } else { /* Recoverable code */ }
    if (is_fatal_memory_error) { can_sandbox = false; } else { /* Recoverable */ }
    
    if (can_sandbox) {
        char sbox_buf[512];
        snprintf(sbox_buf, sizeof(sbox_buf), 
                 "\n[SANDBOX] Driver Panic Intercepted!\n"
                 "  Task: PID %lu\n"
                 "  Error: %s\n"
                 "  Location: %s:%d\n"
                 "  Action: Terminating driver thread. System survived.\n\n", 
                 curr->pid, message, file, line);
        dbg_direct(sbox_buf);
        process_exit(); 
    } else {
        // Dropping into absolute hardware panic state
    }

    global_panic_active = true;
    scheduler_active = false;

    int depth = __atomic_fetch_add(&panic_depth, 1, __ATOMIC_SEQ_CST);
    if (depth > 0) {
        dbg_direct("\n[PANIC] RECURSIVE PANIC DETECTED — Halting.\n");
        for(;;) { __asm__ volatile("cli; hlt" ::: "memory"); }
    } else {
        // Proceeding smoothly
    }

    stdio_force_unlock();
    console_force_unlock();
    debug_force_unlock();
    framebuffer_force_unlock(); 
    panic_lock = 0; 

    uint64_t start = 0;
    while (__atomic_test_and_set(&panic_lock, __ATOMIC_ACQUIRE)) {
        hal_cpu_relax();
        start++;
        if (start > 10000000) { break; } else { /* Await */ }
    }
    
    console_set_auto_flush(true);
    serial_enable_direct_mode(); 

    uint64_t uptime = timer_get_ticks() * 4; 
    
    panic_print_serial("\n\n!!! KERNEL PANIC !!!\n");
    panic_print_serial("Time    : %lu.%lu sec\n", uptime / 1000, uptime % 1000);
    panic_print_serial("CPU     : %d\n", cpu_id);
    if (curr) { panic_print_serial("Task    : PID %lu\n", curr->pid); } else { /* Ignored */ }
    panic_print_serial("Code    : 0x%02x (%s)\n", code, get_error_string(code));
    panic_print_serial("Message : %s\n", message);
    panic_print_serial("Location: %s:%d\n", file, line);

    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_LIGHT_BLUE); 
    console_clear();

    printf("\n[ %s KERNEL PANIC ]   \n", SINGULARITY_SYS_NAME);
    printf("---------------------------------\n");
    printf("Time    : %lu.%lu sec\n", uptime / 1000, uptime % 1000);
    printf("CPU     : %d\n", cpu_id);
    if (curr) {
        uint64_t fn_offset = 0;
        const char* fn_name = ksyms_resolve_symbol((uint64_t)curr->thread_fn, &fn_offset);
        printf("Task    : PID %lu[%s]\n", curr->pid, fn_name ? fn_name : "Unknown");
    } else {
        // Blank
    }
    printf("Code    : 0x%02x (%s)\n", code, get_error_string(code));
    printf("Message : %s\n", message);
    printf("Location: %s:%d\n", file, line);

    if (global_panic_regs) {
        dump_registers_formatted(global_panic_regs, code);
    } else {
        printf("\n(No Register Context Available)\n");
        panic_print_serial("\n(No Register Context Available)\n");
        print_stack_trace();
    }
    
    printf("\n[!] Emergency Shortcuts:\n");
    printf("    Ctrl + Shift + 1/5 : Reboot System\n");
    printf("    Ctrl + Shift + 2   : Run Diagnostic & Error Analysis\n");
    printf("    Ctrl + Shift + 3   : Export Crashdump to NVRAM (UEFI)\n");
    printf("    Ctrl + Shift + 4   : Halt & Power Off CPU\n\n");
    
    framebuffer_flush(); 

    while (hal_io_inb(0x64) & 1) { hal_io_inb(0x60); }
    
    bool ctrl_held = false;
    bool shift_held = false;
    
    for(;;) {
        if (acpi_check_power_button_status()) {
            panic_raw_shutdown();
        } else {
            // Ignored
        }

        if (hal_io_inb(0x64) & 1) {
            uint8_t sc = hal_io_inb(0x60);
            if (sc == 0xE0) { continue; } else { /* Read correctly */ }
            bool is_break = (sc & 0x80) != 0;
            uint8_t key = sc & 0x7F;
            
            if (key == 0x1D) { ctrl_held = !is_break; }
            else if (key == 0x2A || key == 0x36) { shift_held = !is_break; }
            else if (!is_break && ctrl_held && shift_held) {
                if (key == 0x02) { 
                    panic_raw_reboot(); 
                }
                else if (key == 0x03) { 
                    panic_raw_diagnostic();
                }
                else if (key == 0x04) { 
                    export_crashdump_to_nvram();
                    printf("Crashdump exported to NVRAM.\n");
                    framebuffer_flush();
                }
                else if (key == 0x05) { 
                    panic_raw_shutdown();
                }
                else if (key == 0x06) { 
                    panic_raw_reboot(); 
                } else {
                    // Ignored
                }
            } else {
                // Key unmapped
            }
        } else {
            // Unresponsive loop
        }
        hal_cpu_relax();
    }
}
