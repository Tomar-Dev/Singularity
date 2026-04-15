// drivers/keyboard/keyboard.cpp
#include "drivers/keyboard/keyboard.h"
#include "archs/cpu/cpu_hal.h"
#include "drivers/video/framebuffer.h"
#include "system/console/console.h"
#include "libc/stdio.h"
#include "system/process/process.h"
#include "drivers/serial/serial.h"
#include "archs/cpu/x86_64/errno.h"
#include "system/irq/kir.hpp"

#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64

#define KBD_CMD_SET_LEDS  0xED
#define KBD_CMD_ECHO      0xEE
#define KBD_CMD_SCANCODE  0xF0
#define KBD_CMD_IDENTIFY  0xF2
#define KBD_CMD_TYPEMATIC 0xF3
#define KBD_CMD_ENABLE    0xF4
#define KBD_CMD_RESET     0xFF
#define KBD_ACK           0xFA
#define KBD_RESEND        0xFE

#define KBD_BUF_SIZE 256
static uint8_t          kbd_buf[KBD_BUF_SIZE];
static volatile uint32_t kbd_head = 0;
static volatile uint32_t kbd_tail = 0;

PS2Keyboard* g_Keyboard = nullptr;
static KInterrupt* kbd_irq = nullptr;

namespace KeyMaps {
    const char us[128] = {
        0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
        0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,
        '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0,
        ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    const char us_shift[128] = {
        0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
        '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
        0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,
        '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0,
        ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
}

extern "C" void keyboard_thread_fn() {
    while (true) {
        kbd_irq->wait();
        if (g_Keyboard) {
            g_Keyboard->handleInterrupt();
        }
    }
}

PS2Keyboard::PS2Keyboard()
    : Device("PS2_Keyboard", DEV_KEYBOARD),
      shiftPressed(false), capsLock(false), altPressed(false)
{
    g_Keyboard = this;
}

int PS2Keyboard::waitWrite(void) {
    int timeout = 100000;
    while (timeout--) {
        if ((hal_io_inb(PS2_STATUS) & 2) == 0) {
            return 1;
        } else {
            hal_cpu_relax();
        }
    }
    return 0;
}

int PS2Keyboard::waitRead(void) {
    int timeout = 100000;
    while (timeout--) {
        if ((hal_io_inb(PS2_STATUS) & 1) == 1) {
            return 1;
        } else {
            hal_cpu_relax();
        }
    }
    return 0;
}

void PS2Keyboard::putBuffer(char c) {
    uint32_t head = __atomic_load_n(&kbd_head, __ATOMIC_ACQUIRE);
    uint32_t next = (head + 1) % KBD_BUF_SIZE;
    
    kbd_buf[head] = (uint8_t)c;
    __atomic_store_n(&kbd_head, next, __ATOMIC_RELEASE);
    
    if (next == __atomic_load_n(&kbd_tail, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&kbd_tail, (next + 1) % KBD_BUF_SIZE, __ATOMIC_RELEASE);
    }
}

void PS2Keyboard::setTypematicRate(void) {
    if (!waitWrite()) {
        serial_write("[KBD] Typematic: write timeout (cmd byte).\n");
        return;
    } else {
        hal_io_outb(PS2_DATA, KBD_CMD_TYPEMATIC);
    }

    if (!waitRead()) {
        serial_write("[KBD] Typematic: read timeout (ACK).\n");
        return;
    } else {
        uint8_t ack = hal_io_inb(PS2_DATA);
        if (ack != KBD_ACK) {
            serial_write("[KBD] Typematic: command not ACKed.\n");
            return;
        }
    }

    if (!waitWrite()) {
        serial_write("[KBD] Typematic: write timeout (rate byte).\n");
        return;
    } else {
        hal_io_outb(PS2_DATA, 0x00);
    }

    if (!waitRead()) {
        serial_write("[KBD] Typematic: read timeout (rate ACK).\n");
        return;
    } else {
        uint8_t ack = hal_io_inb(PS2_DATA);
        if (ack == KBD_ACK) {
            serial_write("[KBD] Typematic Rate set to Max (250ms/30.0Hz).\n");
        } else {
            serial_write("[KBD] Typematic: rate byte not ACKed.\n");
        }
    }
}

int PS2Keyboard::init(void) {
    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags) : : "memory");

#define KBD_RETURN(val) \
    do { if (rflags & 0x200) { hal_interrupts_enable(); } return (val); } while (0)

