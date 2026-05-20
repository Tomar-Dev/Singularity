// system/shell/commands.cpp
#include "system/shell/shell.hpp"
#include "system/shell/utils/sysinfo.hpp"
#include "system/shell/utils/torture.hpp"
#include "libc/stdio.h"
#include "libc/string.h"
#include "system/power/power.h"
#include "archs/cpu/x86_64/core/thermal.h"
#include "drivers/pci/pci.hpp"
#include "system/gui/gui.h"
#include "system/graphics/graphics.h"
#include "archs/cpu/x86_64/core/cpuid.h"
#include "memory/kheap.h"
#include "system/console/console.h"
#include "kernel/profiler.h"
#include "archs/cpu/x86_64/smp/smp.h"
#include "archs/cpu/x86_64/acpi/numa.h"
#include "system/process/process.h"
#include "archs/cpu/x86_64/apic/apic.h"
#include "archs/cpu/x86_64/acpi/acpi.h"
#include "system/debug/system_check.h"
#include "kernel/config.h"
#include "system/ffi/ffi.hpp"
#include "archs/kom/kom_aal.h"
#include "archs/memory/pmm.h" 

extern "C" {
    void acpi_suspend();
    void profiler_enable(bool enable);
    void profiler_print_report();
    void wdt_test_crash();
    void pcie_print_all(void);
    
    extern uint8_t num_cpus;
    void console_set_auto_flush(bool enabled);
    void console_clear();

    void rust_usb_print_ports(void);
    void i2c_dump(uint8_t device_addr);
    void timer_sleep(uint64_t ticks);
    
    int hw_watchpoint_set(uint64_t addr, int size, int type);
    void hw_watchpoint_clear(int index);
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
    {"disks",       &Shell::cmd_disks,       "List all storage devices and partitions"},
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
    {"fbtest",      &Shell::cmd_fbtest,      "Test Framebuffer rendering"},
    {"gui",         &Shell::cmd_gui,         "Start GUI Environment"},
    {"crash_stack", &Shell::cmd_crash_stack, "Trigger Stack Canary Panic"},
    {"wdt_test",    &Shell::cmd_wdt_test,    "Test Watchdog Reset"},
    {"ipc",         &Shell::cmd_ipc,         "Test KOM Channel IPC (Ping-Pong)"},
    {"watch",       &Shell::cmd_watch,       "Set Hardware Watchpoint (DRx)"},
    {"unwatch",     &Shell::cmd_unwatch,     "Clear Hardware Watchpoint"},
    {"watchtest",   &Shell::cmd_watchtest,   "Test Hardware Watchpoint Exception"},
    {"porttest",    &Shell::cmd_porttest,    "Test KPort I/O Multiplexing"},
    {"demandtest",  &Shell::cmd_demandtest,  "Test Demand Paging (Lazy Allocation)"},
};

const int Shell::command_count = sizeof(Shell::command_table) / sizeof(ShellCommand);

int Shell::cmd_demandtest(const char* arg) {
    (void)arg;
    printf("Testing Demand Paging (Lazy Allocation)...\n");
    
    uint64_t before_ram = pmm_get_used_memory();
    printf("RAM Used Before: %lu MB\n", before_ram / (1024 * 1024));
    
    size_t size = 100 * 1024 * 1024;
    void* ptr = kmalloc(size);
    if (!ptr) {
        printf("OOM!\n");
        return 1;
    }
    
    uint64_t after_alloc_ram = pmm_get_used_memory();
    printf("RAM Used After Virtual Alloc (Should be same): %lu MB\n", after_alloc_ram / (1024 * 1024));
    
    uint8_t* p8 = (uint8_t*)ptr;
    for (int i = 0; i < 10; i++) {
        p8[i * 4096] = 0xAA; 
    }
    
    uint64_t after_touch_ram = pmm_get_used_memory();
    printf("RAM Used After Touching 10 Pages: %lu MB\n", after_touch_ram / (1024 * 1024));
    
    kfree(ptr);
    
    uint64_t after_free_ram = pmm_get_used_memory();
    printf("RAM Used After Free: %lu MB\n", after_free_ram / (1024 * 1024));
    
    return 0;
}

