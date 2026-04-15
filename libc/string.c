// libc/string.c
// FIX: init_string_optimization() içindeki AVX seçim kriteri düzeltildi.

#include "libc/string.h"
#include <stdint.h>
#include "archs/cpu/x86_64/core/cpuid.h"
#include "drivers/serial/serial.h"
extern uint32_t g_fpu_mode;
extern uint64_t g_xsave_mask;

#define EXPORT_SYMBOL __attribute__((used)) __attribute__((noinline))

extern void*    memcpy_avx(void* dest, const void* src, size_t count);
extern void*    memset_avx(void* dest, int c, size_t count);
extern size_t   strlen_sse2(const char* str);
extern int      memcmp_sse2(const void* p1, const void* p2, size_t count);
extern void*    memchr_avx(const void* ptr, int ch, size_t count);
extern uint32_t crc32_sse42(uint32_t crc, const void* buf, size_t size);
extern void     memzero_nt_avx_asm(void* dest, size_t count);
extern void     memcpy_nt_avx(void* dest, const void* src, size_t count);

static void* memset_generic(void* ptr, int value, size_t num) {
    uint64_t* p64 = (uint64_t*)ptr;
    uint64_t v64 = (uint8_t)value;
    v64 = (v64 * 0x0101010101010101ULL);
    while (num >= 8) { *p64++ = v64; num -= 8; }
    uint8_t* p8 = (uint8_t*)p64;
    while (num--) { *p8++ = (uint8_t)value; }
    return ptr;
}

static void* memcpy_generic(void* dest, const void* src, size_t num) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    while (num--) *d++ = *s++;
    return dest;
}

static size_t strlen_generic(const char* str) {
    size_t len = 0; while (str[len]) len++; return len;
}

static int memcmp_generic(const void* ptr1, const void* ptr2, size_t num) {
    const unsigned char* p1 = (const unsigned char*)ptr1;
    const unsigned char* p2 = (const unsigned char*)ptr2;
    for (size_t i = 0; i < num; i++) if (p1[i] != p2[i]) return p1[i] - p2[i];
    return 0;
}

static void* memchr_generic(const void* ptr, int ch, size_t count) {
    const unsigned char* p = (const unsigned char*)ptr;
    while (count--) { if (*p == (unsigned char)ch) return (void*)p; p++; }
    return NULL;
}

static uint32_t crc32_generic(uint32_t crc, const void* buf, size_t size) {
    (void)crc; (void)buf; (void)size; return 0;
}

static void memzero_nt_generic(void* dest, size_t count) {
    memset_generic(dest, 0, count);
}

static void memcpy_nt_generic(void* dest, const void* src, size_t count) {
    memcpy_generic(dest, src, count);
}

static void* memset_sse2(void* ptr, int value, size_t num) {
    uint8_t* p = (uint8_t*)ptr;
    if (num < 64) return memset_generic(ptr, value, num);
    while (((uintptr_t)p & 0xF) && num > 0) { *p++ = (uint8_t)value; num--; }
    if (value == 0) {
        size_t loops = num / 16;
        size_t remaining = num % 16;
        __asm__ volatile("pxor %%xmm0, %%xmm0" ::: "xmm0");
        while (loops >= 4) {
            __asm__ volatile(
                "movdqa %%xmm0, (%0);"
                "movdqa %%xmm0, 16(%0);"
                "movdqa %%xmm0, 32(%0);"
                "movdqa %%xmm0, 48(%0)"
                : : "r"(p) : "memory");
            p += 64; loops -= 4;
        }
        while (loops--) {
            __asm__ volatile("movdqa %%xmm0, (%0)" :: "r"(p) : "memory");
            p += 16;
        }
        while (remaining--) *p++ = 0;
    } else {
        return memset_generic(ptr, value, num);
    }
    return ptr;
}

static void* memcpy_sse2(void* dest, const void* src, size_t num) {
    if (num < 64) return memcpy_generic(dest, src, num);
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    size_t i = 0;
    for (; i + 16 <= num; i += 16) {
        __asm__ volatile(
            "movdqu (%1), %%xmm0;"
            "movdqu %%xmm0, (%0)"
            : : "r"(d + i), "r"(s + i) : "memory", "xmm0");
    }
    for (; i < num; i++) d[i] = s[i];
    return dest;
}

typedef void*    (*memset_func_t)(void*, int, size_t);
typedef void*    (*memcpy_func_t)(void*, const void*, size_t);
typedef size_t   (*strlen_func_t)(const char*);
typedef int      (*memcmp_func_t)(const void*, const void*, size_t);
typedef void*    (*memchr_func_t)(const void*, int, size_t);
typedef uint32_t (*crc32_func_t)(uint32_t, const void*, size_t);
typedef void     (*memzero_nt_func_t)(void*, size_t);
typedef void     (*memcpy_nt_func_t)(void*, const void*, size_t);

static memset_func_t     g_memset     = memset_generic;
static memcpy_func_t     g_memcpy     = memcpy_generic;
static strlen_func_t     g_strlen     = strlen_generic;
static memcmp_func_t     g_memcmp     = memcmp_generic;
static memchr_func_t     g_memchr     = memchr_generic;
static crc32_func_t      g_crc32      = crc32_generic;
static memzero_nt_func_t g_memzero_nt = memzero_nt_generic;
static memcpy_nt_func_t  g_memcpy_nt  = memcpy_nt_generic;

