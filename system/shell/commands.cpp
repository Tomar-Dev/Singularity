// system/shell/commands.cpp
#include "system/shell/shell.hpp"
#include "system/shell/utils/sysinfo.hpp"
#include "system/shell/utils/torture.hpp"
#include "libc/stdio.h"
#include "libc/string.h"
#include "system/power/power.h"
#include "archs/cpu/x86_64/core/thermal.h"
#include "drivers/pci/pci.hpp"
#include "drivers/misc/speaker.h"
#include "system/gui/gui.h"
#include "system/graphics/graphics.h"
#include "archs/cpu/x86_64/core/cpuid.h"
#include "archs/memory/kheap.h"
#include "system/console/console.h"
#include "kernel/profiler.h"
#include "archs/cpu/x86_64/smp/smp.h"
#include "drivers/acpi/numa.h"
#include "system/process/process.h"
#include "drivers/apic/apic.h"
#include "drivers/acpi/acpi.h"
#include "system/debug/system_check.h"
#include "kernel/config.h"
#include "system/ffi/ffi.hpp"
#include "archs/kom/kom_aal.h"

extern "C" {
    void acpi_suspend();
    void beep(uint32_t freq, uint32_t duration_ms);
    void profiler_enable(bool enable);
    void profiler_print_report();
    void wdt_test_crash();
    void pcie_print_all(void);
    
    extern uint8_t num_cpus;
    void console_set_auto_flush(bool enabled);
    void console_clear();

    void rust_usb_print_ports(void);
    void i2c_dump(uint8_t device_addr);
}

const Shell::ShellCommand Shell::command_table[] = {
    {"help",        &Shell::cmd_help,        "List available commands"},
    {"clear",       &Shell::cmd_clear,       "Clear the screen"},
    {"ls",          &Shell::cmd_ls,          "List directory contents"},
    {"cat",         &Shell::cmd_cat,         "Display file contents"},
    {"cd",          &Shell::cmd_cd,          "Change current directory"},
    {"mkdir",       &Shell::cmd_mkdir,       "Create a new directory"},
    {"touch",       &Shell::cmd_touch,       "Create a new empty file"},
    {"write",       &Shell::cmd_write,       "Write text to a file"},
    {"mount",       &Shell::cmd_mount,       "Mount a filesystem manually"},
    {"automount",   &Shell::cmd_automount,   "Auto-mount all discovered volumes"},
    {"ramdisk",     &Shell::cmd_ramdisk,     "Create Volatile Storage (Ramdisk)"},
    {"disks",       &Shell::cmd_disks,       "List all physical storage devices"},
    {"parts",       &Shell::cmd_parts,       "List all disk partitions"},
    {"system",      &Shell::cmd_system,      "Display full system information"},
    {"systemcheck", &Shell::cmd_systemcheck, "Run kernel health diagnostics"},
    {"taskmgr",     &Shell::cmd_taskmgr,     "Show Task Manager"},
    {"torture",     &Shell::cmd_torture,     "Run system stability stress tests"},
    {"reboot",      &Shell::cmd_reboot,      "Restart the computer"},
    {"shutdown",    &Shell::cmd_shutdown,    "Power off the computer"},
    {"numa",        &Shell::cmd_numa,        "Display NUMA topology"},
    {"pcie",        &Shell::cmd_pcie,        "PCIe bus scan and enumeration"},
    {"pci",         &Shell::cmd_pcie,        "Alias for pcie"},
    {"usb",         &Shell::cmd_usb,         "Dump attached USB devices"},
    {"i2c",         &Shell::cmd_i2c,         "Dump I2C/SMBus EEPROM data"},
    {"fdisk",       &Shell::cmd_fdisk,       "Disk partition management"},
    {"mkfs",        &Shell::cmd_mkfs,        "Format a partition"},
    {"profile",     &Shell::cmd_profile,     "Kernel Profiler control"},
    {"reap",        &Shell::cmd_reap,        "Wake up Grim Reaper"},
    {"echo",        &Shell::cmd_echo,        "Print text to terminal"},
    {"log",         &Shell::cmd_log,         "Print startup log"},
    {"suspend",     &Shell::cmd_suspend,     "Suspend to RAM (S3)"},
    {"power",       &Shell::cmd_power,       "Power monitoring (unsupported)"},
    {"turbo",       &Shell::cmd_turbo,       "CPU Turbo Boost control"},
    {"beep",        &Shell::cmd_beep,        "Play PC Speaker beep"},
    {"fbtest",      &Shell::cmd_fbtest,      "Test Framebuffer rendering"},
    {"gui",         &Shell::cmd_gui,         "Start GUI Environment"},
    {"crash_stack", &Shell::cmd_crash_stack, "Trigger Stack Canary Panic"},
    {"wdt_test",    &Shell::cmd_wdt_test,    "Test Watchdog Reset"},
};

const int Shell::command_count = sizeof(Shell::command_table) / sizeof(ShellCommand);

