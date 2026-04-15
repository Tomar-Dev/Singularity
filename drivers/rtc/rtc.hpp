// drivers/rtc/rtc.hpp
#ifndef RTC_HPP
#define RTC_HPP

#include "system/device/device.h"
#include <stdint.h>

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} rtc_time_t;

#ifdef __cplusplus

class RTCDriver : public Device {
public:
    RTCDriver();
    int init() override;
    
    void getTime(rtc_time_t* time);
    void getFormattedTime(char* buffer);
    void delaySeconds(uint32_t seconds);
};

#endif

#ifdef __cplusplus
extern "C" {
#endif

void rtc_init();
void rtc_get_time(rtc_time_t* time);
void rtc_delay_seconds(uint32_t seconds);
void rtc_wait_one_second(void); // FIX: Yeni Polling Fonksiyonu
void rtc_get_formatted_time(char* buffer);

#ifdef __cplusplus
}
#endif

#endif