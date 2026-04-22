// archs/cpu/x86_64/drivers/bus/smbus.hpp
#ifndef SMBUS_HPP
#define SMBUS_HPP

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void init_smbus(void);
void i2c_dump(uint8_t device_addr);

#ifdef __cplusplus
}
#endif

#endif