int Shell::cmd_watchtest(const char* arg) {
    (void)arg;
    printf("Allocating a dummy variable...\n");
    uint64_t* dummy = (uint64_t*)kmalloc(sizeof(uint64_t));
    if (!dummy) {
        printf("OOM!\n");
        return 1;
    }
    *dummy = 0;

    printf("Dummy variable allocated at: 0x%lx\n", (uint64_t)dummy);
    printf("Setting Hardware Watchpoint (Write, 8 bytes)...\n");
    
    int idx = hw_watchpoint_set((uint64_t)dummy, 8, 1); 
    if (idx == -1) {
        printf("Failed to set watchpoint.\n");
        kfree(dummy);
        return 1;
    }

    printf("Writing to dummy variable... (This should trigger a #DB warning!)\n");
    
    *dummy = 0xDEADBEEF;

    printf("Write completed. Value is now: 0x%lx\n", *dummy);
    
    printf("Clearing watchpoint...\n");
    hw_watchpoint_clear(idx);
    kfree(dummy);
    
    return 0;
}

static uint64_t global_port_handle = 0;

static void port_worker_1() {
    timer_sleep(500); 
    kom_port_packet_t pkt = { .key = 101, .type = 1, .status = 0 };
    kom_port_queue(global_port_handle, &pkt);
    process_exit();
}

static void port_worker_2() {
    timer_sleep(250); 
    kom_port_packet_t pkt = { .key = 102, .type = 1, .status = 0 };
    kom_port_queue(global_port_handle, &pkt);
    process_exit();
}

static void port_worker_3() {
    timer_sleep(750); 
    kom_port_packet_t pkt = { .key = 103, .type = 1, .status = 0 };
    kom_port_queue(global_port_handle, &pkt);
    process_exit();
}

int Shell::cmd_porttest(const char* arg) {
    (void)arg;
    printf("Testing KPort (Zircon-Style I/O Multiplexing)...\n");

    int err = kom_port_create(&global_port_handle);
    if (err != KOM_OK) {
        printf("Failed to create KPort (Err: %d)\n", err);
        return 1;
    }

    printf("KPort created. Handle: %lu\n", global_port_handle);
    printf("Spawning 3 worker threads with different sleep delays...\n");

    create_kernel_task(port_worker_1);
    create_kernel_task(port_worker_2);
    create_kernel_task(port_worker_3);

    printf("Waiting for events on KPort...\n");

    for (int i = 0; i < 3; i++) {
        kom_port_packet_t pkt;
        err = kom_port_wait(global_port_handle, &pkt);
        if (err == KOM_OK) {
            console_set_color(CONSOLE_COLOR_LIGHT_GREEN, CONSOLE_COLOR_BLACK);
            printf("Event Received! Key: %lu, Type: %u\n", pkt.key, pkt.type);
            console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
        } else {
            printf("Port wait failed (Err: %d)\n", err);
        }
    }

    printf("All events received. Closing KPort.\n");
    kom_close(global_port_handle);
    return 0;
}

int Shell::cmd_watch(const char* arg) {
    if (!arg || arg[0] == '\0') {
        printf("Usage: watch <hex_addr> [size:1/2/4/8] [type:w/rw]\n");
        return 1;
    }
    
    char argcopy[128];
    strncpy(argcopy, arg, 127);
    argcopy[127] = '\0';
    
    char* addr_str = argcopy;
    char* size_str = nullptr;
    char* type_str = nullptr;
    
    char* p = argcopy;
    while (*p && *p != ' ') p++;
    if (*p == ' ') {
        *p = '\0';
        size_str = p + 1;
        p = size_str;
        while (*p && *p != ' ') p++;
        if (*p == ' ') {
            *p = '\0';
            type_str = p + 1;
        }
    }
    
    uint64_t addr = 0;
    p = addr_str;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    while (*p) {
        addr <<= 4;
        if (*p >= '0' && *p <= '9') addr |= (*p - '0');
        else if (*p >= 'a' && *p <= 'f') addr |= (*p - 'a' + 10);
        else if (*p >= 'A' && *p <= 'F') addr |= (*p - 'A' + 10);
        else break;
        p++;
    }
    
    int size = 8;
    if (size_str) {
        if (strcmp(size_str, "1") == 0) size = 1;
        else if (strcmp(size_str, "2") == 0) size = 2;
        else if (strcmp(size_str, "4") == 0) size = 4;
        else if (strcmp(size_str, "8") == 0) size = 8;
    }
    
    int type = 1; 
    if (type_str) {
        if (strcmp(type_str, "rw") == 0) type = 3;
    }
    
    int idx = hw_watchpoint_set(addr, size, type);
    if (idx != -1) {
        printf("Watchpoint %d set at 0x%lx (Size: %d, Type: %s)\n", idx, addr, size, type == 3 ? "R/W" : "W");
    } else {
        printf("Error: All 4 hardware watchpoints are in use.\n");
    }
    return 0;
}

