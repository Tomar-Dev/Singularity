// kernel/kernel.c
#include "drivers/video/framebuffer.h"
#include "system/console/console.h"
#include "archs/cpu/x86_64/gdt/gdt.h"
#include "archs/cpu/x86_64/idt/idt.h"
#include "archs/cpu/x86_64/interrupts/isr.h"
#include "archs/cpu/cpu_hal.h"
#include "archs/cpu/x86_64/core/cpuid.h"
#include "archs/cpu/x86_64/core/fpu.h"
#include "archs/cpu/x86_64/core/multiboot.h"
#include "archs/memory/pmm.h" 
#include "archs/cpu/x86_64/memory/paging.h"
#include "archs/memory/kheap.h"
#include "libc/string.h"
#include "libc/stdio.h"
#include "kernel/debug.h"
#include "kernel/config.h"
#include "archs/cpu/x86_64/context/tss.h"
#include "system/process/process.h"
#include "system/power/power.h"
#include "system/security/stack_protector.h"
#include "archs/cpu/x86_64/core/thermal.h"
#include "system/device/device.h"
#include "system/disk/gpt.h"
#include "archs/cpu/x86_64/smp/smp.h"
#include "system/disk/cache.hpp"
#include "archs/cpu/x86_64/sync/spinlock.h"
#include "system/process/priority.h"
#include "system/security/rng.h"
#include "drivers/uefi/uefi.h"
#include "kernel/profiler.h"
#include "archs/cpu/x86_64/syscall/syscall.h"
#include "system/init/service.hpp"
#include "system/process/reaper.hpp"
#include "archs/kom/kom_aal.h"
#include <stdbool.h>

const char* kernel_build_date = __DATE__ " " __TIME__;

extern uint64_t end;

extern void tar_inflater_init(void* addr, uint32_t size);
extern void setup_system_info_nodes(void);

extern void init_pat();
extern void rtc_init();
extern void init_speaker();
extern void init_ahci_early();
extern void init_ahci_late();
extern void init_nvme_early(); 
extern int  init_nvme_late();  
extern void init_virtio();
extern void init_intel_hda();
extern void init_string_optimization();
extern void detect_cpu_topology();
extern void vmm_init_manager();
extern void hardware_integrity_check();
extern void shell_init_c();
extern void shell_update_c();
extern void shell_run_startup_c();
extern void init_wdt();
extern void rtc_get_time(rtc_time_t* time);
extern void service_signal_finished(const char* name);
extern void init_global_constructors();
extern void init_smbus(); 
extern void rust_usb_init(); 
extern void kir_init_c(); 

#include "system/ffi/ffi.h"
#define MULTIBOOT_STORAGE_SIZE 32768
static uint8_t multiboot_storage[MULTIBOOT_STORAGE_SIZE];
static struct multiboot_tag* safe_multiboot_ptr = NULL;

volatile int  g_input_system_ready = 0;
volatile bool g_audio_ready        = false;

uint64_t kernel_boot_start_tsc = 0;

void show_system_info() {
    console_set_color(CONSOLE_COLOR_LIGHT_CYAN, CONSOLE_COLOR_BLACK);
    printf("\n===================================\n");
    printf("     %s %s (%s)      \n", SINGULARITY_SYS_NAME, SINGULARITY_SYS_VER, SINGULARITY_SYS_ARCH);
    printf("===================================\n\n");
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
}

void print_status(const char* prefix, const char* msg, const char* status) {
    uint64_t flags = stdio_acquire_lock();

    char serial_buf[256];
    const char* s_tag = "[ INFO ]";
    if      (strcmp(status, "OK")   == 0) { s_tag = "[  OK  ]"; }
    else if (strcmp(status, "FAIL") == 0) { s_tag = "[ FAIL ]"; } else { /* Neutral */ }

    snprintf(serial_buf, sizeof(serial_buf), "%s %s %s\n", s_tag, prefix, msg);
    serial_write(serial_buf);

    if (strcmp(status, "OK") == 0) {
        console_set_color(CONSOLE_COLOR_LIGHT_GREEN, CONSOLE_COLOR_BLACK);
        console_write("[  OK  ] ");
    } else if (strcmp(status, "ERR") == 0 || strcmp(status, "FAIL") == 0) {
        console_set_color(CONSOLE_COLOR_LIGHT_RED, CONSOLE_COLOR_BLACK);
        console_write("[ FAIL ] ");
    } else {
        console_set_color(CONSOLE_COLOR_LIGHT_GREY, CONSOLE_COLOR_BLACK);
        console_write("[ INFO ] ");
    }

    console_set_color(CONSOLE_COLOR_DARK_GREY, CONSOLE_COLOR_BLACK);
    console_write(prefix);
    console_write(" ");

    if (strcmp(status, "INFO") == 0) {
        console_set_color(CONSOLE_COLOR_LIGHT_GREY, CONSOLE_COLOR_BLACK);
    } else {
        console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    }
    console_write(msg);
    console_write("\n");

    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    if (!scheduler_active) { framebuffer_flush(); } else { /* Maintained by daemon */ }

    stdio_release_lock(flags);
}

