// libc/string.h
#ifndef STRING_H
#define STRING_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void itoa(uint64_t n, char *str, int base);

size_t strlen(const char* str); 
int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, size_t n);
int memcmp(const void* ptr1, const void* ptr2, size_t num);
void* memcpy(void* dest, const void* src, size_t num);
void* memset(void* ptr, int value, size_t num);
void* memmove(void* dest, const void* src, size_t num);
char* strstr(const char* haystack, const char* needle);
char* strcpy(char* dest, const char* src);
char* strcat(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
char* strncat(char* dest, const char* src, size_t n);
size_t strnlen(const char *s, size_t maxlen);
char* strrchr(const char* str, int ch); 
char* strpbrk(const char *s, const char *accept);
void explicit_bzero(void* ptr, size_t len);

char* strchr(const char* str, int ch);

void* memchr(const void* ptr, int ch, size_t count);
uint32_t compute_crc32(uint32_t crc, const void* buf, size_t size);

void memzero_nt_avx(void* dest, size_t count);
void memcpy_nt(void* dest, const void* src, size_t count); 

#ifdef __cplusplus
}
#endif

#endif
