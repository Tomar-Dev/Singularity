// drivers/timer/pit_lock.h
#ifndef PIT_LOCK_H
#define PIT_LOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void pit_lock_acquire(uint64_t* flags);
void pit_lock_release(uint64_t flags);
void pit_lock_init();

#ifdef __cplusplus
}
#endif

#endif