int Shell::cmd_usb(const char* arg) { 
    (void)arg;
    console_set_auto_flush(false);
    rust_usb_print_ports();
    console_set_auto_flush(true);
    return 0;
}

int Shell::cmd_i2c(const char* arg) {
    if (!arg || arg[0] == '\0') {
        printf("Usage: i2c <hex_addr> (e.g., i2c 0x50 for RAM SPD)\n");
        return 1;
    } else {
        // Argument validated
    }

    uint64_t addr = 0;
    const char* p = arg;
    if (p[0] == '0' && p[1] == 'x') { p += 2; } else { /* Clean parameter */ }

    while (*p) {
        addr <<= 4;
        if (*p >= '0' && *p <= '9') { addr |= (*p - '0'); }
        else if (*p >= 'a' && *p <= 'f') { addr |= (*p - 'a' + 10); }
        else if (*p >= 'A' && *p <= 'F') { addr |= (*p - 'A' + 10); }
        else { break; }
        p++;
    }

    console_set_auto_flush(false);
    i2c_dump((uint8_t)addr);
    console_set_auto_flush(true);
    return 0;
}

int Shell::cmd_fbtest(const char* arg) {
    (void)arg;
    printf("Testing ONS GPU Device: /devices/fb0...\n");

    KObject* fb_node = ons_resolve("/devices/fb0");
    if (!fb_node) {
        printf("Error: /devices/fb0 not found in ONS!\n");
        return 1;
    } else {
        // Exists inside KOM memory space
    }

    if (fb_node->type != KObjectType::GPU_DEVICE) {
        printf("Error: fb0 is not a GPU_DEVICE.\n");
        kobject_unref(fb_node);
        return 1;
    } else {
        // Strict boundary check passed
    }

    KDevice* dev = (KDevice*)fb_node;

    struct fb_var_screeninfo vinfo;
    if (dev->query(KDQ_GPU_VRAM_SIZE, &vinfo, sizeof(vinfo)) != KOM_OK) {
        if (((Device*)dev)->ioctl(FBIOGET_VSCREENINFO, &vinfo) != 0) {
            printf("Error: Could not get screen info.\n");
            kobject_unref(fb_node);
            return 1;
        } else {
            // Derived correctly
        }
    } else {
        // Query accepted natively
    }

    printf("Screen Info: %dx%d, %d bpp\n", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);
    printf("Drawing SMP-Themed test pattern via Direct KDevice Write...\n");

    uint32_t w = 250, h = 250;
    uint32_t* buf = (uint32_t*)kmalloc(w * h * 4);
    if (!buf) { kobject_unref(fb_node); return 1; } else { /* Memory acquired */ }

    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint8_t r = (x * 255) / w;
            uint8_t b = (y * 255) / h;
            uint8_t g = 150 - (r/2) + (b/2);
            buf[y * w + x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
        }
    }

    uint32_t pitch = vinfo.xres * (vinfo.bits_per_pixel / 8);
    uint32_t start_x = (vinfo.xres - w) / 2;
    uint32_t start_y = (vinfo.yres - h) / 2;

    for (uint32_t y = 0; y < h; y++) {
        uint64_t offset = ((start_y + y) * pitch) + (start_x * 4);
        ((Device*)dev)->writeOffset(offset, w * 4, (uint8_t*)(buf + (y * w)));
    }

    ((Device*)dev)->ioctl(FBIO_WAITFORVSYNC, nullptr);
    kfree(buf); kobject_unref(fb_node);

    console_set_color(CONSOLE_COLOR_LIGHT_GREEN, CONSOLE_COLOR_BLACK);
    printf("Test pattern successfully drawn via ONS Device!\n");
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    return 0;
}

int Shell::cmd_log(const char* arg) {
    if (!arg) { return 0; } else { /* Process normally */ }
    const char* msg_ptr = arg;
    const char* p = arg;
    while (*p && *p != ' ') { p++; }
    if (*p == ' ') { msg_ptr = p + 1; } else { /* Use base arg */ }
    console_set_color(CONSOLE_COLOR_DARK_GREY, CONSOLE_COLOR_BLACK); printf("[ STARTUP ] ");
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    printf("%s\n", msg_ptr);
    return 0;
}

int Shell::cmd_systemcheck(const char* arg) { (void)arg; perform_system_check(); return 0; }
int Shell::cmd_reboot(const char* arg) { (void)arg; system_reboot(); return 0; }
int Shell::cmd_shutdown(const char* arg) { (void)arg; system_shutdown("User Command (Shell)"); return 0; }
int Shell::cmd_pcie(const char* arg) { (void)arg; console_set_auto_flush(false); pcie_print_all(); console_set_auto_flush(true); return 0; }

int Shell::cmd_system(const char* arg) {
    console_set_auto_flush(false);

    if (!arg || arg[0] == '\0') { show_system_info_all(); }
    else if (strcmp(arg, "mem") == 0) { kheap_print_stats(); show_memory_map(); }
    else if (strcmp(arg, "tasks") == 0) { show_task_list(); }
    else if (strcmp(arg, "all") == 0) { show_system_info_all(); }
    else if (strcmp(arg, "ffi") == 0) { SingularityFFI::print_ffi_stats(); }
    else {
        printf("Unknown system command: %s\n", arg);
        console_set_auto_flush(true);
        return 1;
    }

    console_set_auto_flush(true);
    return 0;
}

