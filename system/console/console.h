// system/console/console.h
#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CONSOLE_COLOR_BLACK = 0,
    CONSOLE_COLOR_BLUE = 1,
    CONSOLE_COLOR_GREEN = 2,
    CONSOLE_COLOR_CYAN = 3,
    CONSOLE_COLOR_RED = 4,
    CONSOLE_COLOR_MAGENTA = 5,
    CONSOLE_COLOR_BROWN = 6,
    CONSOLE_COLOR_LIGHT_GREY = 7,
    CONSOLE_COLOR_DARK_GREY = 8,
    CONSOLE_COLOR_LIGHT_BLUE = 9,
    CONSOLE_COLOR_LIGHT_GREEN = 10,
    CONSOLE_COLOR_LIGHT_CYAN = 11,
    CONSOLE_COLOR_LIGHT_RED = 12,
    CONSOLE_COLOR_LIGHT_MAGENTA = 13,
    CONSOLE_COLOR_YELLOW = 14,
    CONSOLE_COLOR_WHITE = 15
} console_color_t;

void console_init();

void console_putchar(char c);
void console_write(const char* str);
void console_set_color(console_color_t fg, console_color_t bg);
void console_clear();

void console_write_noflush(const char* str);
void console_flush_if_needed();
void console_force_unlock();

void console_set_auto_flush(bool enabled);
bool console_get_auto_flush();
void console_scroll_up();
void console_scroll_down();
void console_blink_cursor(bool state);

void gop_draw_string(int x, int y, const char* str, uint32_t color);

#ifdef __cplusplus
}

class ScopedAutoFlush {
private:
    bool old_state;
public:
    explicit ScopedAutoFlush(bool new_state) {
        old_state = console_get_auto_flush();
        console_set_auto_flush(new_state);
    }
    ~ScopedAutoFlush() {
        console_set_auto_flush(old_state);
    }
    ScopedAutoFlush(const ScopedAutoFlush&) = delete;
    ScopedAutoFlush& operator=(const ScopedAutoFlush&) = delete;
};

#endif

#endif
