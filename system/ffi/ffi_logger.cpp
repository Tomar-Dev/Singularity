// system/ffi/ffi_logger.cpp
#include "system/ffi/ffi.h"
#include "libc/stdio.h"
#include "drivers/serial/serial.h"
#include "system/process/process.h"
#include "system/console/console.h"
#define FFI_RING_SIZE 16384

extern "C" {
    char ffi_log_ring_buf[FFI_RING_SIZE];
    volatile uint32_t ffi_log_ring_head = 0;
    volatile uint32_t ffi_log_ring_tail = 0;
}

extern "C" void ffi_logger_flush_sync() {
    uint32_t tail = __atomic_load_n(&ffi_log_ring_tail, __ATOMIC_ACQUIRE);
    
    while (true) {
        // FIX: Snapshot Kaybı Önlendi. Head döngü içinde dinamik olarak okunur.
        uint32_t head = __atomic_load_n(&ffi_log_ring_head, __ATOMIC_ACQUIRE);
        if (tail == head) break;
        
        char c = ffi_log_ring_buf[tail];
        if (c == '\n') serial_putc('\r');
        serial_putc(c);
        
        ffi_log_ring_buf[tail] = 0;
        tail = (tail + 1) % FFI_RING_SIZE;
    }
    __atomic_store_n(&ffi_log_ring_tail, tail, __ATOMIC_RELEASE);
}

void ffi_logger_task() {
    while (1) {
        uint32_t head = __atomic_load_n(&ffi_log_ring_head, __ATOMIC_ACQUIRE);
        uint32_t tail = __atomic_load_n(&ffi_log_ring_tail, __ATOMIC_ACQUIRE);
        if (head != tail) {
            ffi_logger_flush_sync();
        } else {
            task_sleep(2);
        }
    }
}