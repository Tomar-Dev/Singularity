// drivers/serial/serial.cpp
#include "drivers/serial/serial.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include <stdarg.h>
#include "archs/cpu/x86_64/interrupts/isr.h"
#include "archs/cpu/cpu_hal.h"
#include "archs/cpu/x86_64/sync/spinlock.h"
#include "system/irq/kir.hpp"
#include "system/process/process.h"

#define SERIAL_RING_SIZE 16384
#define RING_MASK (SERIAL_RING_SIZE - 1)

static char     serial_ring[SERIAL_RING_SIZE];
static volatile uint32_t ring_head = 0;
static volatile uint32_t ring_tail = 0;

static bool serial_hardware_present = false;
static bool async_mode = false;
static KInterrupt* serial_irq_obj = nullptr;

extern "C" void serial_irq_handler_wrapper(registers_t* regs) {
    (void)regs;
    if (!serial_hardware_present) return;

    // Wait-Free Consumer: IRQ geldiğinde tamponu boşalt
    while (ring_head != ring_tail) {
        if ((hal_io_inb(COM1 + 5) & 0x20)) {
            uint32_t next_tail = (ring_tail + 1) & RING_MASK;
            hal_io_outb(COM1, serial_ring[ring_tail]);
            ring_tail = next_tail;
        } else {
            break;
        }
    }
}

extern "C" void serial_thread_fn() {
    while(true) {
        serial_irq_obj->wait();
        serial_irq_handler_wrapper(nullptr);
    }
}

SerialDriver::SerialDriver() : Device("COM1 Serial", DEV_SERIAL) {}

int SerialDriver::init() {
    if (serial_hardware_present) {
        DeviceManager::registerDevice(this);
        return 1;
    }
    return 0;
}

int SerialDriver::write(const char* buffer, int size) {
    for (int i = 0; i < size; i++) {
        serial_putc(buffer[i]);
    }
    return size;
}

int SerialDriver::read(char* buffer, int size) {
    (void)buffer; (void)size;
    return 0; 
}

extern "C" {
    void init_serial_early() {
        hal_io_outb(COM1 + 1, 0x00);
        hal_io_outb(COM1 + 3, 0x80);
        hal_io_outb(COM1 + 0, 0x01);
        hal_io_outb(COM1 + 1, 0x00); 
        hal_io_outb(COM1 + 3, 0x03);
        hal_io_outb(COM1 + 2, 0xC7);
        hal_io_outb(COM1 + 4, 0x0B); 
        
        serial_hardware_present = true;
    }

    void init_serial_late() {
        if (serial_hardware_present) {
            SerialDriver* drv = new SerialDriver();
            drv->init();

            serial_irq_obj = new KInterrupt(36);
            async_mode = true;
            create_kernel_task_prio(serial_thread_fn, PRIO_REALTIME);
            
            // Kesmeleri aktif et
            hal_io_outb(COM1 + 1, 0x01);
        }
    }

    void serial_putc(char c) {
        if (!serial_hardware_present) return;

        if (!async_mode) {
            // Polling Mode (Early Boot / Panic)
            while ((hal_io_inb(COM1 + 5) & 0x20) == 0) hal_cpu_relax();
            hal_io_outb(COM1, c);
        } else {
            // Wait-Free Producer: Tampona yaz, kilit bekleme
            uint32_t next_head = (ring_head + 1) & RING_MASK;
            if (next_head == ring_tail) {
                // Buffer full: Skip or drop oldest? Enterprise standard: Drop oldest.
                ring_tail = (ring_tail + 1) & RING_MASK;
            }
            serial_ring[ring_head] = c;
            ring_head = next_head;
            
            // Eğer donanım boşsa manuel dürt
            if (hal_io_inb(COM1 + 5) & 0x20) {
                serial_irq_handler_wrapper(nullptr);
            }
        }
    }

    void serial_write_atomic(const char* str) {
        while(*str) {
            if (*str == '\n') serial_putc('\r');
            serial_putc(*str++);
        }
    }

    void serial_write(const char* str) {
        serial_write_atomic(str);
    }

    void serial_enable_direct_mode() {
        async_mode = false;
        hal_io_outb(COM1 + 1, 0x00); // Disable interrupts
    }

    void serial_printf(const char* format, ...) {
        char buf[1024]; 
        va_list args;
        va_start(args, format);
        vsnprintf(buf, sizeof(buf), format, args);
        va_end(args);
        serial_write(buf);
    }
}