// libc/stdio.c
#include "libc/stdio.h"
#include "drivers/video/framebuffer.h"
#include "system/console/console.h"
#include "archs/cpu/cpu_hal.h"
#include "libc/string.h"
#include "archs/cpu/x86_64/sync/spinlock.h"
#include "system/process/process.h"
#include "kernel/fastops.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>

static spinlock_t screen_lock = {0, 0, {0}};

#define SERIAL_BUF_SIZE 1024
static char serial_line_buf[SERIAL_BUF_SIZE];
static int serial_buf_pos = 0;
static bool serial_buffering_enabled = true; 

extern volatile bool global_panic_active;
extern uint64_t safe_div64(uint64_t dividend, uint64_t divisor);

typedef struct {
    char* buffer;
    size_t size;
    size_t written;
} print_ctx_t;

uint64_t stdio_acquire_lock(void) {
    if (global_panic_active) {
        return 0;
    } else {
        return spinlock_acquire(&screen_lock);
    }
}

void stdio_release_lock(uint64_t flags) {
    if (!global_panic_active) {
        spinlock_release(&screen_lock, flags);
    } else {
        // Skip lock operation during panic
    }
}

static void print_char(char c, print_ctx_t* ctx) {
    if (ctx->size > 0 && ctx->written < ctx->size - 1) {
        ctx->buffer[ctx->written] = c;
    } else {
        // Buffer is full, drop character
    }
    ctx->written++;
}

static void print_string(const char* s, int width, bool left_align, print_ctx_t* ctx) {
    if (!s) {
        s = "(null)";
    } else {
        // Valid string
    }
    int len = strlen(s);
    int pad = (width > len) ? (width - len) : 0;
    if (!left_align) {
        while (pad-- > 0) print_char(' ', ctx);
    } else {
        // Padding will be added after string
    }
    while (*s) {
        print_char(*s++, ctx);
    }
    if (left_align) {
        while (pad-- > 0) print_char(' ', ctx);
    } else {
        // Padding already added before string
    }
}

static void print_number(uint64_t n, int base, bool is_signed, bool uppercase, int width, char padChar, print_ctx_t* ctx) {
    char buffer[128];
    int i = 0;
    bool is_neg = false;
    
    if (is_signed && (int64_t)n < 0) { 
        is_neg = true; 
        n = (uint64_t)(-(int64_t)n); 
    } else {
        // Number is positive or unsigned
    }
    
    if (n == 0) {
        buffer[i++] = '0';
    } else {
        while (n != 0) {
            uint64_t quot = safe_div64(n, (uint64_t)base);
            uint64_t rem = n - (quot * base);
            
            if (rem > 9) {
                if (uppercase) {
                    buffer[i++] = (rem - 10) + 'A';
                } else {
                    buffer[i++] = (rem - 10) + 'a';
                }
            } else {
                buffer[i++] = rem + '0';
            }
            n = quot;
            if (i >= 127) {
                break;
            } else {
                // Continue parsing number
            }
        }
    }
    
    int len = i + (is_neg ? 1 : 0);
    int padding = (width > len) ? (width - len) : 0;
    
    if (is_neg && padChar == '0') { 
        print_char('-', ctx); 
        is_neg = false; 
    } else {
        // Handle negative sign later
    }
    
    while (padding-- > 0) {
        print_char(padChar, ctx);
    }
    
    if (is_neg) {
        print_char('-', ctx);
    } else {
        // Sign already handled or positive
    }
    
    while (i > 0) {
        print_char(buffer[--i], ctx);
    }
}

static void print_double(double val, int width, int precision, print_ctx_t* ctx) {
    char buffer[128];
    int len = 0;
    if (precision < 0) {
        precision = 2;
    } else if (precision > 20) {
        precision = 20; 
    } else {
        // Valid precision
    }

    if (val < 0) { 
        buffer[len++] = '-'; 
        val = -val; 
    } else {
        // Positive float
    }
    
    uint64_t int_part = (uint64_t)val;
    double remainder = val - (double)int_part;
    
    char temp_buf[64];
    int t = 0;
    if (int_part == 0) {
        temp_buf[t++] = '0';
    } else {
        while (int_part != 0) {
            temp_buf[t++] = (int_part % 10) + '0';
            int_part /= 10;
            if (t >= 63) {
                break; 
            } else {
                // Continue parsing float integer part
            }
        }
    }
    
    while (t > 0) {
        if (len < 120) {
            buffer[len++] = temp_buf[--t];
        } else { 
            break; 
        }
    }

    if (precision > 0 && len < 120) {
        buffer[len++] = '.';
        while (precision-- > 0 && len < 126) {
            remainder *= 10.0;
            int digit = (int)remainder;
            if (digit > 9) {
                digit = 9; 
            } else {
                // Valid digit
            }
            buffer[len++] = digit + '0';
            remainder -= digit;
        }
    } else {
        // No precision requested
    }
    
    int padding = (width > len) ? (width - len) : 0;
    while (padding-- > 0) {
        print_char(' ', ctx);
    }
    for (int k = 0; k < len; k++) {
        print_char(buffer[k], ctx);
    }
}

