// kernel/debug.c
#include "kernel/debug.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "archs/cpu/x86_64/sync/spinlock.h"
#include "drivers/serial/serial.h"
#include "kernel/ksyms.h" 
#include "drivers/uefi/uefi.h"
#include "kernel/config.h"

extern void rtc_get_formatted_time(char* buffer);

#define KLOG_BUF_SIZE 4096
static char klog_buffer[KLOG_BUF_SIZE];
static size_t klog_head = 0;
static size_t klog_tail = 0; 
static bool klog_full = false;

static char debug_fmt_buf[1024];
static spinlock_t debug_lock = {0, 0, {0}};

extern void vga_set_color(uint8_t fg, uint8_t bg);

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
        snprintf(dbg_buf, sizeof(dbg_buf), " [00] 0x%016lx <%s+%ld>\n", rip, sym, offset);
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
        
        if (stk->rip == 0) break;

        offset = 0;
        sym = ksyms_resolve_symbol(stk->rip, &offset);
        
        char line_buf[128];
        if (sym) {
            snprintf(line_buf, sizeof(line_buf), "[%02d] 0x%016lx <%s+%ld>\n", depth, stk->rip, sym, offset);
        } else {
            snprintf(line_buf, sizeof(line_buf), " [%02d] 0x%016lx\n", depth, stk->rip);
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
    vga_set_color(14, 0); 
    printf("\n[WARNING] %s\n", msg);
    printf("Location: %s:%d\n", file, line);
    vga_set_color(15, 0); 
    spinlock_release(&debug_lock, flags);
    print_stack_trace();
}

void klog_hex(const void* ptr, size_t size) {
    const uint8_t* p = (const uint8_t*)ptr;
    char ascii[17];
    printf("\n--- Memory Dump at 0x%p (%lu bytes) ---\n", ptr, size);
    for (size_t i = 0; i < size; ++i) {
        if (i % 16 == 0) {
            if (i != 0) printf("  %s\n", ascii);
            printf("%04lx: ", i);
        }
        printf("%02x ", p[i]);
        if (p[i] >= 32 && p[i] <= 126) ascii[i % 16] = p[i];
        else ascii[i % 16] = '.';
        ascii[(i % 16) + 1] = '\0';
    }
    size_t remainder = size % 16;
    if (remainder != 0) {
        for (size_t i = 0; i < 16 - remainder; i++) printf("   ");
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
    uint8_t color;
    
    switch (level) {
        case LOG_INFO:    level_str = "INFO"; color = 7; break;
        case LOG_WARN:    level_str = "WARN"; color = 14; break;
        case LOG_ERROR:   level_str = "ERR "; color = 12; break;
        case LOG_DEBUG:   level_str = "DBG "; color = 8; break;
        case LOG_SUCCESS: level_str = " OK "; color = 10; break;
        case LOG_CRITICAL:level_str = "CRIT"; color = 12; break;
        default:          level_str = "LOG "; color = 15; break;
    }

    va_list args;
    va_start(args, fmt);
    vsnprintf(debug_fmt_buf, sizeof(debug_fmt_buf), fmt, args);
    va_end(args);

    char temp_log[1100];
    snprintf(temp_log, sizeof(temp_log), "[%s][%s] %s\n", time_buf, level_str, debug_fmt_buf);
    klog_append(temp_log);

    vga_set_color(9, 0);
    printf("[%s] ", time_buf);
    vga_set_color(color, 0);
    printf("[%s] ", level_str);
    vga_set_color(15, 0);
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
    pos += snprintf(crash_buf + pos, 2048 - pos, "--- %s CRASHDUMP ---\n", SINGULARITY_SYS_NAME);
    
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