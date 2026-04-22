// archs/cpu/x86_64/drivers/misc/speaker.cpp
#include "archs/cpu/x86_64/drivers/misc/speaker.h"
#include "system/device/device.h"
#include "archs/cpu/cpu_hal.h"
#include "archs/cpu/x86_64/timer/tsc.h"
#include "archs/cpu/x86_64/timer/pit_lock.h" 

class PCSpeaker : public Device {
public:
    PCSpeaker();
    int init() override;
    void tone(uint32_t freq);
    void off();
    void beep(uint32_t freq, uint32_t duration_ms);
};

extern "C" void timer_sleep(uint64_t ticks);
extern "C" void sleep_ms(uint64_t ms);

extern "C" {
    void pit_channel2_start_asm(uint16_t divisor);
}

static PCSpeaker* g_speaker = nullptr;

PCSpeaker::PCSpeaker() : Device("PC Speaker", DEV_UNKNOWN) {
    g_speaker = this;
}

int PCSpeaker::init() {
    DeviceManager::registerDevice(this);
    return 1;
}

void PCSpeaker::tone(uint32_t freq) {
    if (freq == 0) return;
    uint32_t div = 1193180 / freq;
    
    uint64_t flags;
    pit_lock_acquire(&flags);
    
    pit_channel2_start_asm((uint16_t)div);
    
    pit_lock_release(flags);
}

void PCSpeaker::off() {
    uint64_t flags;
    pit_lock_acquire(&flags);
    
    uint8_t val = hal_io_inb(0x61);
    val &= 0xFC;
    hal_io_outb(0x61, val);
    
    pit_lock_release(flags);
}

void PCSpeaker::beep(uint32_t freq, uint32_t duration_ms) {
    if (freq > 0) tone(freq);
    sleep_ms(duration_ms);
    if (freq > 0) off();
}

extern "C" {
    void init_speaker() {
        PCSpeaker* spk = new PCSpeaker();
        spk->init();
    }

    void beep(uint32_t freq, uint32_t duration_ms) {
        if (g_speaker) g_speaker->beep(freq, duration_ms);
        else {
            PCSpeaker temp;
            temp.beep(freq, duration_ms);
        }
    }
    
    void play_boot_sound() {
        beep(1000, 100);
        beep(1500, 100);
        beep(2000, 200);
    }
}