void init_string_optimization() {
    bool avx_hw_confirmed = (g_xsave_mask & (1ULL << 2)) && (g_fpu_mode >= 1);
    bool use_avx          = avx_hw_confirmed && cpu_info.has_avx2;
    bool use_sse2         = cpu_info.has_sse2;
    bool use_sse42        = cpu_info.has_sse4_2;

    if (use_avx) {
        serial_printf("[STRING] Mode: AVX/AVX2 (256-bit) | XCR0: 0x%lx\n", g_xsave_mask);
        g_memset     = memset_avx;
        g_memcpy     = memcpy_avx;
        g_memchr     = memchr_avx;
        g_memzero_nt = memzero_nt_avx_asm;
        g_memcpy_nt  = memcpy_nt_avx;
    } else if (use_sse2) {
        serial_printf("[STRING] Mode: SSE2 (128-bit) | AVX HW confirmed: %d\n",
                      (int)avx_hw_confirmed);
        g_memset = memset_sse2;
        g_memcpy = memcpy_sse2;
    } else {
        serial_write("[STRING] Mode: Generic (Scalar) — No SSE2!\n");
    }

    if (use_sse2) {
        g_strlen = strlen_sse2;
        g_memcmp = memcmp_sse2;
    }

    if (use_sse42) {
        g_crc32 = crc32_sse42;
    }
}

EXPORT_SYMBOL void* memset(void* ptr, int value, size_t num) {
    return g_memset(ptr, value, num);
}

EXPORT_SYMBOL void* memcpy(void* dest, const void* src, size_t num) {
    return g_memcpy(dest, src, num);
}

EXPORT_SYMBOL void* memmove(void* dest, const void* src, size_t num) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    if (d == s) return dest;
    if (d < s) return g_memcpy(dest, src, num);
    d += num; s += num;
    while (num--) *--d = *--s;
    return dest;
}

EXPORT_SYMBOL size_t strlen(const char* str) {
    return g_strlen(str);
}

EXPORT_SYMBOL int memcmp(const void* ptr1, const void* ptr2, size_t num) {
    return g_memcmp(ptr1, ptr2, num);
}

EXPORT_SYMBOL void* memchr(const void* ptr, int ch, size_t count) {
    return g_memchr(ptr, ch, count);
}

EXPORT_SYMBOL uint32_t compute_crc32(uint32_t crc, const void* buf, size_t size) {
    return g_crc32(crc, buf, size);
}

void memzero_nt_avx(void* dest, size_t count) {
    g_memzero_nt(dest, count);
}

void memcpy_nt(void* dest, const void* src, size_t count) {
    g_memcpy_nt(dest, src, count);
}

EXPORT_SYMBOL void safe_memcpy_asm(void* dest, const void* src, size_t count) {
    __asm__ volatile("cld; rep movsb"
        : "+D"(dest), "+S"(src), "+c"(count) :: "memory");
}

EXPORT_SYMBOL int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

EXPORT_SYMBOL int strncmp(const char* s1, const char* s2, size_t n) {
    while (n > 0 && *s1 && (*s1 == *s2)) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

EXPORT_SYMBOL char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char* h = haystack;
            const char* n = needle;
            while (*h && *n && *h == *n) { h++; n++; }
            if (!*n) return (char*)haystack;
        }
    }
    return NULL;
}

EXPORT_SYMBOL char* strchr(const char* s, int c) {
    while (*s != (char)c) {
        if (!*s++) return NULL;
    }
    return (char*)s;
}

EXPORT_SYMBOL char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

EXPORT_SYMBOL char* strcat(char* dest, const char* src) {
    char* d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

EXPORT_SYMBOL size_t strnlen(const char* s, size_t maxlen) {
    size_t len = 0;
    for (; len < maxlen; len++, s++) if (!*s) break;
    return len;
}

EXPORT_SYMBOL char* strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

EXPORT_SYMBOL char* strncat(char* dest, const char* src, size_t n) {
    size_t dest_len = strlen(dest);
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) dest[dest_len + i] = src[i];
    dest[dest_len + i] = '\0';
    return dest;
}

EXPORT_SYMBOL char* strrchr(const char* str, int ch) {
    const char* last = NULL;
    do { if (*str == (char)ch) last = str; } while (*str++);
    return (char*)last;
}

EXPORT_SYMBOL char* strpbrk(const char* s, const char* accept) {
    while (*s) {
        const char* a = accept;
        while (*a) { if (*a++ == *s) return (char*)s; }
        s++;
    }
    return NULL;
}

EXPORT_SYMBOL void itoa(uint64_t n, char* str, int base) {
    int i = 0;
    if (n == 0) { str[i++] = '0'; str[i] = '\0'; return; }
    while (n != 0) {
        int rem = n % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'A' : rem + '0';
        n = n / base;
    }
    str[i] = '\0';
    int start = 0, end = i - 1;
    while (start < end) {
        char temp = str[start]; str[start] = str[end]; str[end] = temp;
        start++; end--;
    }
}

EXPORT_SYMBOL void explicit_bzero(void* ptr, size_t len) {
    volatile uint8_t* p = (volatile uint8_t*)ptr;
    while (len--) *p++ = 0;
}
