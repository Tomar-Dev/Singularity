// system/console/console.cpp
#include "system/console/console.h"
#include "drivers/video/framebuffer.h"
#include "drivers/video/font.h"
#include "archs/cpu/x86_64/core/cpuid.h"
#include "libc/string.h"
#include "libc/stdio.h"
#include "archs/cpu/x86_64/sync/spinlock.h"
#include "system/process/process.h"
#include "memory/kheap.h" 

extern "C" {
    void draw_char_row_avx(void* dest, uint32_t fg, uint32_t bg, uint8_t font_bits);
    void framebuffer_flush();
    void framebuffer_mark_dirty_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    void fill_rect_avx(uint32_t* buffer, uint32_t color, size_t pixel_count);
}

#define HISTORY_LINES 500
#define MAX_COLS 200 
#define GLYPH_W 8
#define GLYPH_H 16

static const uint32_t console_palette[16] = {
    0xFF000000, 0xFF0000AA, 0xFF00AA00, 0xFF00AAAA,
    0xFFAA0000, 0xFFAA00AA, 0xFFAA5500, 0xFFAAAAAA,
    0xFF555555, 0xFF5555FF, 0xFF55FF55, 0xFF55FFFF,
    0xFFFF5555, 0xFFFF55FF, 0xFFFFFF55, 0xFFFFFFFF
};

typedef struct {
    char c;
    uint8_t fg_idx;
    uint8_t bg_idx;
} __attribute__((packed)) ConsoleChar;

static ConsoleChar (*history_buffer)[MAX_COLS] = nullptr;

static uint32_t cursor_x = 0;
static uint32_t cursor_y = 0; 
static uint32_t head_idx = 0; 
static uint32_t scroll_offset = 0; 
static bool buffer_wrapped = false; 

static uint8_t current_fg_idx = 15;
static uint8_t current_bg_idx = 0;

static uint32_t chars_per_line = 0;
static uint32_t lines_per_screen = 0;

static bool cursor_visible = true;
static bool auto_flush_enabled = true; 
static volatile bool console_dirty = false;

spinlock_t console_lock = {0, 0, {0}};
extern volatile bool global_panic_active; 

static void draw_char_at_fast(char c, uint32_t x, uint32_t y, uint8_t fg_idx, uint8_t bg_idx) {
    if (!backbuffer) { 
        return; 
    } else {
        // Valid
    }
    uint8_t uc = (uint8_t)c;
    const uint8_t* glyph = &font8x16[uc * 16];
    uint32_t fg = console_palette[fg_idx & 0x0F];
    uint32_t bg = console_palette[bg_idx & 0x0F];
    uint8_t* base_addr = (uint8_t*)backbuffer + (y * fb_info.pitch) + (x * 4);
    uint32_t pitch = fb_info.pitch;

    if (cpu_info.has_avx2) {
        for (int i = 0; i < 16; i++) { 
            draw_char_row_avx((void*)(base_addr + (i * pitch)), fg, bg, glyph[i]); 
        }
    } else {
        for (int i = 0; i < 16; i++) {
            uint32_t* row = (uint32_t*)(base_addr + (i * pitch));
            uint8_t bits = glyph[i]; 
            for (int j = 0; j < 8; j++) {
                uint32_t mask = (bits & (0x80 >> j)) ? 0xFFFFFFFF : 0;
                row[j] = (fg & mask) | (bg & ~mask);
            }
        }
    }
}

static void draw_history_line(int logical_idx, uint32_t screen_y) {
    int buffer_idx = logical_idx;
    while (buffer_idx < 0) { 
        buffer_idx += HISTORY_LINES; 
    }
    buffer_idx %= HISTORY_LINES;

    bool is_empty = false;
    if (!buffer_wrapped && logical_idx > (int)head_idx) { 
        is_empty = true; 
    } else {
        // Present
    }
    if (!buffer_wrapped && logical_idx < 0) { 
        is_empty = true; 
    } else {
        // Present
    }
    
    uint32_t y_px = screen_y * GLYPH_H;
    for (uint32_t x = 0; x < chars_per_line; x++) {
        uint32_t x_px = x * GLYPH_W;
        if (is_empty) {
            draw_char_at_fast(' ', x_px, y_px, current_fg_idx, current_bg_idx);
        } else {
            ConsoleChar* ch = &history_buffer[buffer_idx][x];
            char c = (ch->c == 0) ? ' ' : ch->c;
            draw_char_at_fast(c, x_px, y_px, ch->fg_idx, ch->bg_idx);
        }
    }
}

