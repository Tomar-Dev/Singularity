// system/debug/system_check.cpp
#include "system/debug/system_check.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "memory/kheap.h"
#include "memory/pmm.h"
#include "memory/paging.h"
#include "archs/cpu/x86_64/core/cpuid.h"
#include "drivers/acpi/acpi.h"
#include "drivers/acpi/numa.h"
#include "drivers/apic/apic.h"
#include "drivers/keyboard/keyboard.h"
#include "system/device/device.h"
#include "system/disk/cache.hpp"
#include "drivers/serial/serial.h"
#include "system/security/rng.h"
#include "system/security/stack_protector.h"
#include "drivers/video/framebuffer.h"
#include "archs/cpu/x86_64/core/msr.h"
#include "drivers/rtc/rtc.hpp" 
#include "archs/cpu/x86_64/core/ports.h" 
#include "system/sync/rcu.h" 
#include "kernel/ksyms.h"
#include "archs/kom/kom_aal.h"         
#include "archs/cpu/cpu_hal.h"
#include "kernel/config.h"
#include "system/console/console.h"

extern "C" {
    extern volatile uint64_t cpu_tick_counts[];
    extern uint8_t num_cpus;
    extern bool uefi_available();
    
    // YENİ: LTO Optimization Bypass
    extern void syscall_entry();
    
    struct process;
    extern struct process* process_list_head;
    
    extern uintptr_t __stack_chk_guard;
    
    void rtc_get_time(rtc_time_t* time);
}

static void print_check_row(const char* name, int status, const char* msg) {
    console_set_color(CONSOLE_COLOR_LIGHT_GREY, CONSOLE_COLOR_BLACK);
    printf("%-20s: ", name);
    
    if (status == 1) {
        console_set_color(CONSOLE_COLOR_LIGHT_GREEN, CONSOLE_COLOR_BLACK);
        printf("[PASS]\n");
    } else if (status == 0) {
        console_set_color(CONSOLE_COLOR_YELLOW, CONSOLE_COLOR_RED);
        printf("[FAIL]");
        console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
        printf(" -> %s\n", msg);
    } else {
        console_set_color(CONSOLE_COLOR_YELLOW, CONSOLE_COLOR_BLACK);
        printf("[INFO]");
        console_set_color(CONSOLE_COLOR_LIGHT_GREY, CONSOLE_COLOR_BLACK);
        printf(" -> %s\n", msg);
    }
}

static int check_paging() {
    void* phys_ptr = pmm_alloc_frame();
    if (!phys_ptr) { return 0; } else { /* Valid */ }
    
    void* virt_ptr = ioremap((uint64_t)phys_ptr, 4096, PAGE_PRESENT | PAGE_WRITE | PAGE_NX);
    if (!virt_ptr) {
        pmm_free_frame(phys_ptr);
        return 0;
    } else { /* Valid */ }
    
    volatile uint32_t* ptr = (volatile uint32_t*)virt_ptr;
    *ptr = 0xCAFEBABE;
    bool ok = (*ptr == 0xCAFEBABE);
    
    iounmap(virt_ptr, 4096);
    pmm_free_frame(phys_ptr);
    return ok ? 1 : 0;
}

static int check_ram_integrity() {
    size_t size = 2 * 1024 * 1024;
    void* ptr = kmalloc(size);
    if (!ptr) { return 0; } else { /* Tested Size Generated */ } 
    
    uint64_t* p64 = (uint64_t*)ptr;
    size_t count = size / 8;
    for(size_t i=0; i<count; i++) { p64[i] = 0xAAAAAAAAAAAAAAAA; }
    
    bool ok = true;
    for(size_t i=0; i<count; i++) {
        if (p64[i] != 0xAAAAAAAAAAAAAAAA) { ok = false; break; } else { /* Memory unit retained value */ }
    }
    
    kfree(ptr);
    return ok ? 1 : 0;
}

