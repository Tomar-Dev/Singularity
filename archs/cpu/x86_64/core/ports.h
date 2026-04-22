// archs/cpu/x86_64/core/ports.h
#ifndef PORTS_H
#define PORTS_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t data) {
    __asm__ volatile("outb %0, %1" : : "a"(data), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t data) {
    __asm__ volatile("outw %0, %1" : : "a"(data), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outl(uint16_t port, uint32_t data) {
    __asm__ volatile("outl %0, %1" : : "a"(data), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void insw(uint16_t port, void* buffer, uint32_t count) {
    __asm__ volatile("rep insw" 
                     : "+D"(buffer), "+c"(count) 
                     : "d"(port) 
                     : "memory");
}

static inline void outsw(uint16_t port, const void* buffer, uint32_t count) {
    __asm__ volatile("rep outsw" 
                     : "+S"(buffer), "+c"(count) 
                     : "d"(port) 
                     : "memory");
}

static inline void io_wait() {
    __asm__ volatile("pause" ::: "memory");
}

static inline void io_wait_legacy() {
    outb(0x80, 0);
}

#endif