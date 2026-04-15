// system/console/console.h
#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void console_init();

void console_putchar(char c);
void console_write(const char* str);
void console_set_color(uint8_t fg, uint8_t bg);
void console_clear();

void console_write_noflush(const char* str);
void console_flush_if_needed();
void console_force_unlock();

void console_set_auto_flush(bool enabled);
void console_scroll_up();
void console_scroll_down();
void console_blink_cursor(bool state);

void gop_draw_string(int x, int y, const char* str, uint32_t color);

#ifdef __cplusplus
}
#endif

#endif