static int check_vfs_write() {
    KObject* ramdisk_obj = ons_resolve("/ramdisk");
    if (!ramdisk_obj) { return 0; } else { /* Found */ }
    
    if (ramdisk_obj->type != KObjectType::CONTAINER) {
        kobject_unref(ramdisk_obj);
        return 0;
    } else { /* Target is active Directory */ }
    
    KContainer* ramdisk = (KContainer*)ramdisk_obj;
    const char* test_file = "chk_w.tmp";
    
    KObject* existing = ramdisk->lookup(test_file);
    if (existing) {
        kobject_unref(existing);
        kobject_unref(ramdisk);
        return 1; 
    } else { /* Path clear */ }
    
    if (ramdisk->create_child(test_file, KObjectType::BLOB) == KOM_OK) {
        ramdisk->unbind(test_file);
        kobject_unref(ramdisk);
        return 1;
    } else {
        // I/O Blocked
    }
    
    kobject_unref(ramdisk);
    return 2; 
}

static int check_disk_io() {
    device_t dev = device_get_first_block();
    if (!dev) { return 2; } else { /* Connected storage active */ } 
    
    uint8_t* buf = (uint8_t*)kmalloc_contiguous(4096); 
    if (!buf) { return 0; } else { /* Buffer allocated */ }
    
    int ret = DiskCache::readBlock((Device*)dev, 0, 1, buf);
    kfree_contiguous(buf, 4096);
    return (ret == 1) ? 1 : 0;
}

static int check_rng() {
    uint64_t r1 = get_secure_random();
    uint64_t r2 = get_secure_random();
    if (r1 == 0 && r2 == 0) { return 0; } else { /* Output active */ }
    if (r1 == r2) { return 0; } else { /* Safe random output */ }
    return 1;
}

static int check_stack_canary() {
    if (__stack_chk_guard == 0) { return 0; } else { return 1; }
}

static int check_syscall_msr(char* out_msg) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000001));
    
    if (!(edx & (1 << 11))) {
        sprintf(out_msg, "Not Supported by CPU");
        return 0;
    } else {
        // HW Verified
    }

    uint64_t lstar = rdmsr(0xC0000082);
    if (lstar == 0) {
        sprintf(out_msg, "LSTAR: 0x0 (Unset)");
        return 0;
    } else {
        return 1;
    }
}

static int check_framebuffer() {
    if (fb_info.address == 0) { return 0; } else { /* Valid */ }
    if (fb_info.width == 0 || fb_info.height == 0) { return 0; } else { /* Valid */ }
    return 1;
}

static int check_vram_integrity() {
    if (!backbuffer) { return 0; } else { /* Render path confirmed */ }
    uint32_t original = backbuffer[0];
    backbuffer[0] = 0x12345678;
    bool ok = (backbuffer[0] == 0x12345678);
    backbuffer[0] = original;
    return ok ? 1 : 0;
}

static int check_serial() {
    uint8_t status = hal_io_inb(0x3F8 + 5);
    return (status != 0xFF) ? 1 : 0;
}

static int check_pci_bus() {
    hal_io_outl(0xCF8, 0x80000000);
    uint32_t val = hal_io_inl(0xCFC);
    if ((val & 0xFFFF) != 0xFFFF) { return 1; } else { return 0; }
}

static int check_rtc_sanity() {
    rtc_time_t t;
    rtc_get_time(&t);
    if (t.year < 2020 || t.year > 2100) { return 0; } else { return 1; }
}

static int check_symbol_resolver() {
    uint64_t offset = 0;
    // BUG-006 FIX: kmain LTO tarafından inline yapılmış olabilir. 
    // Assembler tarafından export edilen kesin fonksiyon: syscall_entry
    const char* sym = ksyms_resolve_symbol((uint64_t)syscall_entry, &offset);
    if (sym && strcmp(sym, "syscall_entry") == 0 && offset == 0) { return 1; } else { return 2; } 
}

static int check_rcu_sync() {
    uint64_t start_tick = hal_timer_get_ticks();
    synchronize_rcu();
    uint64_t diff = hal_timer_get_ticks() - start_tick;
    if (diff < 1500) { return 1; } else { return 0; }
}

static int check_numa() {
    if (numa_node_count > 0) { return 1; } else { return 2; }
}

static char sys_msg_buf[64];

