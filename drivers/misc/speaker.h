// drivers/misc/speaker.h
#ifndef SPEAKER_H
#define SPEAKER_H

#include <stdint.h>

#ifdef __cplusplus
#include "system/device/device.h"
class PCSpeaker : public Device {
public:
    PCSpeaker();
    int init() override;
    
    void tone(uint32_t freq);
    void off();
    void beep(uint32_t freq, uint32_t duration_ms);
};
#endif

#ifdef __cplusplus
extern "C" {
#endif

void init_speaker();
void beep(uint32_t frequency_hz, uint32_t duration_ms);
void play_boot_sound();

#ifdef __cplusplus
}
#endif

#endif
