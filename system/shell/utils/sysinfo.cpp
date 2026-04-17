// system/shell/utils/sysinfo.cpp
#include "system/shell/utils/sysinfo.hpp"
#include "libc/stdio.h"
#include "libc/string.h"
#include "archs/memory/pmm.h"
#include "archs/memory/kheap.h"
#include "system/process/process.h"
#include "archs/cpu/x86_64/core/cpuid.h"
#include "drivers/smbios/smbios.h"
#include "system/disk/cache.hpp"
#include "system/device/device.h"
#include "drivers/serial/serial.h"
#include "archs/cpu/x86_64/smp/smp.h"
#include "kernel/config.h"
#include "system/console/console.h"

#include "archs/kom/common/kblob.hpp"
#include "archs/kom/common/kcontainer.hpp"
#include "archs/kom/common/ons.hpp"

extern "C" {
    void smbios_print_full_info();
    void smbios_get_ram_info(ram_hw_info_t* info);
    void print_cpu_z_info();
    void process_get_info(int* total, int* running, int* zombie);
    void timer_sleep(uint64_t ticks);
    uint64_t timer_get_ticks();
    extern const char* kernel_build_date;

    extern volatile uint64_t cpu_tick_counts[];
    extern uint8_t num_cpus;
    extern per_cpu_t* per_cpu_data[];

    size_t   kheap_get_cached_size();
    size_t   kheap_get_payload_size();
    size_t   kheap_get_reserved_size();
    size_t   disk_cache_get_size();

    uint64_t pmm_get_reserved_memory(); 
    void     pmm_flush_magazines();
}

static void print_size(const char* label, uint64_t bytes, console_color_t val_color) {
    console_set_color(CONSOLE_COLOR_LIGHT_GREY, CONSOLE_COLOR_BLACK);
    if (label && label[0]) {
        printf("%-16s: ", label);
    } else {
        // Space mapped natively
    }
    console_set_color(val_color, CONSOLE_COLOR_BLACK);

    if (bytes >= 1024ULL * 1024ULL) {
        uint64_t mb = bytes / (1024ULL * 1024ULL);
        uint64_t kb = (bytes % (1024ULL * 1024ULL)) / 1024ULL;
        if (kb > 0 && mb < 1000) {
            printf("%d.%02d MB", (int)mb, (int)((kb * 100ULL) / 1024ULL));
        } else {
            printf("%d MB", (int)mb);
        }
    } else {
        printf("%d KB", (int)(bytes / 1024ULL));
    }
}

static void print_kv(const char* key, const char* val_fmt, ...) {
    console_set_color(CONSOLE_COLOR_LIGHT_GREY, CONSOLE_COLOR_BLACK);
    printf("%-14s: ", key);
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    va_list args;
    va_start(args, val_fmt);
    vprintf(val_fmt, args);
    va_end(args);
    printf("\n");
}

