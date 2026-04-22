// drivers/timer/pit_lock.c
#include "archs/cpu/cpu_hal.h"
#include "archs/cpu/x86_64/sync/spinlock.h"
static spinlock_t pit_lock = {0};

void pit_lock_init() {
    spinlock_init(&pit_lock);
}

void pit_lock_acquire(uint64_t* flags) {
    *flags = spinlock_acquire(&pit_lock);
}

void pit_lock_release(uint64_t flags) {
    spinlock_release(&pit_lock, flags);
}