void print_section_header(const char* title) {
    static uint64_t last_tsc = 0;
    if (last_tsc == 0) { last_tsc = kernel_boot_start_tsc; } else { /* Evaluated */ }
    
    uint64_t current_tsc = rdtsc_ordered();
    uint64_t freq = get_tsc_freq();
    if (freq == 0) { freq = 2000000000ULL; } else { /* Synced clock */ }
    
    uint64_t diff_ms = ((current_tsc - last_tsc) * 1000) / freq;
    uint64_t total_ms = ((current_tsc - kernel_boot_start_tsc) * 1000) / freq;
    
    last_tsc = current_tsc;

    uint64_t flags = stdio_acquire_lock();
    console_set_color(CONSOLE_COLOR_YELLOW, CONSOLE_COLOR_BLACK);
    
    char buf[128];
    snprintf(buf, sizeof(buf), "\n>>> %-25s [ +%lu ms | %lu ms ] <<<\n", title, diff_ms, total_ms);
    
    serial_write(buf);
    console_write(buf);
    
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    stdio_release_lock(flags);
}

void prepare_ps2_for_ioapic() {
    hal_io_outb(0x64, 0xAD);
    hal_io_outb(0x64, 0xA7);
    while (hal_io_inb(0x64) & 1) { hal_io_inb(0x60); }
}

void enable_ps2_ports() {
    hal_io_outb(0x64, 0xAE);
    hal_io_outb(0x64, 0xA8);
}

void shell_task_entry() {
    console_set_auto_flush(true);
    framebuffer_flush();
    while (1) {
        shell_update_c();
        task_sleep(1);
    }
}

void memory_monitor_task() {
    while (1) {
        task_sleep(5000 / 4);
        if (pmm_is_low_memory()) {
            serial_write("[MEM] Low Memory detected! Triggering Reaper...\n");
            reaper_invoke();
            disk_cache_flush_all();
        } else {
            // Memory stable
        }
    }
}

void singd_monitor_entry() {
    service_monitor();
}

__attribute__((used))
int is_input_subsystem_ready() {
    return g_input_system_ready;
}

void gpf_handler(registers_t* regs) {
    (void)regs;
    panic_at("CPU", 0, KERR_GP_FAULT, "General Protection Fault (#GP)");
}

void svc_audio() { init_intel_hda(); service_signal_finished("Audio"); }
void svc_usb() { rust_usb_init(); service_signal_finished("USB"); }
void svc_speaker() { init_speaker(); service_signal_finished("Speaker"); }

