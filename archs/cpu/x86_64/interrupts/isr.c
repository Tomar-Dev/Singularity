// archs/cpu/x86_64/interrupts/isr.c
#include "archs/cpu/x86_64/interrupts/isr.h"
#include "archs/cpu/x86_64/idt/idt.h"
#include "archs/cpu/cpu_hal.h"
#include "libc/stdio.h"
#include "drivers/video/framebuffer.h"
#include "system/console/console.h"
#include "drivers/apic/apic.h"
#include "kernel/debug.h"
#include "drivers/serial/serial.h"
#include "kernel/ksyms.h" 
#include "system/process/process.h" 

extern void set_panic_registers(registers_t* regs);
extern volatile bool global_panic_active;
extern uint8_t get_apic_id();
extern void kir_dispatch_irq(uint8_t vector); 
extern void panic_at(const char* file, int line, kernel_error_t code, const char* message);

isr_t interrupt_handlers[256];

extern void irq0();  extern void irq1();  extern void irq2();  extern void irq3();
extern void irq4();  extern void irq5();  extern void irq6();  extern void irq7();
extern void irq8();  extern void irq9();  extern void irq10(); extern void irq11();
extern void irq12(); extern void irq13(); extern void irq14(); extern void irq15();

void register_interrupt_handler(uint8_t n, isr_t handler) {
    if (handler) {
        interrupt_handlers[n] = handler;
    } else {
        interrupt_handlers[n] = 0;
    }
}

static void fpu_exception_handler(registers_t* regs) {
    if (!regs) return;

    bool from_user_mode = (regs->cs & 3) == 3;

    const char* ex_name = "Unknown";
    switch (regs->int_no) {
        case 6:  ex_name = "#UD Invalid Opcode (SSE/AVX not supported or FPU not initialized)"; break;
        case 7:  ex_name = "#NM Device Not Available (FPU state not saved)"; break;
        case 16: ex_name = "#MF x87 FPU Floating-Point Error"; break;
        case 19: ex_name = "#XM SIMD Floating-Point Exception"; break;
        default: ex_name = "Undefined FPU Fault"; break;
    }

    uint8_t cpu_id = get_cpu_id_fast();
    process_t* curr = current_process[cpu_id];
    bool is_sandboxed_driver = (!from_user_mode && curr && curr->pid != 0 && !(curr->flags & PROC_FLAG_CRITICAL));

    if (from_user_mode || is_sandboxed_driver) {
        // GÜVENLİK YAMASI: Süreç hemen ölmek yerine lekeli (TAINTED) olarak işaretlenir.
        curr->flags |= PROC_FLAG_TAINTED; 
        
        const char* mode_str = from_user_mode ? "User-Space" : "Sandboxed Driver";
        serial_printf("[KERNEL] %s process %lu TAINTED and killed by FPU Exception: %s at RIP: 0x%lx\n", 
                      mode_str, curr ? curr->pid : 0, ex_name, regs->rip);
        process_exit();
        return; 
    } else {
        // Çekirdek kritik alanı patladıysa panik
        if (global_panic_active) {
            dbg_direct("\n[FATAL] FPU Exception WHILE in panic state! (Double Panic)\n");
            for (;;) { __asm__ volatile("cli; hlt"); }
        } else {
            global_panic_active = true;

            char buf[256];
            snprintf(buf, sizeof(buf), "FPU/SSE Fault: %s", ex_name);
            
            if (regs->int_no == 6) {
                dbg_direct("[CPU] DIAGNOSIS: xsaveopt/xsave/fxsave may have been called without proper CR4.OSXSAVE / XCR0 initialization on this core.\n");
            } else {}

            set_panic_registers(regs);
            panic_at("isr.c", __LINE__, KERR_CPU_EX, buf);
        }
    }
}

