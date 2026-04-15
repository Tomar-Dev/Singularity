// drivers/mouse/mouse.cpp
#include "drivers/mouse/mouse.h"
#include "archs/cpu/cpu_hal.h"
#include "archs/cpu/x86_64/interrupts/isr.h"
#include "libc/string.h"
#include "system/irq/kir.hpp"
#include "system/process/process.h"

static uint8_t mouse_cycle  = 0;
static int8_t  mouse_packet[4];
static int8_t  prev_packet[4];
static uint8_t mouse_id     = 0;

static void* mouse_irq_obj = nullptr;

static int mouse_wait(uint8_t type) {
    uint32_t timeout = 500000;
    while (timeout--) {
        if (type == 0) {
            if ((hal_io_inb(MOUSE_STATUS_PORT) & 2) == 0) {
                return 1;
            } else {
                hal_cpu_relax();
            }
        } else {
            if ((hal_io_inb(MOUSE_STATUS_PORT) & 1) == 1) {
                return 1;
            } else {
                hal_cpu_relax();
            }
        }
    }
    return 0;
}

static void mouse_write(uint8_t value) {
    if (!mouse_wait(0)) return;
    hal_io_outb(MOUSE_CMD_PORT, 0xD4);

    if (!mouse_wait(0)) return;
    hal_io_outb(MOUSE_DATA_PORT, value);
}

static uint8_t mouse_read(void) {
    if (!mouse_wait(1)) return 0;
    return hal_io_inb(MOUSE_DATA_PORT);
}

static void mouse_send_cmd(uint8_t cmd) {
    mouse_write(cmd);
    mouse_read();
}

static void enable_scroll_wheel(void) {
    mouse_send_cmd(MOUSE_CMD_SET_SAMPLE_RATE);
    mouse_send_cmd(200);
    mouse_send_cmd(MOUSE_CMD_SET_SAMPLE_RATE);
    mouse_send_cmd(100);
    mouse_send_cmd(MOUSE_CMD_SET_SAMPLE_RATE);
    mouse_send_cmd(80);

    mouse_send_cmd(MOUSE_CMD_GET_DEVICE_ID);
    mouse_id = mouse_read();
}

static void mouse_handler(registers_t* regs) {
    (void)regs;

    // FIX: Infinite IRQ Stall Protection (Fare için)
    int max_reads = 32; 
    while ((hal_io_inb(MOUSE_STATUS_PORT) & 1) && max_reads-- > 0) {
        uint8_t status = hal_io_inb(MOUSE_STATUS_PORT);
        uint8_t input = hal_io_inb(MOUSE_DATA_PORT); // Tamponu ne olursa olsun okuyup boşalt!

        if (!(status & 0x20)) {
            continue; // Klavye verisiyse es geç, ancak okunduğu için kilit kalktı.
        }

        if (mouse_cycle == 0 && !(input & 0x08)) {
            continue; 
        }

        mouse_packet[mouse_cycle] = (int8_t)input;
        mouse_cycle++;

        uint8_t packet_size = (mouse_id == 3) ? 4 : 3;

        if (mouse_cycle == packet_size) {
            mouse_cycle = 0;
            if (memcmp(mouse_packet, prev_packet, packet_size) == 0) continue;
            memcpy(prev_packet, mouse_packet, packet_size);
            // TODO: Dispatch mouse event
        }
    }
}

extern "C" void mouse_thread_fn() {
    while(1) {
        kir_wait_interrupt(mouse_irq_obj);
        mouse_handler(nullptr);
    }
}

extern "C" {
    __attribute__((used, noinline))
    void init_mouse(void) {
        uint64_t rflags;
        __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags) : : "memory");

        if (!mouse_wait(0)) {
            if (rflags & 0x200) { hal_interrupts_enable(); }
            return;
        }

        mouse_irq_obj = kir_create_interrupt(44);
        create_kernel_task_prio(mouse_thread_fn, PRIO_REALTIME);

        if (!mouse_wait(0)) {
            if (rflags & 0x200) { hal_interrupts_enable(); }
            return;
        } else {
            hal_io_outb(MOUSE_CMD_PORT, 0xA8);
        }

        if (!mouse_wait(0)) {
            if (rflags & 0x200) { hal_interrupts_enable(); }
            return;
        } else {
            hal_io_outb(MOUSE_CMD_PORT, 0x20);
        }

        if (!mouse_wait(1)) {
            if (rflags & 0x200) { hal_interrupts_enable(); }
            return;
        } else {
            uint8_t config = hal_io_inb(MOUSE_DATA_PORT) | 2;

            if (!mouse_wait(0)) {
                if (rflags & 0x200) { hal_interrupts_enable(); }
                return;
            } else {
                hal_io_outb(MOUSE_CMD_PORT, 0x60);
            }

            if (!mouse_wait(0)) {
                if (rflags & 0x200) { hal_interrupts_enable(); }
                return;
            } else {
                hal_io_outb(MOUSE_DATA_PORT, config);
            }
        }

        mouse_write(MOUSE_CMD_SET_DEFAULTS);
        mouse_read();

        enable_scroll_wheel();

        mouse_write(MOUSE_CMD_ENABLE_DATA);
        mouse_read();

        uint8_t current_mask = hal_io_inb(0xA1);
        hal_io_outb(0xA1, current_mask & ~(1 << 4));

        memset(mouse_packet, 0, 4);
        memset(prev_packet,  0, 4);

        if (rflags & 0x200) {
            hal_interrupts_enable();
        }
    }
}