// kernel/debug.c
#include "kernel/debug.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "archs/cpu/x86_64/sync/spinlock.h"
#include "archs/cpu/cpu_hal.h"
#include "kernel/ksyms.h" 
#include "drivers/uefi/uefi.h"
#include "kernel/config.h"
#include "archs/cpu/x86_64/apic/apic.h" 

extern void rtc_get_formatted_time(char* buffer);

#define KLOG_BUF_SIZE 4096
static char klog_buffer[KLOG_BUF_SIZE];
static size_t klog_head = 0;
static size_t klog_tail = 0; 
static bool klog_full = false;

static char debug_fmt_buf[1024];
static spinlock_t debug_lock = {0, 0, {0}};

void debug_force_unlock() {
    spinlock_init(&debug_lock);
}

void dbg_direct(const char* msg) {
    const char* p = msg;
    while (*p) {
        while ((hal_io_inb(0x3F8 + 5) & 0x20) == 0); 
        hal_io_outb(0x3F8, *p++);
    }
}

static void klog_append(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        klog_buffer[klog_head] = str[i];
        klog_head = (klog_head + 1) % KLOG_BUF_SIZE;
        if (klog_head == klog_tail) {
            klog_full = true;
            klog_tail = (klog_tail + 1) % KLOG_BUF_SIZE;
        }
    }
}

struct stackframe {
    struct stackframe* rbp;
    uint64_t rip;
};

void print_stack_trace_from(uint64_t rbp, uint64_t rip) {
    struct stackframe* stk = (struct stackframe*)rbp;
    
    printf("\n--- Call Stack Trace ---\n");
    dbg_direct("\n--- Call Stack Trace ---\n");
    
    uint64_t offset = 0;
    const char* sym = ksyms_resolve_symbol(rip, &offset);
    if (sym) {
        printf(" [00] 0x%016lx <%s+%ld>\n", rip, sym, offset);
        char dbg_buf[128];
        snprintf(dbg_buf, sizeof(dbg_buf), " [00] 0x%016lx <%s+%ld>\n", rip, sym, offset); // NOLINT
        dbg_direct(dbg_buf);
    } else {
        printf("[00] 0x%016lx\n", rip);
    }

    int depth = 1;
    while(stk && depth < 20) {
        if ((uint64_t)stk < 4096 || ((uint64_t)stk & 7)) {
            printf(" [!] Stack walk aborted: Frame pointer (0x%lx) invalid or unaligned.\n", (uint64_t)stk);
            dbg_direct(" [!] Stack walk aborted: Invalid frame pointer.\n");
            break;
        }
        
        if (stk->rip == 0) { break; }

        offset = 0;
        sym = ksyms_resolve_symbol(stk->rip, &offset);
        
        char line_buf[128];
        if (sym) {
            snprintf(line_buf, sizeof(line_buf), "[%02d] 0x%016lx <%s+%ld>\n", depth, stk->rip, sym, offset); // NOLINT
        } else {
            snprintf(line_buf, sizeof(line_buf), " [%02d] 0x%016lx\n", depth, stk->rip); // NOLINT
        }
        
        printf("%s", line_buf);
        dbg_direct(line_buf);
        
        stk = stk->rbp;
        depth++;
    }
    printf("------------------------\n");
    dbg_direct("------------------------\n");
}

void print_stack_trace() {
    uint64_t current_rbp, current_rip;
    __asm__ volatile("mov %%rbp, %0" : "=r"(current_rbp));
    
    if (current_rbp < 4096 || (current_rbp & 7) != 0) {
        printf(" [!] Stack trace unavailable (Invalid RBP: 0x%016lx).\n", current_rbp);
        dbg_direct(" [!] Stack trace unavailable (Invalid RBP).\n");
        return;
    }

    current_rip = ((uint64_t*)current_rbp)[1]; 
    print_stack_trace_from(current_rbp, current_rip);
}