void isr_handler(registers_t* regs) {
    if (!regs) return;

    if (regs->int_no == 8) {
        dbg_direct("\n[CPU] DOUBLE FAULT (#DF) - System halted.\n");
        // FIX: hal_cpu_halt NMI'da uyanabilir, bu nedenle sonsuz kilit atıldı.
        for (;;) { __asm__ volatile("cli; hlt"); }
    } else {}

    bool from_user_mode = (regs->cs & 3) == 3;

    if (global_panic_active && regs->int_no < 32) {
        char dp_buf[256];
        snprintf(dp_buf, sizeof(dp_buf), "\n\n!!![FATAL] DOUBLE PANIC !!!\nException %llu (ERR: 0x%llx) at RIP: 0x%llx occurred WHILE in panic state!\n", 
                 regs->int_no, regs->err_code, regs->rip);
        dbg_direct(dp_buf);
        
        uint64_t offset = 0;
        const char* sym = ksyms_resolve_symbol(regs->rip, &offset);
        if (sym) {
            snprintf(dp_buf, sizeof(dp_buf), "Location: <%s+%llu>\n", sym, offset);
            dbg_direct(dp_buf);
        } else {}
        
        dbg_direct("System gracefully halted to prevent infinite loop.\n");
        for (;;) { __asm__ volatile("cli; hlt"); }
    } else {}

    if (regs->int_no < 32) {
        set_panic_registers(regs);
    } else {}

    if (interrupt_handlers[regs->int_no] != 0) {
        isr_t handler = interrupt_handlers[regs->int_no];
        handler(regs);
        if (regs->int_no < 32) set_panic_registers(NULL);
        return;
    } else {
        uint8_t cpu_id = get_cpu_id_fast();
        process_t* curr = current_process[cpu_id];
        bool is_sandboxed_driver = (!from_user_mode && curr && curr->pid != 0 && !(curr->flags & PROC_FLAG_CRITICAL) && regs->int_no < 32);

        if (from_user_mode || is_sandboxed_driver) {
            // GÜVENLİK YAMASI: Process Tainting
            curr->flags |= PROC_FLAG_TAINTED;
            const char* mode_str = from_user_mode ? "User-Space" : "Sandboxed Driver";
            serial_printf("[KERNEL] %s process %lu TAINTED and killed by Unhandled Exception #%lu at RIP: 0x%lx\n", 
                          mode_str, curr ? curr->pid : 0, regs->int_no, regs->rip);
            process_exit();
            return;
        } else {
            char buf[128];
            snprintf(buf, sizeof(buf),
                     "Unhandled Exception #%lu (Error: 0x%lx) at RIP=0x%lx",
                     regs->int_no, regs->err_code, regs->rip);

            panic_at("isr.c", __LINE__, KERR_CPU_EX, buf);
        }
    }
}

void register_fpu_exception_handlers(void) {
    register_interrupt_handler(6,  fpu_exception_handler); 
    register_interrupt_handler(7,  fpu_exception_handler); 
    register_interrupt_handler(16, fpu_exception_handler); 
    register_interrupt_handler(19, fpu_exception_handler); 
}

void irq_handler(registers_t* regs) {
    if (!regs) return;

    if (is_apic_enabled()) {
        // GÜVENLİK YAMASI: Spurious IRQ Handling APIC uyumlu hale getirildi.
        if (regs->int_no == 0xFF) {
            return; // Spurious interrupt safely ignored
        } else {
            apic_send_eoi(); 
        }
    } else {
        // MİMARİ YAMASI: Legacy-Free ortamda PIC üzerinden IRQ gelmesi ölümcüldür.
        panic_at("isr.c", __LINE__, KERR_UNHANDLED_IRQ, "FATAL: Legacy PIC IRQ received in strictly APIC-only environment!");
        return;
    }

    kir_dispatch_irq(regs->int_no);

    if (interrupt_handlers[regs->int_no] != 0) {
        isr_t handler = interrupt_handlers[regs->int_no];
        handler(regs);
    } else {
        // Unhandled APIC IRQ
    }
    
    // GÜVENLİK YAMASI: Eski (Legacy 8259) PIC outb çağrıları tamamen silinmiştir.
}

static void pic_remap() {
    uint8_t a1, a2;
    a1 = hal_io_inb(0x21); a2 = hal_io_inb(0xA1);
    hal_io_outb(0x20, 0x11); hal_io_outb(0xA0, 0x11);
    hal_io_outb(0x21, 0x20); hal_io_outb(0xA1, 0x28);
    hal_io_outb(0x21, 0x04); hal_io_outb(0xA1, 0x02);
    hal_io_outb(0x21, 0x01); hal_io_outb(0xA1, 0x01);
    hal_io_outb(0x21, a1);   hal_io_outb(0xA1, a2);
}

void pic_disable() {
    hal_io_outb(0xA1, 0xFF);
    hal_io_outb(0x21, 0xFF);
}

void irq_install() {
    pic_remap();
    idt_set_entry(32, (uint64_t)irq0,  0x08, 0x8E);
    idt_set_entry(33, (uint64_t)irq1,  0x08, 0x8E);
    idt_set_entry(34, (uint64_t)irq2,  0x08, 0x8E);
    idt_set_entry(35, (uint64_t)irq3,  0x08, 0x8E);
    idt_set_entry(36, (uint64_t)irq4,  0x08, 0x8E);
    idt_set_entry(37, (uint64_t)irq5,  0x08, 0x8E);
    idt_set_entry(38, (uint64_t)irq6,  0x08, 0x8E);
    idt_set_entry(39, (uint64_t)irq7,  0x08, 0x8E);
    idt_set_entry(40, (uint64_t)irq8,  0x08, 0x8E);
    idt_set_entry(41, (uint64_t)irq9,  0x08, 0x8E);
    idt_set_entry(42, (uint64_t)irq10, 0x08, 0x8E);
    idt_set_entry(43, (uint64_t)irq11, 0x08, 0x8E);
    idt_set_entry(44, (uint64_t)irq12, 0x08, 0x8E);
    idt_set_entry(45, (uint64_t)irq13, 0x08, 0x8E);
    idt_set_entry(46, (uint64_t)irq14, 0x08, 0x8E);
    idt_set_entry(47, (uint64_t)irq15, 0x08, 0x8E);

    hal_io_outb(0x21, 0xF8);
    hal_io_outb(0xA1, 0xFF);
}