    int max_drain = 32;
    while ((hal_io_inb(PS2_STATUS) & 1) && max_drain-- > 0) {
        hal_io_inb(PS2_DATA);
    }

    if (!waitWrite()) {
        serial_write("[KBD] Error: write timeout during self-test.\n");
        KBD_RETURN(0);
    } else {
        hal_io_outb(PS2_CMD, 0xAA);
    }

    {
        int retry = 10000;
        while (retry-- > 0) {
            if (hal_io_inb(PS2_STATUS) & 1) {
                break;
            } else {
                hal_cpu_relax();
            }
        }
        if (hal_io_inb(PS2_STATUS) & 1) {
            hal_io_inb(PS2_DATA);
        }
    }

    if (!waitWrite()) {
        serial_write("[KBD] Error: write timeout enabling port.\n");
        KBD_RETURN(0);
    } else {
        hal_io_outb(PS2_CMD, 0xAE);
    }

    if (!waitWrite()) {
        serial_write("[KBD] Error: timeout requesting config byte.\n");
        KBD_RETURN(0);
    } else {
        hal_io_outb(PS2_CMD, 0x20);
    }

    if (!waitRead()) {
        serial_write("[KBD] Error: timeout reading config byte.\n");
        KBD_RETURN(0);
    } else {
        uint8_t config = hal_io_inb(PS2_DATA);
        config |= (1 << 0) | (1 << 1) | (1 << 6);

        if (!waitWrite()) {
            serial_write("[KBD] Error: timeout writing config byte cmd.\n");
            KBD_RETURN(0);
        } else {
            hal_io_outb(PS2_CMD, 0x60);
        }

        if (!waitWrite()) {
            serial_write("[KBD] Error: timeout writing config byte data.\n");
            KBD_RETURN(0);
        } else {
            hal_io_outb(PS2_DATA, config);
        }
    }

    setTypematicRate();

    if (!waitWrite()) {
        serial_write("[KBD] Error: timeout sending ENABLE command.\n");
        KBD_RETURN(0);
    } else {
        hal_io_outb(PS2_DATA, KBD_CMD_ENABLE);
    }

    if (!waitRead()) {
        serial_write("[KBD] Warning: no ACK for ENABLE command.\n");
    } else {
        hal_io_inb(PS2_DATA);
    }

    max_drain = 32;
    while ((hal_io_inb(PS2_STATUS) & 1) && max_drain-- > 0) {
        hal_io_inb(PS2_DATA);
    }

    kbd_irq = new KInterrupt(33); 
    create_kernel_task_prio(keyboard_thread_fn, PRIO_REALTIME);

    DeviceManager::registerDevice(this);

    KBD_RETURN(1);

#undef KBD_RETURN
}