void kernel_warning(const char* file, int line, const char* msg) {
    uint64_t flags = spinlock_acquire(&debug_lock);
    console_set_color(CONSOLE_COLOR_YELLOW, CONSOLE_COLOR_BLACK); 
    printf("\n[WARNING] %s\n", msg);
    printf("Location: %s:%d\n", file, line);
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK); 
    spinlock_release(&debug_lock, flags);
    print_stack_trace();
}

void klog_hex(const void* ptr, size_t size) {
    const uint8_t* p = (const uint8_t*)ptr;
    char ascii[17];
    printf("\n--- Memory Dump at 0x%p (%lu bytes) ---\n", ptr, size);
    for (size_t i = 0; i < size; ++i) {
        if (i % 16 == 0) {
            if (i != 0) { printf("  %s\n", ascii); }
            printf("%04lx: ", i);
        }
        printf("%02x ", p[i]);
        if (p[i] >= 32 && p[i] <= 126) { ascii[i % 16] = p[i]; }
        else { ascii[i % 16] = '.'; }
        ascii[(i % 16) + 1] = '\0';
    }
    size_t remainder = size % 16;
    if (remainder != 0) {
        for (size_t i = 0; i < 16 - remainder; i++) { printf("   "); }
        printf("  %s\n", ascii);
    } else {
        printf("  %s\n", ascii);
    }
    printf("------------------------------------------\n");
}

void klog(log_level_t level, const char* fmt, ...) {
    uint64_t flags = spinlock_acquire(&debug_lock);

    char time_buf[16];
    rtc_get_formatted_time(time_buf);
    
    const char* level_str;
    console_color_t color;
    
    switch (level) {
        case LOG_INFO:    level_str = "INFO"; color = CONSOLE_COLOR_LIGHT_GREY; break;
        case LOG_WARN:    level_str = "WARN"; color = CONSOLE_COLOR_YELLOW; break;
        case LOG_ERROR:   level_str = "ERR "; color = CONSOLE_COLOR_LIGHT_RED; break;
        case LOG_DEBUG:   level_str = "DBG "; color = CONSOLE_COLOR_DARK_GREY; break;
        case LOG_SUCCESS: level_str = " OK "; color = CONSOLE_COLOR_LIGHT_GREEN; break;
        case LOG_CRITICAL:level_str = "CRIT"; color = CONSOLE_COLOR_LIGHT_RED; break;
        default:          level_str = "LOG "; color = CONSOLE_COLOR_WHITE; break;
    }

    va_list args;
    va_start(args, fmt);
    vsnprintf(debug_fmt_buf, sizeof(debug_fmt_buf), fmt, args); // NOLINT
    va_end(args);

    char temp_log[1100];
    snprintf(temp_log, sizeof(temp_log), "[%s][%s] %s\n", time_buf, level_str, debug_fmt_buf); // NOLINT
    klog_append(temp_log);

    console_set_color(CONSOLE_COLOR_LIGHT_BLUE, CONSOLE_COLOR_BLACK);
    printf("[%s] ", time_buf);
    console_set_color(color, CONSOLE_COLOR_BLACK);
    printf("[%s] ", level_str);
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    printf("%s\n", debug_fmt_buf);

    spinlock_release(&debug_lock, flags);
}

void klog_buffer_dump() {
    printf("\n--- Kernel Log Buffer Dump ---\n");
    size_t i = klog_full ? klog_tail : 0;
    while (i != klog_head) {
        printf("%c", klog_buffer[i]);
        i = (i + 1) % KLOG_BUF_SIZE;
    }
    printf("\n------------------------------\n");
}

void klog_buffer_dump_serial() {
    dbg_direct("\n--- Kernel Log Buffer Dump ---\n");
    size_t i = klog_full ? klog_tail : 0;
    while (i != klog_head) {
        dbg_putc(klog_buffer[i]);
        i = (i + 1) % KLOG_BUF_SIZE;
    }
    dbg_direct("\n------------------------------\n");
}

