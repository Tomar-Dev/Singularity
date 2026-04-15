// archs/cpu/x86_64/syscall/syscall.h
#ifndef SYSCALL_H
#define SYSCALL_H

#include "archs/cpu/x86_64/interrupts/isr.h"
#define SYSCALL_WRITE 1

void init_syscalls();

#endif