void show_memory_map() {
    pmm_flush_magazines();

    uint64_t pmm_total  = pmm_get_total_memory();
    uint64_t pmm_used   = pmm_get_used_memory();
    uint64_t pmm_free   = pmm_get_free_memory();

    uint64_t kernel_static = pmm_get_reserved_memory();
    uint64_t dynamic       = 0;
    if (pmm_used > kernel_static) {
        dynamic = pmm_used - kernel_static;
    } else {
        // Avoid calculation bounds underflow
    }

    uint64_t heap_reserved = kheap_get_reserved_size();
    uint64_t heap_payload  = kheap_get_payload_size();
    uint64_t heap_cached   = kheap_get_cached_size();

    uint64_t other = 0;
    if (dynamic > heap_reserved) {
        other = dynamic - heap_reserved;
    } else {
        // Free bounds matched perfectly
    }

    uint64_t disk_cached       = disk_cache_get_size();
    uint64_t total_reclaimable = heap_cached + disk_cached;

    uint64_t active_used = kernel_static + heap_payload + other;

    if (active_used > pmm_used) {
        active_used = pmm_used;
    } else {
        // Values coherent
    }

    uint64_t available = pmm_free;

    console_set_color(CONSOLE_COLOR_LIGHT_CYAN, CONSOLE_COLOR_BLACK);
    printf("\n========== ");
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    printf("MEMORY STATISTICS");
    console_set_color(CONSOLE_COLOR_LIGHT_CYAN, CONSOLE_COLOR_BLACK);
    printf(" ==========\n");
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);

    print_size("Total RAM",  pmm_total, CONSOLE_COLOR_WHITE);       printf("\n");
    print_size("Available",  available, CONSOLE_COLOR_LIGHT_GREEN); printf("\n");

    printf("\n--- Breakdown ---\n");

    print_size("Active Used",   active_used,  CONSOLE_COLOR_LIGHT_RED);
    console_set_color(CONSOLE_COLOR_DARK_GREY, CONSOLE_COLOR_BLACK);
    printf(" (non-reclaimable)\n");

    print_size("  Kernel/Static", kernel_static, CONSOLE_COLOR_WHITE); printf("\n");
    print_size("  Heap Objects",  heap_payload,  CONSOLE_COLOR_WHITE); printf("\n");
    print_size("  Buffers/Stack", other,         CONSOLE_COLOR_WHITE); printf("\n");

    print_size("Cached",   total_reclaimable, CONSOLE_COLOR_YELLOW);
    console_set_color(CONSOLE_COLOR_DARK_GREY, CONSOLE_COLOR_BLACK);
    printf(" (reclaimable)\n");
    print_size("  Slab cache",  heap_cached,  CONSOLE_COLOR_DARK_GREY); printf("\n");
    print_size("  Disk cache",  disk_cached,  CONSOLE_COLOR_DARK_GREY); printf("\n");

    print_size("Free Frames", pmm_free, CONSOLE_COLOR_WHITE); printf("\n");

    console_set_color(CONSOLE_COLOR_LIGHT_CYAN, CONSOLE_COLOR_BLACK);
    printf("=======================================\n");
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
}

void show_task_list() {
    int total = 0, running = 0, zombie = 0;
    process_get_info(&total, &running, &zombie);

    console_set_color(CONSOLE_COLOR_LIGHT_CYAN, CONSOLE_COLOR_BLACK);
    printf("\n========== ");
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    printf("PROCESS SUMMARY");
    console_set_color(CONSOLE_COLOR_LIGHT_CYAN, CONSOLE_COLOR_BLACK);
    printf(" ==========\n");

    print_kv("Total Tasks", "%d", total);
    print_kv("Running",     "%d", running);

    console_set_color(CONSOLE_COLOR_LIGHT_GREY, CONSOLE_COLOR_BLACK);
    printf("%-14s: ", "Zombies");
    if (zombie > 0) {
        console_set_color(CONSOLE_COLOR_LIGHT_RED, CONSOLE_COLOR_BLACK);
    } else {
        console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    }
    printf("%d\n", zombie);

    printf("\n");
    console_set_color(CONSOLE_COLOR_LIGHT_GREY, CONSOLE_COLOR_BLACK);
    printf("[SMP] Core Activity (Interrupts):\n");
    for (int i = 0; i < num_cpus; i++) {
        console_set_color(CONSOLE_COLOR_LIGHT_GREY, CONSOLE_COLOR_BLACK);
        printf("  CPU %d: ", i);
        console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
        printf("%lu ticks\n", cpu_tick_counts[i]);
    }

    console_set_color(CONSOLE_COLOR_LIGHT_CYAN, CONSOLE_COLOR_BLACK);
    printf("==================================\n");
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
}