static void console_redraw() {
    if (!history_buffer) { 
        return; 
    } else {
        // Proceed
    }
    int top_line_logical = (int)head_idx - (int)(lines_per_screen - 1) - (int)scroll_offset;
    for (uint32_t y = 0; y < lines_per_screen; y++) { 
        draw_history_line(top_line_logical + y, y); 
    }
    framebuffer_mark_dirty_rect(0, 0, fb_info.width, fb_info.height);
}

static void erase_cursor() {
    if (!backbuffer || !history_buffer) { 
        return; 
    } else {
        // Proceed
    }
    char c = history_buffer[head_idx][cursor_x].c;
    uint8_t fg = history_buffer[head_idx][cursor_x].fg_idx;
    uint8_t bg = history_buffer[head_idx][cursor_x].bg_idx;
    if (c == 0) { 
        c = ' '; 
    } else {
        // Existent char
    }
    if (scroll_offset == 0) {
        draw_char_at_fast(c, cursor_x * GLYPH_W, cursor_y * GLYPH_H, fg, bg);
        framebuffer_mark_dirty_rect(cursor_x * GLYPH_W, cursor_y * GLYPH_H, GLYPH_W, GLYPH_H);
    } else {
        // Do not erase visual cursor if scrolling
    }
}

static void draw_cursor() {
    if (!backbuffer || !cursor_visible || scroll_offset != 0 || !history_buffer) { 
        return; 
    } else {
        // Render mode
    }
    uint32_t x_px = cursor_x * GLYPH_W;
    uint32_t y_px = cursor_y * GLYPH_H;
    uint32_t color = console_palette[current_fg_idx & 0x0F]; 
    uint8_t* base_addr = (uint8_t*)backbuffer + (y_px * fb_info.pitch) + (x_px * 4);
    uint32_t pitch = fb_info.pitch;

    for (int i = 13; i < 16; i++) { 
        uint32_t* row = (uint32_t*)(base_addr + (i * pitch));
        for (int j = 0; j < 8; j++) { 
            row[j] = color; 
        }
    }
    framebuffer_mark_dirty_rect(x_px, y_px + 13, GLYPH_W, 3);
}

static void console_newline() {
    if (!history_buffer) { 
        return; 
    } else {
        // Proceed
    }
    head_idx++;
    if (head_idx >= HISTORY_LINES) { 
        head_idx = 0; 
        buffer_wrapped = true; 
    } else {
        // Linear flow
    }
    
    for (int j = 0; j < MAX_COLS; j++) {
        history_buffer[head_idx][j].c = 0;
        history_buffer[head_idx][j].fg_idx = current_fg_idx;
        history_buffer[head_idx][j].bg_idx = current_bg_idx;
    }
    cursor_x = 0;

    if (scroll_offset == 0) {
        if (cursor_y < lines_per_screen - 1) {
            cursor_y++;
        } else {
            // KLASİK YAMA: Satır satır kaydırma (Line-by-Line Scroll) geri getirildi.
            if (auto_flush_enabled && backbuffer) {
                console_redraw();
                console_dirty = true;
            } else {
                // Manual flush triggered later
            }
        }
    } else {
        scroll_offset++;
        uint32_t max_history = buffer_wrapped ? HISTORY_LINES : head_idx;
        if (scroll_offset > max_history - lines_per_screen) { 
            scroll_offset = max_history - lines_per_screen; 
        } else {
            // Keep bounds
        }
        if (auto_flush_enabled) {
            console_redraw();
            console_dirty = true;
        } else {
            // Unflushed
        }
    }
    if (auto_flush_enabled) { 
        draw_cursor(); 
    } else {
        // Blocked
    }
}

