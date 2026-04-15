// system/security/stack_protector.h
#ifndef STACK_PROTECTOR_H
#define STACK_PROTECTOR_H

#include <stdint.h>

void init_stack_protector();

__attribute__((noreturn)) void __stack_chk_fail(void);

#endif