int Shell::cmd_taskmgr(const char* arg) {
    (void)arg;
    console_set_auto_flush(false);
    show_task_manager();
    console_set_auto_flush(true);
    return 0;
}

int Shell::cmd_power(const char* arg) { (void)arg; printf("Power monitoring not supported directly.\n"); return 0; }
int Shell::cmd_turbo(const char* arg) { (void)arg; printf("Usage: turbo <on/off>\n"); return 0; }
int Shell::cmd_beep(const char* arg) { (void)arg; beep(1000, 200); printf("Beep!\n"); return 0; }

#ifdef __clang__
#pragma clang optimize off
#else
#pragma GCC push_options
#pragma GCC optimize ("O0")
#endif
int Shell::cmd_crash_stack(const char* arg) {
    (void)arg;
    printf("[TEST] Smashing stack canary to trigger Panic...\n");
    volatile char buf[8];
    for (volatile int i = 0; i < 64; i++) {
        buf[i] = 0xAA;
    }
    return 0;
}
#ifdef __clang__
#pragma clang optimize on
#else
#pragma GCC pop_options
#endif

int Shell::cmd_suspend(const char* arg) { (void)arg; printf("Suspending to RAM (S3)...\n"); acpi_suspend(); return 0; }
int Shell::cmd_gui(const char* arg) { (void)arg; gui_mode_start(); return 0; }

int Shell::cmd_profile(const char* arg) {
    if (!arg || arg[0] == '\0') { printf("Usage: profile <on/off/show>\n"); return 1; } else { /* Bounds validated */ }
    if (strcmp(arg, "on") == 0) { profiler_enable(true); }
    else if (strcmp(arg, "off") == 0) { profiler_enable(false); }
    else if (strcmp(arg, "show") == 0) { profiler_print_report(); }
    else { printf("Unknown option.\n"); return 1; }
    return 0;
}

int Shell::cmd_wdt_test(const char* arg) {
    (void)arg;
    printf("Freezing system to test Watchdog Timer...\nSystem should reboot in ~3 seconds.\n");
    wdt_test_crash();
    return 0;
}

int Shell::cmd_numa(const char* arg) { (void)arg; numa_print_topology(); return 0; }

int Shell::cmd_help(const char* arg) {
    (void)arg;
    console_set_color(CONSOLE_COLOR_LIGHT_CYAN, CONSOLE_COLOR_BLACK); printf("\n========== ");
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK); printf("AVAILABLE COMMANDS");
    console_set_color(CONSOLE_COLOR_LIGHT_CYAN, CONSOLE_COLOR_BLACK); printf(" ==========\n");
    console_set_color(CONSOLE_COLOR_DARK_GREY, CONSOLE_COLOR_BLACK); printf("----------------------------------------\n");
    console_set_color(CONSOLE_COLOR_LIGHT_GREY, CONSOLE_COLOR_BLACK);

    for (int i = 0; i < command_count; i++) {
        if (kconfig.lockdown && (strcmp(command_table[i].name, "fdisk") == 0 || strcmp(command_table[i].name, "mkfs") == 0)) {
            console_set_color(CONSOLE_COLOR_DARK_GREY, CONSOLE_COLOR_BLACK);
            printf("%-12s : %s (Locked)\n", command_table[i].name, command_table[i].description);
            console_set_color(CONSOLE_COLOR_LIGHT_GREY, CONSOLE_COLOR_BLACK);
        } else {
            printf("%-12s : %s\n", command_table[i].name, command_table[i].description);
        }
    }

    console_set_color(CONSOLE_COLOR_LIGHT_CYAN, CONSOLE_COLOR_BLACK); printf("========================================\n"); console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    return 0;
}

int Shell::cmd_clear(const char* arg) { (void)arg; console_clear(); return 0; }

int Shell::cmd_echo(const char* arg) {
    if (arg) { printf("%s\n", arg); } else { printf("\n"); }
    return 0;
}

int Shell::cmd_torture(const char* arg) { (void)arg; run_torture_test(); return 0; }

int Shell::cmd_reap(const char* arg) { 
    (void)arg;
    printf("Signaling Grim Reaper for process cleanup...\n"); 
    reaper_invoke(); 
    printf("Signal sent.\n"); 
    return 0; 
}

int Shell::dispatchCommand(const char* cmd, const char* arg) {
    if (!cmd || cmd[0] == '\0') { return 0; } else { /* Valid */ }
    
    for (int i = 0; i < command_count; i++) {
        if (strcmp(cmd, command_table[i].name) == 0) {
            return (this->*(command_table[i].handler))(arg);
        } else {
            // Not this index
        }
    }
    
    printf("Unknown command: '%s'. Type 'help' for a list of commands.\n", cmd);
    return 1;
}