int Shell::cmd_unwatch(const char* arg) {
    if (!arg || arg[0] == '\0') {
        printf("Usage: unwatch <index 0-3>\n");
        return 1;
    }
    int idx = arg[0] - '0';
    if (idx >= 0 && idx <= 3) {
        hw_watchpoint_clear(idx);
        printf("Watchpoint %d cleared.\n", idx);
    } else {
        printf("Invalid index. Must be 0, 1, 2, or 3.\n");
    }
    return 0;
}

static uint64_t ipc_worker_handle = 0;

static void ipc_worker_task() {
    char buf[64];
    uint32_t data_size = sizeof(buf);
    uint32_t handle_count = 0;

    int err = kom_channel_read(ipc_worker_handle, buf, &data_size, nullptr, &handle_count);
    
    if (err == KOM_OK) {
        buf[data_size] = '\0';
        printf("[IPC Worker] Received message: '%s'\n", buf);
        
        const char* reply = "PONG from Worker!";
        kom_channel_write(ipc_worker_handle, reply, strlen(reply), nullptr, 0);
    } else {
        printf("[IPC Worker] Read failed with error: %d\n", err);
    }

    kom_close(ipc_worker_handle);
    process_exit();
}

int Shell::cmd_ipc(const char* arg) {
    (void)arg;
    printf("Testing KOM Channel IPC (Zircon-Style)...\n");

    uint64_t handle_a, handle_b;
    int err = kom_channel_create(&handle_a, &handle_b);
    
    if (err != KOM_OK) {
        printf("Error: Failed to create channel pair (Err: %d)\n", err);
        return 1;
    }

    printf("Channel created. Handle A: %lu, Handle B: %lu\n", handle_a, handle_b);

    ipc_worker_handle = handle_b;
    create_kernel_task(ipc_worker_task);

    const char* msg = "PING from Shell!";
    printf("[Shell] Sending message: '%s'\n", msg);
    err = kom_channel_write(handle_a, msg, strlen(msg), nullptr, 0);
    
    if (err != KOM_OK) {
        printf("Error: Write failed (Err: %d)\n", err);
        kom_close(handle_a);
        return 1;
    }

    char buf[64];
    uint32_t data_size = sizeof(buf);
    uint32_t handle_count = 0;
    
    err = kom_channel_read(handle_a, buf, &data_size, nullptr, &handle_count);
    
    if (err == KOM_OK) {
        buf[data_size] = '\0';
        console_set_color(CONSOLE_COLOR_LIGHT_GREEN, CONSOLE_COLOR_BLACK);
        printf("[Shell] Received reply: '%s'\n", buf);
        console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    } else {
        printf("Error: Read failed (Err: %d)\n", err);
    }

    kom_close(handle_a);
    return 0;
}

int Shell::cmd_usb(const char* arg) { 
    (void)arg;
    rust_usb_print_ports();
    return 0;
}

int Shell::cmd_i2c(const char* arg) {
    if (!arg || arg[0] == '\0') {
        printf("Usage: i2c <hex_addr> (e.g., i2c 0x50 for RAM SPD)\n");
        return 1;
    }

    uint64_t addr = 0;
    const char* p = arg;
    if (p[0] == '0' && p[1] == 'x') { p += 2; }

    while (*p) {
        addr <<= 4;
        if (*p >= '0' && *p <= '9') { addr |= (*p - '0'); }
        else if (*p >= 'a' && *p <= 'f') { addr |= (*p - 'a' + 10); }
        else if (*p >= 'A' && *p <= 'F') { addr |= (*p - 'A' + 10); }
        else { break; }
        p++;
    }

    i2c_dump((uint8_t)addr);
    return 0;
}

