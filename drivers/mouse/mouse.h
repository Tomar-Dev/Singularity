// drivers/mouse/mouse.h
#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

#define MOUSE_DATA_PORT 0x60
#define MOUSE_STATUS_PORT 0x64
#define MOUSE_CMD_PORT 0x64

#define MOUSE_CMD_SET_SAMPLE_RATE 0xF3
#define MOUSE_CMD_GET_DEVICE_ID   0xF2
#define MOUSE_CMD_ENABLE_DATA     0xF4
#define MOUSE_CMD_RESET           0xFF
#define MOUSE_CMD_SET_DEFAULTS    0xF6

#ifdef __cplusplus
extern "C" {
#endif

void init_mouse(void);

#ifdef __cplusplus
}
#endif

#endif
