// archs/cpu/x86_64/drivers/keyboard/keyboard.h
#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

#define KEY_F4  0x84
#define KEY_F5  0x85
#define KEY_F12 0x8C

#ifdef __cplusplus
extern "C" {
#endif

void init_keyboard_cpp(void);
#define init_keyboard init_keyboard_cpp 

bool keyboard_has_input(void);
char keyboard_getchar(void);
bool keyboard_is_alt_pressed(void);
void keyboard_enable(void);

#ifdef __cplusplus
}
#endif

#endif