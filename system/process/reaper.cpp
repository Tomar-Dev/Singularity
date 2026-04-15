// system/process/reaper.cpp
#include "system/process/reaper.hpp"
#include "system/process/process.h" 
#include "memory/kheap.h"
#include "memory/pmm.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "drivers/serial/serial.h"
#include "archs/cpu/x86_64/sync/spinlock.h"
#include "archs/cpu/x86_64/smp/smp.h" 
#include "archs/cpu/cpu_hal.h"

extern "C" {
    extern rwlock_t task_list_lock;
    extern uint8_t num_cpus;
    
    void task_sleep(uint64_t ticks);
    void yield();
    void free_kernel_stack(void* stack);
    void kheap_trim();
    int process_get_zombie_count();
    void sched_wake_task(process_t* task);
    
    uint64_t pmm_get_total_memory();
    uint64_t pmm_get_used_memory();
    void pmm_flush_magazines(); 
}

static volatile bool force_run = false;
static process_t* reaper_task = nullptr;

#define TICK_1_SEC 250
#define TICK_20_SEC (20 * TICK_1_SEC)
#define TICK_2_MIN (120 * TICK_1_SEC)

#define ZOMBIE_THRESHOLD_NORMAL 60
#define ZOMBIE_THRESHOLD_CRITICAL 10
#define RAM_THRESHOLD_PERCENT 95

void GrimReaper::init() {
    create_kernel_task_prio(GrimReaper::run, PRIO_LOW);
}

// FIX: Kilit Yükseltme (Read -> Write) ile "Stop-The-World" Gecikmeleri Önlendi.
bool GrimReaper::reapOne() {
    process_t* victim = nullptr;
    
    // AŞAMA 1: Sadece Okuma Kilidi ile Kurban Ara (Diğer görevler durmaz)
    rwlock_acquire_read(task_list_lock);
    process_t* curr = process_list_head;
    if (!curr) {
        rwlock_release_read(task_list_lock);
        return false;
    }
    
    process_t* start = curr;
    do {
        if (curr->thread_fn == GrimReaper::run) {
            reaper_task = curr;
        }

        if (curr->state == PROCESS_ZOMBIE || curr->state == PROCESS_DEAD) {
            victim = curr;
            break; 
        }
        curr = curr->next;
    } while (curr && curr != start); 
    
    rwlock_release_read(task_list_lock);
    
    if (!victim) return false; 
    
    // AŞAMA 2: Yazma Kilidi Al ve Kurbanı Güvenle Sil
    rwlock_acquire_write(task_list_lock);
    
    // Kurban listeden zaten çıkmış olabilir, tekrar doğrula
    bool still_valid = false;
    curr = process_list_head;
    process_t* prev = process_list_tail; 
    
    if (curr) {
        do {
            if (curr == victim && (curr->state == PROCESS_ZOMBIE || curr->state == PROCESS_DEAD)) {
                bool active = false;
                for (int i = 0; i < num_cpus; i++) {
                    if (current_process[i] == curr || (per_cpu_data[i] && (process_t*)per_cpu_data[i]->switching_task == curr)) {
                        active = true;
                        break;
                    }
                }
                
                if (!active && curr->pid > 0) {
                    if (curr == process_list_head) process_list_head = curr->next;
                    if (curr == process_list_tail) process_list_tail = prev;
                    
                    prev->next = curr->next;
                    still_valid = true;
                }
                break; 
            }
            prev = curr;
            curr = curr->next;
        } while (curr && curr != process_list_head); 
    }
    
    rwlock_release_write(task_list_lock);
    
    if (still_valid) {
        sched_remove(victim->cpu_id, victim);
        
        if (victim->stack_base) {
            free_kernel_stack(victim->stack_base);
        }
        memset(victim->fxsave_region, 0, FPU_BUFFER_SIZE); // FIX: FPU Buffer boyutu güncellendi
        kfree_aligned(victim);
        return true; 
    }
    
    return false; 
}

void GrimReaper::run() {
    while (reapOne()) { yield(); }
    kheap_trim();
    pmm_flush_magazines(); 
    
    uint64_t last_run_tick = hal_timer_get_ticks();
    
    while (true) {
        bool should_run = false;
        uint64_t current_tick = hal_timer_get_ticks();
        
        if (force_run) {
            should_run = true;
            force_run = false;
        }
        else {
            int zombies = process_get_zombie_count();
            uint64_t total_ram = pmm_get_total_memory();
            uint64_t used_ram = pmm_get_used_memory();
            int ram_usage = (used_ram * 100) / total_ram;
            
            if (ram_usage >= RAM_THRESHOLD_PERCENT) {
                if ((current_tick - last_run_tick) >= TICK_20_SEC) {
                    if (zombies >= ZOMBIE_THRESHOLD_CRITICAL) {
                        should_run = true;
                    }
                }
            }
            else {
                if ((current_tick - last_run_tick) >= TICK_2_MIN) {
                    if (zombies >= ZOMBIE_THRESHOLD_NORMAL) {
                        should_run = true;
                    }
                }
            }
        }
        
        if (should_run) {
            int count = 0;
            while (reapOne()) {
                count++;
                yield(); 
            }
            if (count > 0) {
                kheap_trim();
                pmm_flush_magazines(); 
            }
            last_run_tick = hal_timer_get_ticks();
        }
        
        task_sleep(TICK_1_SEC); 
    }
}

extern "C" {
    void init_reaper() {
        GrimReaper::init();
    }
    
    void signal_reaper() {
        force_run = true;
        if (reaper_task) {
            sched_wake_task(reaper_task);
        }
    }
}