static void console_putchar_internal(char c, uint8_t fg, uint8_t bg) {
    if (!backbuffer || !history_buffer) { 
        return; 
    } else {
        // Valid
    }

    if (scroll_offset != 0) {
        scroll_offset = 0;
        if (auto_flush_enabled) {
            console_redraw();
            console_dirty = true;
        } else {
            // Queued
        }
    } else {
        // Aligned at bottom
    }

    if (c == '\n') {
        if (auto_flush_enabled) { 
            erase_cursor(); 
        } else {
            // Hidden
        }
        console_newline();
    } else if (c == '\b') {
        if (auto_flush_enabled) { 
            erase_cursor(); 
        } else {
            // Hidden
        }
        if (cursor_x > 0) {
            cursor_x--;
            history_buffer[head_idx][cursor_x].c = 0;
            if (auto_flush_enabled) {
                draw_char_at_fast(' ', cursor_x * GLYPH_W, cursor_y * GLYPH_H, fg, bg);
                framebuffer_mark_dirty_rect(cursor_x * GLYPH_W, cursor_y * GLYPH_H, GLYPH_W, GLYPH_H);
                console_dirty = true;
            } else {
                // Pending rendering cycle
            }
        } else if (cursor_y > 0 || (scroll_offset == 0 && head_idx > 0)) {
            if (cursor_y > 0) { 
                cursor_y--; 
            } else {
                // Top
            }
            if (head_idx == 0) { 
                head_idx = HISTORY_LINES - 1; 
            } else { 
                head_idx--; 
            }
            
            int last_char = chars_per_line - 1;
            while (last_char >= 0 && history_buffer[head_idx][last_char].c == 0) { 
                last_char--; 
            }
            cursor_x = (last_char < (int)chars_per_line - 1) ? last_char + 1 : chars_per_line - 1;
            if (cursor_x < 0) { 
                cursor_x = 0; 
            } else {
                // Validated
            }

            history_buffer[head_idx][cursor_x].c = 0;
            if (auto_flush_enabled) {
                draw_char_at_fast(' ', cursor_x * GLYPH_W, cursor_y * GLYPH_H, fg, bg);
                framebuffer_mark_dirty_rect(cursor_x * GLYPH_W, cursor_y * GLYPH_H, GLYPH_W, GLYPH_H);
                console_dirty = true;
            } else {
                // Ignored till batch
            }
        } else {
            // Hard bounds hit
        }
        if (auto_flush_enabled) { 
            draw_cursor(); 
        } else {
            // Ignored
        }
    } else if (c == '\t') { 
        cursor_x = (cursor_x + 4) & ~3; 
        if(auto_flush_enabled) { 
            draw_cursor(); 
        } else {
            // Ignored
        }
    } else if (c == '\r') {
        if (auto_flush_enabled) { 
            erase_cursor(); 
        } else {
            // Ignored
        }
        cursor_x = 0;
        if (auto_flush_enabled) { 
            draw_cursor(); 
        } else {
            // Ignored
        }
    } else {
        if (cursor_x >= chars_per_line) {
            if (auto_flush_enabled) { 
                erase_cursor(); 
            } else {
                // Ignored
            }
            console_newline();
        } else {
            // Buffer valid
        }
        
        history_buffer[head_idx][cursor_x].c = c;
        history_buffer[head_idx][cursor_x].fg_idx = fg;
        history_buffer[head_idx][cursor_x].bg_idx = bg;
        
        if (auto_flush_enabled) {
            draw_char_at_fast(c, cursor_x * GLYPH_W, cursor_y * GLYPH_H, fg, bg);
            framebuffer_mark_dirty_rect(cursor_x * GLYPH_W, cursor_y * GLYPH_H, GLYPH_W, GLYPH_H);
            console_dirty = true;
        } else {
            // Deferred tile update
        }
        cursor_x++;
        if (auto_flush_enabled) { 
            draw_cursor(); 
        } else {
            // Wait for flush
        }
    }
}