int Shell::cmd_fbtest(const char* arg) {
    (void)arg;
    printf("Testing ONS GPU Device: /devices/fb0...\n");

    KObject* fb_node = ons_resolve("/devices/fb0");
    if (!fb_node) {
        printf("Error: /devices/fb0 not found in ONS!\n");
        return 1;
    }

    if (fb_node->type != KObjectType::GPU_DEVICE) {
        printf("Error: fb0 is not a GPU_DEVICE.\n");
        kobject_unref(fb_node);
        return 1;
    }

    KDevice* dev = (KDevice*)fb_node;

    struct fb_var_screeninfo vinfo;
    if (dev->query(KDQ_GPU_VRAM_SIZE, &vinfo, sizeof(vinfo)) != KOM_OK) {
        if (((Device*)dev)->ioctl(FBIOGET_VSCREENINFO, &vinfo) != 0) {
            printf("Error: Could not get screen info.\n");
            kobject_unref(fb_node);
            return 1;
        }
    }

    printf("Screen Info: %dx%d, %d bpp\n", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);
    printf("Drawing SMP-Themed test pattern via Direct KDevice Write...\n");

    uint32_t w = 250, h = 250;
    uint32_t* buf = (uint32_t*)kmalloc(w * h * 4);
    if (!buf) { kobject_unref(fb_node); return 1; }

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
    if (!arg) { return 0; }
    const char* msg_ptr = arg;
    const char* p = arg;
    while (*p && *p != ' ') { p++; }
    if (*p == ' ') { msg_ptr = p + 1; }
    console_set_color(CONSOLE_COLOR_DARK_GREY, CONSOLE_COLOR_BLACK); printf("[ STARTUP ] ");
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    printf("%s\n", msg_ptr);
    return 0;
}

int Shell::cmd_systemcheck(const char* arg) { (void)arg; perform_system_check(); return 0; }
int Shell::cmd_reboot(const char* arg) { (void)arg; system_reboot(); return 0; }
int Shell::cmd_shutdown(const char* arg) { (void)arg; system_shutdown("User Command (Shell)"); return 0; }
int Shell::cmd_pcie(const char* arg) { (void)arg; pcie_print_all(); return 0; }

int Shell::cmd_system(const char* arg) {
    if (!arg || arg[0] == '\0') { show_system_info_all(); }
    else if (strcmp(arg, "mem") == 0) { kheap_print_stats(); show_memory_map(); }
    else if (strcmp(arg, "tasks") == 0) { show_task_list(); }
    else if (strcmp(arg, "all") == 0) { show_system_info_all(); }
    else if (strcmp(arg, "ffi") == 0) { SingularityFFI::print_ffi_stats(); }
    else {
        printf("Unknown system command: %s\n", arg);
        return 1;
    }
    return 0;
}

int Shell::cmd_taskmgr(const char* arg) {
    (void)arg;
    show_task_manager();
    return 0;
}

int Shell::cmd_power(const char* arg) { (void)arg; printf("Power monitoring not supported directly.\n"); return 0; }
int Shell::cmd_turbo(const char* arg) { (void)arg; printf("Usage: turbo <on/off>\n"); return 0; }

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
    if (!arg || arg[0] == '\0') { printf("Usage: profile <on/off/show>\n"); return 1; }
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

// YENİ: Help komutu artık tek seferde (Batch Rendering) çiziliyor.
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

    console_set_color(CONSOLE_COLOR_LIGHT_CYAN, CONSOLE_COLOR_BLACK); printf("========================================\n"); 
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    
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
    if (!cmd || cmd[0] == '\0') { return 0; }
    
    for (int i = 0; i < command_count; i++) {
        if (strcmp(cmd, command_table[i].name) == 0) {
            return (this->*(command_table[i].handler))(arg);
        }
    }
    
    printf("Unknown command: '%s'. Type 'help' for a list of commands.\n", cmd);
    return 1;
}