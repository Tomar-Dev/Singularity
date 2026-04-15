// system/power/power.c
#include "system/power/power.h"
#include "archs/cpu/cpu_hal.h"
#include "libc/stdio.h"
#include "drivers/serial/serial.h"
#include "drivers/acpi/acpi.h"
#include "archs/cpu/x86_64/interrupts/isr.h"
#include "drivers/video/framebuffer.h"
#include "system/console/console.h"
#include "drivers/uefi/uefi.h"
#include "system/process/process.h"
#include "drivers/timer/tsc.h" 

extern void disk_cache_flush_all();
extern void device_manager_shutdown();
extern void print_status(const char* prefix, const char* msg, const char* status);
extern void stdio_set_buffering(bool enabled);
extern void stdio_flush();

void print_shutdown(const char* msg) {
    vga_set_color(11, 0); 
    printf("[ Power ] ");
    vga_set_color(15, 0); 
    printf("%s\n", msg);
    framebuffer_flush();
    stdio_flush(); 
}

void system_reboot() {
    serial_enable_direct_mode();
    console_set_auto_flush(true); 
    stdio_set_buffering(false); 
    scheduler_active = false; 

    printf("\n");
    vga_set_color(14, 0); 
    printf(" !!! INITIATING SYSTEM REBOOT !!! \n\n");
    vga_set_color(15, 0);

    disk_cache_flush_all();
    device_manager_shutdown();

    hal_interrupts_disable();
    print_shutdown("Interrupts disabled");

    print_shutdown("System will reboot in 1 second...");
    tsc_delay_ms(1000); 
    
    uint8_t good = 0x02;
    while (good & 0x02) good = hal_io_inb(0x64);
    hal_io_outb(0x64, 0xFE);
    
    hal_io_outb(0xCF9, 0x06); 

    if (uefi_available()) uefi_reset_system(1);
    
    __asm__ volatile ("lidt (%0); int3" : : "r" (0));
    
    for(;;) hal_cpu_halt();
}

void system_shutdown(const char* reason) {
    serial_enable_direct_mode();
    console_set_auto_flush(true); 
    stdio_set_buffering(false); 
    scheduler_active = false; 

    printf("\n");
    vga_set_color(12, 0); 
    printf(" !!! INITIATING SYSTEM SHUTDOWN !!! \n");
    
    vga_set_color(11, 0); 
    printf("\n Reason: ");
    vga_set_color(15, 0); 
    printf("%s\n\n", reason ? reason : "Unknown");

    disk_cache_flush_all();
    device_manager_shutdown();

    hal_interrupts_disable();
    print_shutdown("Interrupts disabled");

    print_shutdown("System will power off in 1 second...");
    tsc_delay_ms(1000); 
    
    acpi_power_off();
    
    hal_io_outw(0x604,  0x2000); 
    hal_io_outw(0x1804, 0x2000); 
    hal_io_outw(0xB004, 0x2000); 
    hal_io_outw(0x4004, 0x3400); 

    hal_io_outb(0x64, 0xFE);
    __asm__ volatile ("lidt (%0); int3" : : "r" (0));

    printf("\n Shutdown failed. It is safe to turn off manually.\n");
    stdio_flush();
    for(;;) hal_cpu_halt();
}