static void do_printf_internal(print_ctx_t* ctx, const char* format, va_list args) {
    for (int i = 0; format[i] != '\0'; i++) {
        if (format[i] != '%') { 
            print_char(format[i], ctx); 
            continue; 
        } else {
            // Processing format specifier
        }
        
        i++; 
        char padChar = ' ';
        int width = 0;
        int precision = -1;
        bool left_align = false;
        
        while (format[i] == '-' || format[i] == '0') {
            if (format[i] == '-') {
                left_align = true;
            } else if (format[i] == '0') {
                padChar = '0';
            } else {
                // Should not reach here
            }
            i++;
        }
        while (format[i] >= '0' && format[i] <= '9') {
            width = width * 10 + (format[i] - '0');
            i++;
        }
        if (format[i] == '.') {
            i++; 
            precision = 0;
            while (format[i] >= '0' && format[i] <= '9') {
                precision = precision * 10 + (format[i] - '0');
                i++;
            }
        } else {
            // No precision specified
        }
        while (format[i] == 'l') {
            i++; 
        }

        switch (format[i]) {
            case 'c': print_char((char)va_arg(args, int), ctx); break;
            case 's': print_string(va_arg(args, const char*), width, left_align, ctx); break;
            case 'd': 
            case 'i': print_number((uint64_t)va_arg(args, long long), 10, true, false, width, padChar, ctx); break;
            case 'u': print_number((uint64_t)va_arg(args, unsigned long long), 10, false, false, width, padChar, ctx); break;
            case 'x': print_number((uint64_t)va_arg(args, unsigned long long), 16, false, false, width, padChar, ctx); break;
            case 'X': print_number((uint64_t)va_arg(args, unsigned long long), 16, false, true, width, padChar, ctx); break;
            case 'p': print_string("0x", 0, false, ctx); print_number((uint64_t)va_arg(args, void*), 16, false, false, width, padChar, ctx); break;
            case 'f': print_double(va_arg(args, double), width, precision, ctx); break;
            case '%': print_char('%', ctx); break;
            default:  print_char('%', ctx); print_char(format[i], ctx); break;
        }
    }
    
    if (ctx->size > 0) {
        if (ctx->written < ctx->size) {
            ctx->buffer[ctx->written] = '\0';
        } else { 
            ctx->buffer[ctx->size - 1] = '\0';
        }
    } else {
        // Context has no buffer limit
    }
}

static void flush_serial_buffer_locked() {
    if (serial_buf_pos > 0) {
        serial_line_buf[serial_buf_pos] = 0;
        serial_write(serial_line_buf);
        serial_buf_pos = 0;
    } else {
        // Nothing to flush
    }
}

static void serial_write_buffered(const char* str) {
    if (!serial_buffering_enabled) {
        serial_write(str);
        return;
    } else {
        // Proceed with buffering
    }

    while (*str) {
        char c = *str++;
        
        if (serial_buf_pos < SERIAL_BUF_SIZE - 2) { 
            if (c == '\n') {
                serial_line_buf[serial_buf_pos++] = '\r';
                serial_line_buf[serial_buf_pos++] = '\n';
                flush_serial_buffer_locked();
            } else {
                serial_line_buf[serial_buf_pos++] = c;
            }
        } else {
            flush_serial_buffer_locked();
            serial_line_buf[serial_buf_pos++] = c;
        }
    }
}

void vprintf(const char* format, va_list args) {
    char local_buf[1024]; 
    print_ctx_t ctx = {local_buf, sizeof(local_buf), 0};
    
    do_printf_internal(&ctx, format, args);
    
    uint8_t cpu_id = get_cpu_id_fast();
    process_t* curr = current_process[cpu_id];
    
    bool is_silent = false;
    if (!global_panic_active && curr && (curr->flags & PROC_FLAG_SILENT)) {
        is_silent = true;
    } else {
        is_silent = false;
    }
    
    uint64_t flags = stdio_acquire_lock();
    
    if (!is_silent) {
        console_write_noflush(local_buf);
    } else {
        // Suppress console output for silent tasks
    }
    
    serial_write_buffered(local_buf); 
    
    stdio_release_lock(flags);
    
    if (!is_silent) {
        console_flush_if_needed();
    } else {
        // Do not trigger flush for silent tasks
    }
}

void printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

int vsnprintf(char* str, size_t size, const char* format, va_list args) {
    print_ctx_t ctx = {str, size, 0};
    do_printf_internal(&ctx, format, args);
    return ctx.written;
}

int snprintf(char* str, size_t size, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(str, size, format, args);
    va_end(args);
    return ret;
}

int sprintf(char* str, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(str, 4096, format, args);
    va_end(args);
    return ret;
}

void set_color_rgb(uint32_t fg, uint32_t bg) {
    (void)fg; (void)bg;
    // Reserved for TrueColor support
}

void stdio_set_buffering(bool enabled) {
    uint64_t flags = stdio_acquire_lock();
    if (!enabled) {
        flush_serial_buffer_locked();
    } else {
        // Enabling buffering, nothing to flush
    }
    serial_buffering_enabled = enabled;
    stdio_release_lock(flags);
}

void stdio_flush() {
    uint64_t flags = stdio_acquire_lock();
    flush_serial_buffer_locked();
    stdio_release_lock(flags);
}

void stdio_force_unlock() {
    spinlock_init(&screen_lock);
}

void kprintf_string(const char* str) {
    uint8_t cpu_id = get_cpu_id_fast();
    process_t* curr = current_process[cpu_id];
    
    bool is_silent = false;
    if (!global_panic_active && curr && (curr->flags & PROC_FLAG_SILENT)) {
        is_silent = true;
    } else {
        is_silent = false;
    }
    
    uint64_t flags = stdio_acquire_lock();
    
    if (!is_silent) {
        console_write_noflush(str);
    } else {
        // Suppressed
    }
    
    serial_write_buffered(str); 
    
    stdio_release_lock(flags);
    
    if (!is_silent) {
        console_flush_if_needed();
    } else {
        // Suppressed
    }
}