__attribute__((no_stack_protector))
void kmain(void* multiboot_structure_addr) {
    kernel_boot_start_tsc = rdtsc_ordered();
    stdio_set_buffering(false);
    init_serial_early();
    serial_printf("\n\n[BOOT] %s %s Entry...\n", SINGULARITY_SYS_NAME, SINGULARITY_SYS_VER);

    gdt_install();
    idt_install();
    register_fpu_exception_handlers();
    register_interrupt_handler(14, page_fault_handler);
    register_interrupt_handler(13, gpf_handler);

    detect_cpu();
    init_rng();
    init_fpu();
    init_string_optimization();
    init_stack_protector();

    if (multiboot_structure_addr == NULL) { PANIC("Multiboot info structure is NULL!"); } else { /* Intact */ }

    uint32_t mb_size = *(uint32_t*)multiboot_structure_addr;
    if (mb_size > MULTIBOOT_STORAGE_SIZE) { mb_size = MULTIBOOT_STORAGE_SIZE; } else { /* Size validated */ }

    memcpy(multiboot_storage, multiboot_structure_addr, mb_size);
    safe_multiboot_ptr = (struct multiboot_tag*)(multiboot_storage + 8);

    const char* boot_cmdline = "";
    struct multiboot_tag* search_tag;
    for (search_tag = safe_multiboot_ptr; 
         search_tag->type != MULTIBOOT_TAG_TYPE_END; 
         search_tag = (struct multiboot_tag*)((uint8_t*)search_tag + ((search_tag->size + 7) & ~7))) 
    {
        if (search_tag->type == MULTIBOOT_TAG_TYPE_CMDLINE) {
            struct multiboot_tag_string* cmd_tag = (struct multiboot_tag_string*)search_tag;
            boot_cmdline = cmd_tag->string;
            break;
        } else {
            // Keep searching
        }
    }
    
    config_init(boot_cmdline);

    pmm_init(multiboot_structure_addr, &end);
    init_paging();
    pmm_map_remaining_memory();
    init_pat();
    vmm_init_manager();
    init_kheap();
    
    kom_init();
    ons_init(); 
    kir_init_c();

    init_global_constructors();
    
    init_uefi(multiboot_structure_addr);
    init_framebuffer(multiboot_structure_addr);
    console_init();
    
    console_set_auto_flush(true); 

    show_system_info();

    print_section_header("SYSTEM CORE STARTING");
    print_status("[ BOOT ]", "Multiboot Info Copied",           "OK");
    print_status("[ CORE ]", "GDT & IDT Active",                "OK");
    print_status("[ CFG  ]", "Cmdline Registry Parsed",         "OK");
    print_status("[ SEC  ]", kconfig.lockdown ? "Device Lockdown ENABLED" : "Device Lockdown DISABLED (Risky!)", kconfig.lockdown ? "OK" : "WARN");
    print_status("[ MEM  ]", "Physical Memory Manager",         "OK");
    print_status("[ MEM  ]", "Virtual Memory Manager",          "OK");
    print_status("[ MEM  ]", "Slab Heap Allocator",             "OK");
    print_status("[ KOM  ]", "Kernel Object Model & Handle Table", "OK");
    print_status("[ ONS  ]", "Object Namespace Root Directory", "OK");
    print_status("[ SEC  ]", "RNG & Stack Protector",           "OK");
    if (uefi_available()) { print_status("[ UEFI ]", "Runtime Services", "OK"); } else { /* Deprecated BIOS Mode */ }
    hardware_integrity_check();

    print_section_header("BUS/TIME STARTING");
    init_acpi(multiboot_structure_addr);
    print_status("[ ACPI ]", "Tables Parsed (RSDP)",        "OK");
    
    init_tsc();
    print_status("[ TIME ]", "TSC + PIT",                   "OK");
    rtc_init();
    init_syscalls();
    print_status("[ CORE ]", "Fast Syscall (MSR) Enabled",  "OK");

    init_ahci_early(); 
    init_nvme_early(); 

    print_section_header("STORAGE STARTING");
    disk_cache_init();      
    device_manager_init();  
    
    init_virtual_devices(); 
    init_smbios(multiboot_structure_addr);
    init_smbus();
    
    init_ahci_late();       print_status("[ DISK ]", "AHCI Driver (SATA)",               "OK");
    if (init_nvme_late()) { print_status("[ DISK ]", "NVMe Driver (PCIe SSD)",           "OK"); } else { /* NVMe absent */ }
    init_virtio();          

    struct multiboot_tag* f_tag;
    uint8_t* ptr = (uint8_t*)safe_multiboot_ptr;
    for (f_tag = (struct multiboot_tag*)ptr;
         f_tag->type != 0;
         f_tag = (struct multiboot_tag*)((uint8_t*)f_tag + ((f_tag->size + 7) & ~7)))
    {
        if ((uint64_t)f_tag >= (uint64_t)multiboot_storage + MULTIBOOT_STORAGE_SIZE) { break; } else { /* Array bounded */ }
        if (f_tag->type == 3) {
            struct multiboot_tag_module* mod = (struct multiboot_tag_module*)f_tag;
            tar_inflater_init((void*)(uint64_t)mod->mod_start, (uint32_t)(mod->mod_end - mod->mod_start));
        } else {
            // Not a module tag
        }
    }
    
    setup_system_info_nodes();

    print_section_header("SMP & SCHEDULER STARTING");
    void* kernel_stack = kmalloc(16384);
    if (kernel_stack) {
        tss_install(0, (uint64_t)kernel_stack + 16384);
    } else {
        PANIC("Failed to allocate Kernel Stack");
    }

    init_multitasking();

    print_status("[ CPU  ]", "CPU Feature Detection",         "OK");
    print_status("[ CPU  ]", "Instruction Set Optimization",  "OK");
    detect_cpu_topology();
    print_status("[ CPU  ]", "Topology Awareness", "OK");

    if (is_apic_enabled() || get_apic_id() != 0xFF) {
        init_apic_acpi();
        print_status("[ APIC ]", "Local APIC Controller", "OK");
        register_interrupt_handler(32, timer_handler);
        apic_timer_init(250);
        hal_interrupts_disable();
        pic_disable();
        
        prepare_ps2_for_ioapic();
        init_ioapic();
        ioapic_set_entry(acpi_remap_irq(4), 36);
        init_serial_late();
        ioapic_set_entry(acpi_remap_irq(1),  33);
        ioapic_set_entry(acpi_remap_irq(12), 44);
        ioapic_set_entry(acpi_remap_irq(9),  48);
        apic_finalize();
        enable_ps2_ports();
        print_status("[ IRQ  ]", "Full APIC Mode (IO-APIC)", "OK");
        
        init_keyboard();
        init_mouse();
        g_input_system_ready = 1;
        print_status("[ INPUT]", "PS/2 Keyboard & Mouse Initialized", "OK");

        init_smp();
        print_status("[ SMP  ]", "Multi-Core Activation", "OK");
    } else {
        PANIC("Legacy PIC Mode not supported (APIC Required)");
    }

    profiler_init();

    for (int i = 0; i < 16; i++) {
        unmap_page(i * 4096);
    }
    print_status("[ SEC  ]", "Absolute NULL Protection (64KB Unmapped)", "OK");

    paging_enable_write_protect();
    paging_protect_kernel();

    init_wdt();
    print_status("[ WDT  ]", "iTCO Watchdog Timer", "OK");

    init_reaper();
    print_status("[ TASK ]", "Grim Reaper (Incremental GC)", "OK");

    print_section_header("SERVICE MANAGER");
    service_manager_init();
    service_register("MemoryMonitor", memory_monitor_task, SERVICE_TYPE_DAEMON,  RESTART_ALWAYS);
    service_register("FFI_Logger",    ffi_logger_task,     SERVICE_TYPE_DAEMON,  RESTART_ALWAYS);
    service_register("Audio",         svc_audio,           SERVICE_TYPE_ONESHOT, RESTART_ON_FAILURE);
    service_register("USB",           svc_usb,             SERVICE_TYPE_ONESHOT, RESTART_ON_FAILURE);
    service_register("Speaker",       svc_speaker,         SERVICE_TYPE_ONESHOT, RESTART_NEVER);

    service_start_all();
    create_kernel_task(singd_monitor_entry);

    enable_scheduler();
    hal_interrupts_enable();

    printf("\nWaiting for hardware initialization...\n");
    service_wait_oneshots();
    signal_reaper();
    printf("\n");

    console_set_color(CONSOLE_COLOR_LIGHT_GREEN, CONSOLE_COLOR_BLACK);
    printf("System Initialized Successfully!\n");
    
    rtc_time_t t;
    rtc_get_time(&t);
    console_set_color(CONSOLE_COLOR_LIGHT_GREY, CONSOLE_COLOR_BLACK);
    printf("Time: %02d:%02d\n",           t.hour, t.minute);
    printf("Date: %02d.%02d.%04d\n",      t.day,  t.month, t.year);

    uint64_t end_tsc = rdtsc_ordered();
    uint64_t diff    = end_tsc - kernel_boot_start_tsc;
    uint64_t freq    = get_tsc_freq();
    if (freq == 0) { freq = 2000000000ULL; } else { /* Synced */ }
    uint64_t ms      = (diff * 1000) / freq;

    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    printf("Total Kernel Boot Time: %lu ms (Cycles: %lu)\n", ms, diff);

    console_set_auto_flush(false);

    shell_init_c();          
    shell_run_startup_c();   
    create_kernel_task_prio(shell_task_entry, PRIO_HIGH); 

    for (;;) {
        hal_cpu_halt();
    }
}