void show_system_info_all() {
    console_set_color(CONSOLE_COLOR_LIGHT_CYAN, CONSOLE_COLOR_BLACK);
    printf("\n========== ");
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    printf("%s SYSTEM REPORT", SINGULARITY_SYS_NAME);
    console_set_color(CONSOLE_COLOR_LIGHT_CYAN, CONSOLE_COLOR_BLACK);
    printf(" ==========\n");
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);

    print_kv("Kernel Ver", "%s %s (%s)", SINGULARITY_SYS_NAME, SINGULARITY_SYS_VER, SINGULARITY_SYS_ARCH);
    print_kv("Build Date", "%s", kernel_build_date);

    uint64_t up_ms = timer_get_ticks() * 4;
    print_kv("Uptime", "%d.%03d sec",
             (int)(up_ms / 1000), (int)(up_ms % 1000));

    print_cpu_z_info();

    console_set_color(CONSOLE_COLOR_LIGHT_GREY, CONSOLE_COLOR_BLACK);
    printf("%-14s:[ ", "Features");
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    if (cpu_info.has_avx)      printf("AVX ");
    if (cpu_info.has_sse2)     printf("SSE2 ");
    if (cpu_info.has_rdrand)   printf("RDRAND ");
    if (cpu_info.has_xsaveopt) printf("XSAVEOPT ");
    console_set_color(CONSOLE_COLOR_LIGHT_GREY, CONSOLE_COLOR_BLACK);
    printf("]\n");

    smbios_print_full_info();

    show_memory_map();
    show_task_list();
}

void show_task_manager() {
    uint64_t start_idle_ticks[32] = {0};
    uint64_t end_idle_ticks[32]   = {0};

    uint64_t start_total = timer_get_ticks();
    for (int i = 0; i < num_cpus; i++) {
        if (per_cpu_data[i]) {
            start_idle_ticks[i] = per_cpu_data[i]->idle_ticks;
        } else {
            // Null Struct
        }
    }

    timer_sleep(25);

    uint64_t end_total   = timer_get_ticks();
    uint64_t delta_total = end_total - start_total;
    if (delta_total == 0) { delta_total = 1; } else { /* Bounds Safe */ }

    console_set_color(CONSOLE_COLOR_LIGHT_CYAN, CONSOLE_COLOR_BLACK);
    printf("\n========== ");
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    printf("%s TASK MANAGER", SINGULARITY_SYS_NAME);
    console_set_color(CONSOLE_COLOR_LIGHT_CYAN, CONSOLE_COLOR_BLACK);
    printf(" ==========\n");

    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    printf("CPU Usage (snapshot):\n");

    for (int i = 0; i < num_cpus; i++) {
        if (per_cpu_data[i]) {
            end_idle_ticks[i] = per_cpu_data[i]->idle_ticks;
        } else {
            // Node mapping disabled
        }

        uint64_t delta_idle = 0;
        if (end_idle_ticks[i] >= start_idle_ticks[i]) {
            delta_idle = end_idle_ticks[i] - start_idle_ticks[i];
        } else {
            // Tick wraparound
        }

        if (delta_idle > delta_total) { delta_idle = delta_total; } else { /* Underflow Guard */ }

        uint64_t delta_active = 0;
        if (delta_total >= delta_idle) {
            delta_active = delta_total - delta_idle;
        } else {
            delta_active = 0;
        }

        int usage = (int)((delta_active * 100ULL) / delta_total);

        console_set_color(CONSOLE_COLOR_LIGHT_GREY, CONSOLE_COLOR_BLACK);
        printf("  Core %d: ", i);

        int bars = usage / 5;
        console_color_t bar_color = (usage > 80)
                            ? CONSOLE_COLOR_LIGHT_RED
                            : (usage > 50 ? CONSOLE_COLOR_YELLOW : CONSOLE_COLOR_LIGHT_GREEN);
        console_set_color(bar_color, CONSOLE_COLOR_BLACK);
        for (int k = 0; k < 20; k++) {
            printf("%c", (k < bars) ? '|' : '.');
        }

        console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
        printf(" %3d%%\n", usage);
    }

    pmm_flush_magazines();
    uint64_t total         = pmm_get_total_memory();
    uint64_t pmm_used      = pmm_get_used_memory();
    uint64_t kernel_static = pmm_get_reserved_memory();
    uint64_t heap_cached   = kheap_get_cached_size();
    uint64_t disk_cached   = disk_cache_get_size();
    uint64_t total_cached  = heap_cached + disk_cached;

    uint64_t active_used = 0;
    if (pmm_used > total_cached) {
        active_used = pmm_used - total_cached;
    } else {
        active_used = 0;
    }

    printf("\nMemory Usage:\n");
    console_set_color(CONSOLE_COLOR_LIGHT_GREY, CONSOLE_COLOR_BLACK);
    printf("  Total : "); print_size("", total,         CONSOLE_COLOR_WHITE);      printf("\n");
    printf("  Active: "); print_size("", active_used,   CONSOLE_COLOR_LIGHT_RED);  printf("\n");
    printf("  Cached: "); print_size("", total_cached,  CONSOLE_COLOR_YELLOW);     printf("\n");
    printf("  Kernel: "); print_size("", kernel_static, CONSOLE_COLOR_WHITE);      printf("\n");

    printf("\n");
    console_set_color(CONSOLE_COLOR_LIGHT_CYAN, CONSOLE_COLOR_BLACK);
    printf("================================\n");
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
}