void PS2Keyboard::handleInterrupt(void) {
    int max_reads = 32; 
    while ((hal_io_inb(PS2_STATUS) & 1) && max_reads-- > 0) {
        uint8_t status = hal_io_inb(PS2_STATUS);
        
        // FIX: DONANIM KİLİTLENMESİ ÖNLENDİ! (Edge-Triggered Deadlock Guard)
        // Eğer fare verisiyse break ATMA! Tamponu okuyup çöpe at ki kontrolör açılsın.
        uint8_t scancode = hal_io_inb(PS2_DATA);

        if (status & 0x20) {
            continue; // Fare verisini es geç ama tamponu okuduğumuz için kilit kalkar.
        }

        if (scancode & 0x80) {
            uint8_t key = scancode & 0x7F;
            if (key == 0x2A || key == 0x36) {
                shiftPressed = false;
            } else if (key == 0x38) {
                altPressed = false;
            }
        } else {
            if (scancode == 0xE0) {
                continue;
            } else {
                switch (scancode) {
                    case 0x2A: { shiftPressed = true;      break; }
                    case 0x36: { shiftPressed = true;      break; }
                    case 0x38: { altPressed   = true;      break; }
                    case 0x3A: { capsLock     = !capsLock; break; }
                    case 0x3E: { putBuffer((char)KEY_F4);  break; }
                    case 0x3F: { putBuffer((char)KEY_F5);  break; }
                    case 0x58: { putBuffer((char)KEY_F12); break; }
                    case 0x48: { console_scroll_up();      break; }
                    case 0x50: { console_scroll_down();    break; }
                    default: {
                        if (scancode < 128) {
                            char c = shiftPressed
                                     ? KeyMaps::us_shift[scancode]
                                     : KeyMaps::us[scancode];

                            if (capsLock) {
                                if (c >= 'a' && c <= 'z') {
                                    c = (char)(c - 32);
                                } else if (c >= 'A' && c <= 'Z') {
                                    c = (char)(c + 32);
                                }
                            }

                            if (c != 0) {
                                putBuffer(c);
                            }
                        }
                        break;
                    }
                }
            }
        }
    }
}

bool PS2Keyboard::hasInput(void) {
    return __atomic_load_n(&kbd_head, __ATOMIC_ACQUIRE) !=
           __atomic_load_n(&kbd_tail, __ATOMIC_ACQUIRE);
}

char PS2Keyboard::getChar(void) {
    uint32_t tail = __atomic_load_n(&kbd_tail, __ATOMIC_ACQUIRE);
    if (tail == __atomic_load_n(&kbd_head, __ATOMIC_ACQUIRE)) {
        return 0;
    } else {
        char c = (char)kbd_buf[tail];
        __atomic_store_n(&kbd_tail, (tail + 1) % KBD_BUF_SIZE,
                         __ATOMIC_RELEASE);
        return c;
    }
}

int PS2Keyboard::read(char* outBuf, int size) {
    if (!outBuf || size <= 0) {
        return 0;
    } else {
        outBuf[0] = getChar();
        return 1;
    }
}

extern "C" {

    __attribute__((used, noinline))
    void init_keyboard_cpp(void) {
        PS2Keyboard* kbd = new PS2Keyboard();
        if (!kbd->init()) {
            serial_write("[KBD] PS/2 Keyboard init failed.\n");
            delete kbd;
            g_Keyboard = nullptr;
        }
    }

    bool keyboard_has_input(void) {
        if (g_Keyboard) {
            return g_Keyboard->hasInput();
        } else {
            return false;
        }
    }

    char keyboard_getchar(void) {
        if (g_Keyboard) {
            return g_Keyboard->getChar();
        } else {
            return 0;
        }
    }

    bool keyboard_is_alt_pressed(void) {
        if (g_Keyboard) {
            return g_Keyboard->isAltPressed();
        } else {
            return false;
        }
    }

    void keyboard_enable(void) {
    }

    char keyboard_wait_char_poll(void) {
        int max_drain = 32;
        while ((hal_io_inb(0x64) & 1) && max_drain-- > 0) {
            hal_io_inb(0x60);
        }

        while (1) {
            while ((hal_io_inb(0x64) & 1) == 0) {
                hal_cpu_relax();
            }

            uint8_t scancode = hal_io_inb(0x60);

            if (scancode & 0x80) {
                continue;
            }

            if (scancode == 0x1C) {
                return '\n';
            } else if (scancode == 0x0E) {
                return '\b';
            } else if (scancode == 0x39) {
                return ' ';
            } else {
                if (scancode < 128) {
                    char c = KeyMaps::us[scancode];
                    if (c) {
                        return c;
                    }
                }
            }
        }
    }
}