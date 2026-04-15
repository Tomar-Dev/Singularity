// libc/stdio.h
#ifndef STDIO_H
#define STDIO_H

#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h> 

#ifdef __cplusplus
extern "C" {
#endif

void printf(const char* format, ...);
void vprintf(const char* format, va_list args);
int sprintf(char* str, const char* format, ...);
int snprintf(char* str, size_t size, const char* format, ...);
int vsnprintf(char* str, size_t size, const char* format, va_list args);

// FIX: Global I/O Lock (GIOL) API'leri eklendi
uint64_t stdio_acquire_lock(void);
void stdio_release_lock(uint64_t flags);

void stdio_force_unlock();
void stdio_set_buffering(bool enabled);
void stdio_flush();
void vga_set_color(uint8_t fg, uint8_t bg);
void set_color_rgb(uint32_t fg, uint32_t bg);

#ifdef __cplusplus
}
#endif

#endif