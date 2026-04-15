// system/sync/rcu.h
#ifndef RCU_H
#define RCU_H

#include <stdint.h>

// Maximum number of timer ticks synchronize_rcu() will wait for a single CPU
// before declaring it dead and moving on. At 250 Hz this equals 4 seconds.
// Increase this value only if hardware timers are known to be slower than 250 Hz.
#define RCU_SYNC_TIMEOUT_TICKS 1000U

#ifdef __cplusplus
extern "C" {
#endif

void rcu_read_lock(void);

void rcu_read_unlock(void);

void synchronize_rcu(void);

#ifdef __cplusplus
}

template<typename T> T* rcu_dereference(T** ptr) {
    __asm__ volatile("" ::: "memory");
    return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
}

template<typename T> void rcu_assign_pointer(T** ptr, T* val) {
    __atomic_store_n(ptr, val, __ATOMIC_RELEASE);
}

#endif

#endif