void export_crashdump_to_nvram() {
    if (!uefi_available()) {
        printf("\n[DUMP] UEFI Runtime Services not available. Cannot write to NVRAM.\n");
        return;
    }

    static char crash_buf[2048];
    int pos = 0;
    pos += snprintf(crash_buf + pos, 2048 - pos, "--- %s CRASHDUMP ---\n", SINGULARITY_SYS_NAME); // NOLINT
    
    size_t i = klog_full ? klog_tail : 0;
    while (i != klog_head && pos < 2000) {
        crash_buf[pos++] = klog_buffer[i];
        i = (i + 1) % KLOG_BUF_SIZE;
    }
    crash_buf[pos] = '\0';

    efi_guid_t guid = { 0x11223344, 0x5566, 0x7788, {0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00} };
    uint32_t attrs = EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS;

    int ret = uefi_set_variable("CrashDump", &guid, attrs, pos, crash_buf);
    if (ret) {
        printf("\n[DUMP] Crash dump successfully saved to NVRAM!\n");
    } else {
        printf("\n[DUMP] Failed to save crash dump to NVRAM.\n");
    }
}

typedef struct {
    uint64_t addr;
    int size;
    int type;
    bool active;
} watchpoint_t;

static watchpoint_t global_watchpoints[4] = {0};
static spinlock_t wp_lock = {0, 0, {0}};

static void update_local_dr() {
    uint64_t dr7 = 0;
    dr7 |= (1 << 10);

    for (int i = 0; i < 4; i++) {
        if (global_watchpoints[i].active) {
            switch(i) {
                case 0: __asm__ volatile("mov %0, %%dr0" :: "r"(global_watchpoints[i].addr)); break;
                case 1: __asm__ volatile("mov %0, %%dr1" :: "r"(global_watchpoints[i].addr)); break;
                case 2: __asm__ volatile("mov %0, %%dr2" :: "r"(global_watchpoints[i].addr)); break;
                case 3: __asm__ volatile("mov %0, %%dr3" :: "r"(global_watchpoints[i].addr)); break;
            }
            
            dr7 |= (1 << (i * 2 + 1));
            
            uint64_t type_val = (global_watchpoints[i].type == 3) ? 3 : 1; 
            dr7 |= (type_val << (16 + i * 4));
            
            uint64_t len_val = 0;
            switch(global_watchpoints[i].size) {
                case 1: len_val = 0; break;
                case 2: len_val = 1; break;
                case 4: len_val = 3; break;
                case 8: len_val = 2; break;
                default: len_val = 2; break;
            }
            dr7 |= (len_val << (18 + i * 4));
        } else {
            switch(i) {
                case 0: __asm__ volatile("mov %0, %%dr0" :: "r"(0ULL)); break;
                case 1: __asm__ volatile("mov %0, %%dr1" :: "r"(0ULL)); break;
                case 2: __asm__ volatile("mov %0, %%dr2" :: "r"(0ULL)); break;
                case 3: __asm__ volatile("mov %0, %%dr3" :: "r"(0ULL)); break;
            }
        }
    }
    __asm__ volatile("mov %0, %%dr7" :: "r"(dr7));
}

void hw_watchpoint_sync_handler(registers_t* regs) {
    (void)regs;
    update_local_dr();
}

int hw_watchpoint_set(uint64_t addr, int size, int type) {
    uint64_t flags = spinlock_acquire(&wp_lock);
    int idx = -1;
    for (int i = 0; i < 4; i++) {
        if (!global_watchpoints[i].active) {
            idx = i;
            break;
        }
    }
    if (idx != -1) {
        global_watchpoints[idx].addr = addr;
        global_watchpoints[idx].size = size;
        global_watchpoints[idx].type = type;
        global_watchpoints[idx].active = true;
    }
    spinlock_release(&wp_lock, flags);
    
    if (idx != -1) {
        update_local_dr(); 
        extern uint8_t num_cpus;
        if (num_cpus > 1) {
            apic_broadcast_ipi(254);
        }
    }
    return idx;
}

void hw_watchpoint_clear(int index) {
    if (index < 0 || index > 3) return;
    uint64_t flags = spinlock_acquire(&wp_lock);
    global_watchpoints[index].active = false;
    spinlock_release(&wp_lock, flags);
    
    update_local_dr();
    extern uint8_t num_cpus;
    if (num_cpus > 1) {
        apic_broadcast_ipi(254);
    }
}
