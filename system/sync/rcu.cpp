// system/sync/rcu.cpp
#include "system/sync/rcu.h"
#include "archs/cpu/x86_64/smp/smp.h"
#include "system/process/process.h"
#include "kernel/debug.h"
#include "kernel/fastops.h"

extern "C" uint64_t timer_get_ticks();

void rcu_read_lock(void) {
    uint8_t cpu = (uint8_t)get_cpu_id_fast();
    if (per_cpu_data[cpu]) {
        __atomic_fetch_add(&per_cpu_data[cpu]->preempt_count, 1, __ATOMIC_ACQUIRE);
    } else {
        // CPU not fully initialized, tracking skipped.
    }
    __asm__ volatile("" ::: "memory");
}

void rcu_read_unlock(void) {
    __asm__ volatile("" ::: "memory");
    uint8_t cpu = (uint8_t)get_cpu_id_fast();
    if (per_cpu_data[cpu]) {
        __atomic_fetch_sub(&per_cpu_data[cpu]->preempt_count, 1, __ATOMIC_RELEASE);
    } else {
        // CPU not fully initialized.
    }
}

void synchronize_rcu(void) {
    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0" : "=r"(rflags));
    if (!(rflags & 0x200)) {
        panic_at("RCU", 0, KERR_DEADLOCK_DETECTED,
                 "synchronize_rcu() called with Interrupts Disabled (CLI)!");
    }

    uint64_t snap[MAX_CPUS] = {0};
    for (int i = 0; i < num_cpus; i++) {
        snap[i] = __atomic_load_n(&cpu_tick_counts[i], __ATOMIC_ACQUIRE);
    }

    for (int i = 0; i < num_cpus; i++) {
        uint64_t timeout_target = timer_get_ticks() + RCU_SYNC_TIMEOUT_TICKS;

        while (true) {
            uint64_t current = __atomic_load_n(&cpu_tick_counts[i], __ATOMIC_ACQUIRE);
            
            // Eğer tick arttıysa çekirdek aktiftir ve güvenli bölgeden geçmiştir
            if (current > snap[i]) {
                break;
            } 
            
            // GÜVENLİK YAMASI: Çekirdek "Idle" (Boşta) durumundaysa tick artmasa bile güvenlidir!
            if (per_cpu_data[i] && current_process[i] && current_process[i] == idle_tasks[i]) {
                break; 
            }
            
            if (timer_get_ticks() > timeout_target) {
                break; // Timeout fallback to prevent complete system freeze
            }
            
            hal_cpu_relax();
            yield();
        }
    }
}