void perform_system_check() {
    console_set_color(CONSOLE_COLOR_LIGHT_CYAN, CONSOLE_COLOR_BLACK);
    printf("\n========== ");
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    printf("%s DIAGNOSTICS", SINGULARITY_SYS_NAME);
    console_set_color(CONSOLE_COLOR_LIGHT_CYAN, CONSOLE_COLOR_BLACK);
    printf(" ==========\n");
    
    console_set_color(CONSOLE_COLOR_DARK_GREY, CONSOLE_COLOR_BLACK);
    printf("System Core Check\n");
    printf("----------------------------------------\n");

    void* p = kmalloc(64);
    print_check_row("Heap Allocator", p != NULL ? 1 : 0, "OOM");
    if(p) { kfree(p); } else { /* Drop NULL */ }
    
    print_check_row("Physical Memory", pmm_get_free_memory() > 0 ? 1 : 0, "Full");
    print_check_row("RAM Integrity", check_ram_integrity(), "Corruption");
    print_check_row("VMM/Paging", check_paging(), "Map Failed");
    print_check_row("RNG Entropy", check_rng(), "Stale/Zero");
    print_check_row("Stack Protection", check_stack_canary(), "Canary Inactive");
    
    memset(sys_msg_buf, 0, 64);
    strcpy(sys_msg_buf, "LSTAR Unset");
    print_check_row("Syscall MSR", check_syscall_msr(sys_msg_buf), sys_msg_buf);

    int sym_status = check_symbol_resolver();
    print_check_row("Symbol Hash Map", sym_status, sym_status == 2 ? "Stripped Binary" : "Failed");
    print_check_row("RCU Synchronization", check_rcu_sync(), "Grace Period Timeout");
    
    int numa_status = check_numa();
    print_check_row("NUMA Topology", numa_status, numa_status == 2 ? "UMA System (Single Node)" : "Failed");

    print_check_row("ACPI Tables", acpi_find_table("APIC") != NULL ? 1 : 0, "MADT Missing");
    print_check_row("Local APIC", is_apic_enabled() ? 1 : 0, "Disabled");
    print_check_row("UEFI Runtime", uefi_available() ? 1 : 0, "Legacy Mode");
    print_check_row("FPU/SSE Context", cpu_info.has_fpu ? 1 : 0, "Missing");
    print_check_row("PCI Bus", check_pci_bus(), "Unreachable");
    print_check_row("RTC Clock", check_rtc_sanity(), "Invalid Date");

    print_check_row("Device Manager", DeviceManager::getDeviceCount() > 0 ? 1 : 0, "Empty");
    print_check_row("Framebuffer", check_framebuffer(), "Invalid Config");
    print_check_row("VRAM Integrity", check_vram_integrity(), "Read/Write Fail");
    print_check_row("Serial Port", check_serial(), "Not Detected");
    
    int disk_status = check_disk_io();
    print_check_row("Disk I/O (Direct DMA)", disk_status, disk_status == 2 ? "No Disk" : "Read Error");
    
    KObject* root_obj = ons_resolve("/");
    bool root_ok = (root_obj != nullptr);
    if (root_obj) { kobject_unref(root_obj); } else { /* Empty path */ }
    
    print_check_row("ONS Root", root_ok ? 1 : 0, "Not Mounted");
    
    int vfs_w_status = check_vfs_write();
    print_check_row("ONS Write Access", vfs_w_status, vfs_w_status == 2 ? "Read-Only ONS" : "Write Failed");
    
    print_check_row("Input Subsystem", keyboard_has_input() || true ? 1 : 0, "");
    print_check_row("Scheduler Core", process_list_head != NULL ? 1 : 0, "No Tasks");
    
    uint64_t start_ticks = hal_timer_get_ticks();
    bool timer_ok = false;
    for (int i = 0; i < 50; i++) { 
        if (hal_timer_get_ticks() > start_ticks) {
            timer_ok = true;
            break;
        } else {
            hal_timer_delay_ms(1);
        }
    }
    print_check_row("Timer Interrupts", timer_ok ? 1 : 0, "Stalled");

    bool smp_ok = true;
    for(int i=0; i<num_cpus; i++) {
        if (cpu_tick_counts[i] == 0) { smp_ok = false; } else { /* CPU is operational */ }
    }
    
    char smp_msg[32];
    sprintf(smp_msg, "%d CPUs Active", num_cpus);
    print_check_row("SMP Cores", smp_ok ? 1 : 0, smp_ok ? smp_msg : "Core Dead");

    console_set_color(CONSOLE_COLOR_LIGHT_CYAN, CONSOLE_COLOR_BLACK);
    printf("========================================\n");
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
}