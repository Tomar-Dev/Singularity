// archs/cpu/x86_64/sync/spinlock.h
#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <stdint.h>
#include "kernel/debug.h"

typedef struct {
    volatile uint32_t next_ticket;
    volatile uint32_t serving_ticket;
    uint8_t _pad[56]; // 64 - 8 bytes = 56 bytes padding
} __attribute__((aligned(64))) spinlock_t;

#define SPINLOCK_DEADLOCK_CYCLES 500000000ULL
#define RFLAGS_IF_BIT 0x200ULL

static inline void spinlock_init(spinlock_t* lock) {
    if (lock) {
        __atomic_store_n(&lock->next_ticket, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&lock->serving_ticket, 0, __ATOMIC_RELEASE);
    } else {
        // Unreachable for valid pointers, ignored silently for NULL
    }
}

static inline uint64_t rdtsc_spin() {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static inline void debug_print_hex_direct(uint64_t val) {
    char buf[17];
    int i = 15;
    buf[16] = 0;
    if (val == 0) { 
        dbg_putc('0'); 
    } else {
        while (val > 0 && i >= 0) {
            int rem = val % 16;
            if (rem < 10) {
                buf[i--] = (rem + '0');
            } else {
                buf[i--] = (rem - 10 + 'A');
            }
            val /= 16;
        }
        dbg_puts(&buf[i + 1]);
    }
}

static inline uint64_t spinlock_acquire(spinlock_t* lock) {
    uint64_t rflags = 0;
    if (!lock) {
        return rflags;
    } else {
        __asm__ volatile("pushfq; pop %0; cli" : "=r" (rflags) : : "memory");

        uint32_t my_ticket = __atomic_fetch_add(&lock->next_ticket, 1, __ATOMIC_RELAXED);
        uint64_t start_cycles = rdtsc_spin();
        
        // OPT-003 FIX: Exponential Backoff for Ticket Spinlocks
        uint32_t backoff = 1;

        while (__atomic_load_n(&lock->serving_ticket, __ATOMIC_ACQUIRE) != my_ticket) {
            for (uint32_t i = 0; i < backoff; i++) {
                hal_cpu_relax();
            }
            if (backoff < 256) {
                backoff <<= 1;
            } else {
                // Max backoff reached
            }

            extern volatile bool global_panic_active;
            if (!global_panic_active &&
                (rflags & RFLAGS_IF_BIT) &&
                (rdtsc_spin() - start_cycles) > SPINLOCK_DEADLOCK_CYCLES)
            {
                dbg_puts("\n[CPU] CRITICAL: SPINLOCK DEADLOCK ON LOCK PTR: 0x");
                debug_print_hex_direct((uint64_t)lock);
                dbg_puts("\n");
                __asm__ volatile("ud2");
            } else {
                // Keep spinning safely
            }
        }

        __asm__ volatile("" ::: "memory");
        return rflags;
    }
}

static inline int spinlock_try_acquire(spinlock_t* lock, uint64_t* rflags) {
    if (!lock || !rflags) {
        return 0;
    } else {
        __asm__ volatile("pushfq; pop %0; cli" : "=r" (*rflags) : : "memory");

        uint32_t serving = __atomic_load_n(&lock->serving_ticket, __ATOMIC_RELAXED);
        uint32_t next    = __atomic_load_n(&lock->next_ticket,    __ATOMIC_RELAXED);

        if (serving == next) {
            if (__atomic_compare_exchange_n(&lock->next_ticket, &next, next + 1,
                                            0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
                __asm__ volatile("" ::: "memory");
                return 1;
            } else {
                if (*rflags & RFLAGS_IF_BIT) { 
                    hal_interrupts_enable(); 
                } else {
                    // Do nothing
                }
                return 0;
            }
        } else {
            if (*rflags & RFLAGS_IF_BIT) { 
                hal_interrupts_enable(); 
            } else {
                // Do nothing
            }
            return 0;
        }
    }
}

static inline void spinlock_release(spinlock_t* lock, uint64_t rflags) {
    if (!lock) {
        return;
    } else {
        __asm__ volatile("" ::: "memory");
        __atomic_fetch_add(&lock->serving_ticket, 1, __ATOMIC_RELEASE);

        if (rflags & RFLAGS_IF_BIT) {
            hal_interrupts_enable();
        } else {
            // Do not enable interrupts if they were disabled before locking
        }
    }
}

#ifdef __cplusplus
class ScopedSpinlock {
private:
    spinlock_t* lock;
    uint64_t    rflags;

public:
    explicit ScopedSpinlock(spinlock_t* l) : lock(l) {
        rflags = spinlock_acquire(lock);
    }
    ~ScopedSpinlock() {
        spinlock_release(lock, rflags);
    }

    ScopedSpinlock(const ScopedSpinlock&)            = delete;
    ScopedSpinlock& operator=(const ScopedSpinlock&) = delete;
};
#endif

#endif