extern "C" {

void console_init() {
    if (!backbuffer) { 
        return; 
    } else {
        // Framebuffer Ready
    } 
    if (!history_buffer) {
        history_buffer = (ConsoleChar(*)[MAX_COLS])kmalloc(HISTORY_LINES * MAX_COLS * sizeof(ConsoleChar));
        if (!history_buffer) { 
            return; 
        } else {
            // Memory Acquired
        }
    } else {
        // Pre-initialized
    }
    
    spinlock_init(&console_lock);

    chars_per_line = fb_info.width / GLYPH_W;
    if (chars_per_line > MAX_COLS) { 
        chars_per_line = MAX_COLS; 
    } else {
        // Matched Size
    }
    lines_per_screen = fb_info.height / GLYPH_H;
    
    current_fg_idx = 15; current_bg_idx = 0;  
    
    clear_screen(console_palette[current_bg_idx]);
    
    cursor_x = 0; cursor_y = 0; head_idx = 0;
    scroll_offset = 0; buffer_wrapped = false;
    
    for (int i = 0; i < HISTORY_LINES; i++) {
        for (int j = 0; j < MAX_COLS; j++) {
            history_buffer[i][j].c = 0;
            history_buffer[i][j].fg_idx = current_fg_idx;
            history_buffer[i][j].bg_idx = current_bg_idx;
        }
    }
    draw_cursor();
}

void console_set_auto_flush(bool enabled) {
    uint64_t flags = 0;
    if (!global_panic_active) { 
        flags = spinlock_acquire(&console_lock); 
    } else {
        // Raw mode
    }
    
    bool was_disabled = !auto_flush_enabled;
    auto_flush_enabled = enabled;
    
    if (enabled && was_disabled) {
        // GÖRSEL YAMA: Auto-flush tekrar açıldığında, arka planda biriken history_buffer'ın
        // ekrana (backbuffer'a) yansıtılması için console_redraw() ÇAĞRILMAK ZORUNDADIR.
        // Bu çağrı "garbled text" (bozuk yazı) hatasını %100 çözer.
        console_redraw(); 
        draw_cursor(); 
        console_dirty = true;
    } else {
        // Keep state
    }
    
    if (!global_panic_active) { 
        spinlock_release(&console_lock, flags); 
    } else {
        // Escaped lock
    }
    
    if (enabled && was_disabled) { 
        framebuffer_flush(); 
        console_dirty = false; 
    } else {
        // Wait
    }
}

bool console_get_auto_flush() {
    return auto_flush_enabled;
}

void console_write_noflush(const char* str) {
    uint64_t flags = 0;
    if (!global_panic_active) { 
        flags = spinlock_acquire(&console_lock); 
    } else {
        // Unlocked bypass
    }
    
    while (*str) {
        console_putchar_internal(*str++, current_fg_idx, current_bg_idx);
    }
    console_dirty = true;

    if (!global_panic_active) { 
        spinlock_release(&console_lock, flags); 
    } else {
        // Unlocked bypass
    }
}

void console_putchar(char c) {
    uint64_t flags = 0;
    if (!global_panic_active) { 
        flags = spinlock_acquire(&console_lock); 
    } else {
        // Unlocked bypass
    }
    
    console_putchar_internal(c, current_fg_idx, current_bg_idx);
    console_dirty = true;

    if (!global_panic_active) { 
        spinlock_release(&console_lock, flags); 
    } else {
        // Unlocked bypass
    }
}

void console_write(const char* str) {
    console_write_noflush(str);
}

void console_flush_if_needed() {
    __asm__ volatile("mfence" ::: "memory");
    if (console_dirty && auto_flush_enabled) {
        framebuffer_flush();
        console_dirty = false;
    } else {
        // Skipped
    }
}

void console_set_color(console_color_t fg, console_color_t bg) {
    current_fg_idx = (uint8_t)fg; 
    current_bg_idx = (uint8_t)bg;
}

void console_clear() {
    uint64_t flags = 0;
    if (!global_panic_active) { 
        flags = spinlock_acquire(&console_lock); 
    } else {
        // Bypassed
    }
    
    clear_screen(console_palette[current_bg_idx & 0x0F]);
    cursor_x = 0; cursor_y = 0; head_idx = 0;
    scroll_offset = 0; buffer_wrapped = false;
    
    if (history_buffer) {
        for (int i = 0; i < HISTORY_LINES; i++) {
            for (int j = 0; j < MAX_COLS; j++) {
                history_buffer[i][j].c = 0;
                history_buffer[i][j].fg_idx = current_fg_idx;
                history_buffer[i][j].bg_idx = current_bg_idx;
            }
        }
    } else {
        // Missing History Array
    }
    
    draw_cursor();
    console_dirty = true;
    
    if (!global_panic_active) { 
        spinlock_release(&console_lock, flags); 
    } else {
        // Free bypass
    }
}

void console_scroll_up() {
    uint64_t flags = 0;
    if (!global_panic_active) { 
        flags = spinlock_acquire(&console_lock); 
    } else {
        // Free
    }
    
    erase_cursor();

    uint32_t total_written = buffer_wrapped ? HISTORY_LINES : (head_idx + 1);
    uint32_t max_scroll = 0;
    if (total_written > lines_per_screen) { 
        max_scroll = total_written - lines_per_screen; 
    } else {
        // Max bounds hit
    }
    
    if (scroll_offset < max_scroll) {
        scroll_offset++;
        console_redraw(); 
        console_dirty = true;
    } else {
        // Can't scroll further up
    }

    draw_cursor();
    
    if (!global_panic_active) { 
        spinlock_release(&console_lock, flags); 
    } else {
        // Bypassed
    }
    
    console_flush_if_needed(); 
}

void console_scroll_down() {
    uint64_t flags = 0;
    if (!global_panic_active) { 
        flags = spinlock_acquire(&console_lock); 
    } else {
        // Free
    }
    
    erase_cursor();

    if (scroll_offset > 0) {
        scroll_offset--;
        console_redraw(); 
        console_dirty = true;
    } else {
        // Can't scroll further down
    }
    
    draw_cursor();
    
    if (!global_panic_active) { 
        spinlock_release(&console_lock, flags); 
    } else {
        // Bypassed
    }
    
    console_flush_if_needed(); 
}

void console_blink_cursor(bool state) {
    uint64_t flags = 0;
    if (!global_panic_active) { 
        flags = spinlock_acquire(&console_lock); 
    } else {
        // Unlocked bypass
    }
    
    if (cursor_visible == state || !history_buffer) {
        if (!global_panic_active) { 
            spinlock_release(&console_lock, flags); 
        } else {
            // Safe Drop
        }
        return;
    } else {
        // Switch state
    }
    cursor_visible = state;
    
    if (scroll_offset == 0) {
        if (cursor_visible) { 
            draw_cursor(); 
        } else {
            char c = history_buffer[head_idx][cursor_x].c;
            uint8_t fg = history_buffer[head_idx][cursor_x].fg_idx;
            uint8_t bg = history_buffer[head_idx][cursor_x].bg_idx;
            if (c == 0) { 
                c = ' '; 
            } else {
                // Kept
            }
            draw_char_at_fast(c, cursor_x * GLYPH_W, cursor_y * GLYPH_H, fg, bg);
            framebuffer_mark_dirty_rect(cursor_x * GLYPH_W, cursor_y * GLYPH_H, GLYPH_W, GLYPH_H);
        }
        console_dirty = true;
    } else {
        // Cursor hidden during scroll operation
    }
    
    if (!global_panic_active) { 
        spinlock_release(&console_lock, flags); 
    } else {
        // Safe Drop
    }
    console_flush_if_needed(); 
}

void console_force_unlock() {
    spinlock_init(&console_lock);
}

}