static size_t uptime_read_cb(uint64_t offset, void* buffer, size_t count) {
    char temp[64];
    uint64_t t = timer_get_ticks();
    snprintf(temp, sizeof(temp), "%llu.%llu\n", t / 250, (t % 250) * 4);
    size_t len = strlen(temp);
    if (offset >= len) {
        return 0;
    } else {
        size_t to_copy = (count < len - offset) ? count : (len - offset);
        memcpy(buffer, (uint8_t*)temp + offset, to_copy);
        return to_copy;
    }
}

static size_t meminfo_read_cb(uint64_t offset, void* buffer, size_t count) {
    char temp[128];
    uint64_t total = pmm_get_total_memory();
    uint64_t used = pmm_get_used_memory();
    uint64_t free = pmm_get_free_memory();
    snprintf(temp, sizeof(temp), "MemTotal: %llu kB\nMemFree:  %llu kB\nMemUsed:  %llu kB\n", total / 1024, free / 1024, used / 1024);
    size_t len = strlen(temp);
    if (offset >= len) {
        return 0;
    } else {
        size_t to_copy = (count < len - offset) ? count : (len - offset);
        memcpy(buffer, (uint8_t*)temp + offset, to_copy);
        return to_copy;
    }
}

static size_t version_read_cb(uint64_t offset, void* buffer, size_t count) {
    char ver[128];
    snprintf(ver, sizeof(ver), "%s %s (%s)\n", SINGULARITY_SYS_NAME, SINGULARITY_SYS_VER, SINGULARITY_SYS_ARCH);
    size_t len = strlen(ver);
    if (offset >= len) {
        return 0;
    } else {
        size_t to_copy = (count < len - offset) ? count : (len - offset);
        memcpy(buffer, (uint8_t*)ver + offset, to_copy);
        return to_copy;
    }
}

extern "C" {
    void setup_system_info_nodes() {
        KObject* sys_dir_obj = ons_resolve("/system");
        if (sys_dir_obj && sys_dir_obj->type == KObjectType::CONTAINER) {
            KContainer* sys_dir = (KContainer*)sys_dir_obj;
            KContainer* info_dir = new KContainer();
            sys_dir->bind("info", info_dir);
            
            KDynamicBlob* b_up = new KDynamicBlob("uptime", uptime_read_cb);
            info_dir->bind("uptime", b_up);
            kobject_unref(b_up);
            
            KDynamicBlob* b_mem = new KDynamicBlob("meminfo", meminfo_read_cb);
            info_dir->bind("meminfo", b_mem);
            kobject_unref(b_mem);

            KDynamicBlob* b_ver = new KDynamicBlob("version", version_read_cb);
            info_dir->bind("version", b_ver);
            kobject_unref(b_ver);

            kobject_unref(info_dir);
        } else {
            // Unbound logic tree
        }
        if (sys_dir_obj) {
            kobject_unref(sys_dir_obj);
        } else {
            // Free operation mapped
        }
    }
}
