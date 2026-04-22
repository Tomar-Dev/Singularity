// system/power/power.c
#include "system/power/power.h"
#include "archs/cpu/cpu_hal.h"
#include "libc/stdio.h"
#include "system/console/console.h"
#include "system/process/process.h"

extern void disk_cache_flush_all();
extern void device_manager_shutdown();
extern void print_status(const char* prefix, const char* msg, const char* status);
extern void stdio_set_buffering(bool enabled);
extern void stdio_flush();
extern void serial_enable_direct_mode();
extern void framebuffer_flush();
extern bool uefi_available();
extern void uefi_reset_system(int type);
extern void acpi_power_off();

void print_shutdown(const char* msg) {
    console_set_color(CONSOLE_COLOR_LIGHT_CYAN, CONSOLE_COLOR_BLACK); 
    printf("[ Power ] ");
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK); 
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
    console_set_color(CONSOLE_COLOR_YELLOW, CONSOLE_COLOR_BLACK); 
    printf(" !!! INITIATING SYSTEM REBOOT !!! \n\n");
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);

    disk_cache_flush_all();
    device_manager_shutdown();

    hal_interrupts_disable();
    print_shutdown("Interrupts disabled");

    print_shutdown("System will reboot in 1 second...");
    hal_timer_delay_ms(1000); 
    
    uint8_t good = 0x02;
    while (good & 0x02) { good = hal_io_inb(0x64); }
    hal_io_outb(0x64, 0xFE);
    
    hal_io_outb(0xCF9, 0x06); 

    if (uefi_available()) { uefi_reset_system(1); } else { /* Standard */ }
    
    __asm__ volatile ("lidt (%0); int3" : : "r" (0));
    
    for(;;) { hal_cpu_halt(); }
}

void system_shutdown(const char* reason) {
    serial_enable_direct_mode();
    console_set_auto_flush(true); 
    stdio_set_buffering(false); 
    scheduler_active = false; 

    printf("\n");
    console_set_color(CONSOLE_COLOR_LIGHT_RED, CONSOLE_COLOR_BLACK); 
    printf(" !!! INITIATING SYSTEM SHUTDOWN !!! \n");
    
    console_set_color(CONSOLE_COLOR_LIGHT_CYAN, CONSOLE_COLOR_BLACK); 
    printf("\n Reason: ");
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK); 
    printf("%s\n\n", reason ? reason : "Unknown");

    disk_cache_flush_all();
    device_manager_shutdown();

    hal_interrupts_disable();
    print_shutdown("Interrupts disabled");

    print_shutdown("System will power off in 1 second...");
    hal_timer_delay_ms(1000); 
    
    acpi_power_off();
    
    hal_io_outw(0x604,  0x2000); 
    hal_io_outw(0x1804, 0x2000); 
    hal_io_outw(0xB004, 0x2000); 
    hal_io_outw(0x4004, 0x3400); 

    hal_io_outb(0x64, 0xFE);
    __asm__ volatile ("lidt (%0); int3" : : "r" (0));

    printf("\n Shutdown failed. It is safe to turn off manually.\n");
    stdio_flush();
    for(;;) { hal_cpu_halt(); }
}