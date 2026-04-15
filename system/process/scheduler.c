// system/process/scheduler.c
#include "system/process/process.h"
#include "archs/cpu/x86_64/sync/spinlock.h"
#include "archs/cpu/x86_64/smp/smp.h"
#include "archs/cpu/x86_64/interrupts/isr.h"
#include "drivers/serial/serial.h"
#include "memory/paging.h"
#include "kernel/fastops.h"
#include "kernel/profiler.h"
#include "archs/cpu/x86_64/context/tss.h"
#include "memory/kheap.h"
#include "libc/string.h"
#include "kernel/config.h"
#include "archs/cpu/x86_64/core/topology.h"
#include "libc/stdio.h"
#include "kernel/debug.h"

#define PRIO_COUNT 6
#define STARVATION_THRESHOLD 1250

typedef struct runqueue {
    process_t* head;
    process_t* tail;
} runqueue_t;

typedef struct cpu_runqueue {
    runqueue_t queues[PRIO_COUNT];
    uint32_t   active_queues; 
    process_t* sleep_queue;
    int        total_tasks;
    uint64_t   min_vruntime;
    spinlock_t lock;
} cpu_runqueue_t;

static cpu_runqueue_t sched_queues[MAX_CPUS];

extern uint8_t       num_cpus;
extern volatile bool global_panic_active;
extern void          prefetch_stack(void* ptr);
extern void          print_status(const char* prefix, const char* msg, const char* status);
extern uint64_t      timer_get_ticks();

static void __sched_enqueue_locked(uint8_t cpu_id, process_t* task,
                                   kernel_prio_t prio, int16_t affinity) {
    if (task->is_queued) {
        return;
    }

    task->priority = prio;
    task->affinity = affinity;
    task->cpu_id   = cpu_id;

    int p = (int)prio;
    if (p >= PRIO_COUNT) {
        p = PRIO_COUNT - 1;
    }

    uint64_t now = timer_get_ticks();
    task->last_queued = now;

    if (task->vruntime < sched_queues[cpu_id].min_vruntime) {
        task->vruntime = sched_queues[cpu_id].min_vruntime;
    }

    task->rq_next = NULL;
    task->rq_prev = sched_queues[cpu_id].queues[p].tail;

    if (sched_queues[cpu_id].queues[p].tail) {
        sched_queues[cpu_id].queues[p].tail->rq_next = task;
    } else {
        sched_queues[cpu_id].queues[p].head = task;
    }
    sched_queues[cpu_id].queues[p].tail = task;

    sched_queues[cpu_id].active_queues |= (1U << p);

    task->is_queued = true;
    sched_queues[cpu_id].total_tasks++;
}

void sched_enqueue(uint8_t cpu_id, process_t* task, kernel_prio_t prio, int16_t affinity) {
    if (unlikely(cpu_id >= MAX_CPUS || !task)) {
        return;
    }
    uint64_t flags = spinlock_acquire(&sched_queues[cpu_id].lock);
    __sched_enqueue_locked(cpu_id, task, prio, affinity);
    spinlock_release(&sched_queues[cpu_id].lock, flags);
}

static void __sched_remove_locked(uint8_t cpu_id, process_t* task) {
    if (!task->is_queued) {
        return;
    }

    int p = (int)task->priority;
    if (p >= PRIO_COUNT) {
        p = PRIO_COUNT - 1;
    }

    if (task->rq_prev) {
        task->rq_prev->rq_next = task->rq_next;
    } else {
        sched_queues[cpu_id].queues[p].head = task->rq_next;
    }

    if (task->rq_next) {
        task->rq_next->rq_prev = task->rq_prev;
    } else {
        sched_queues[cpu_id].queues[p].tail = task->rq_prev;
    }

    if (!sched_queues[cpu_id].queues[p].head) {
        sched_queues[cpu_id].active_queues &= ~(1U << p);
    }

    task->rq_next    = NULL;
    task->rq_prev    = NULL;
    task->is_queued  = false;

    if (sched_queues[cpu_id].total_tasks > 0) {
        sched_queues[cpu_id].total_tasks--;
    }
}

void sched_remove(uint8_t cpu_id, process_t* task) {
    if (unlikely(cpu_id >= MAX_CPUS || !task)) {
        return;
    }
    uint64_t flags = spinlock_acquire(&sched_queues[cpu_id].lock);
    __sched_remove_locked(cpu_id, task);
    spinlock_release(&sched_queues[cpu_id].lock, flags);
}

void sched_wake_task(process_t* task) {
    if (!task) {
        return;
    }
    uint8_t target_cpu = task->cpu_id;

    uint64_t flags = spinlock_acquire(&sched_queues[target_cpu].lock);

    if (task->state == PROCESS_SLEEPING || task->state == PROCESS_BLOCKED) {
        if (task->state == PROCESS_SLEEPING) {
            process_t* prev = NULL;
            process_t* p    = sched_queues[target_cpu].sleep_queue;
            while (p) {
                if (p == task) {
                    if (prev) {
                        prev->sleep_next = p->sleep_next;
                    } else {
                        sched_queues[target_cpu].sleep_queue = p->sleep_next;
                    }
                    p->sleep_next = NULL;
                    break;
                }
                prev = p;
                p    = p->sleep_next;
            }
        }

        task->state    = PROCESS_READY;
        task->wake_tick = 0;
        __sched_enqueue_locked(target_cpu, task, task->priority, task->affinity);
    }

    spinlock_release(&sched_queues[target_cpu].lock, flags);
}

void sched_sleep_current(uint64_t ticks) {
    uint8_t    cpu_id = (uint8_t)get_cpu_id_fast();
    process_t* curr   = current_process[cpu_id];

    if (curr && curr->pid != 0) {
        uint64_t flags  = spinlock_acquire(&sched_queues[cpu_id].lock);
        uint64_t target = system_ticks + ticks;

        if (target < system_ticks) {
            target = 0xFFFFFFFFFFFFFFFFULL;
        }

        curr->state    = PROCESS_SLEEPING;
        curr->wake_tick = target;

        __sched_remove_locked(cpu_id, curr);

        curr->sleep_next                    = sched_queues[cpu_id].sleep_queue;
        sched_queues[cpu_id].sleep_queue    = curr;

        if (per_cpu_data[cpu_id]) {
            per_cpu_data[cpu_id]->pending_task = NULL;
        }

        spinlock_release(&sched_queues[cpu_id].lock, flags);
    }
}

static int get_cpu_distance(uint8_t cpu_a, uint8_t cpu_b) {
    if (cpu_a == cpu_b) return 0;
    if (cpu_topologies[cpu_a].package_id != cpu_topologies[cpu_b].package_id) return 3;
    if (cpu_topologies[cpu_a].core_id != cpu_topologies[cpu_b].core_id) return 2;
    return 1;
}

process_t* sched_pick_next(uint8_t cpu_id) {
    if (unlikely(cpu_id >= MAX_CPUS)) return NULL;

    uint64_t flags = spinlock_acquire(&sched_queues[cpu_id].lock);
    uint64_t now   = timer_get_ticks();
    uint32_t aq    = sched_queues[cpu_id].active_queues;

    if (aq != 0) {
        uint32_t check_aq = aq;
        while (check_aq) {
            int p = __builtin_ctz(check_aq); 
            process_t* front = sched_queues[cpu_id].queues[p].head;
            if (front) {
                if ((now > front->last_queued) && (now - front->last_queued > STARVATION_THRESHOLD)) {
                    __sched_remove_locked(cpu_id, front);
                    spinlock_release(&sched_queues[cpu_id].lock, flags);
                    return front;
                }
            }
            check_aq &= ~(1U << p);
        }

        int p = 31 - __builtin_clz(aq); 
        process_t* front = sched_queues[cpu_id].queues[p].head;
        if (front) {
            __sched_remove_locked(cpu_id, front);

            uint64_t weight = (uint64_t)(PRIO_COUNT - p);
            if (weight == 0) weight = 1;

            front->vruntime += (100 * 1024) / weight;
            if (front->vruntime > sched_queues[cpu_id].min_vruntime) {
                sched_queues[cpu_id].min_vruntime = front->vruntime;
            }

            spinlock_release(&sched_queues[cpu_id].lock, flags);
            return front;
        }
    }
    
    spinlock_release(&sched_queues[cpu_id].lock, flags);

    uint8_t best_victim  = 255;
    int     min_distance = 255;
    int     max_load     = 0;

    for (uint8_t i = 0; i < num_cpus; i++) {
        uint8_t target = (cpu_id + 1 + i) % num_cpus;
        if (target == cpu_id) continue;
        uint64_t remote_flags;
        if (spinlock_try_acquire(&sched_queues[target].lock, &remote_flags)) {
            int count = sched_queues[target].total_tasks;
            if (count > 0) {
                int dist = get_cpu_distance(cpu_id, target);
                if (dist == 1) {
                    best_victim = target;
                    spinlock_release(&sched_queues[target].lock, remote_flags);
                    break;
                } else if (count > max_load || (count == max_load && dist < min_distance)) {
                    max_load     = count;
                    min_distance = dist;
                    best_victim  = target;
                }
            }
            spinlock_release(&sched_queues[target].lock, remote_flags);
        }
    }

    if (best_victim != 255) {
        uint64_t remote_flags = spinlock_acquire(&sched_queues[best_victim].lock);
        uint32_t raq = sched_queues[best_victim].active_queues;
        if (raq != 0) {
            int p = 31 - __builtin_clz(raq);
            process_t* back = sched_queues[best_victim].queues[p].tail;
            if (back && (back->affinity == -1 || back->affinity == cpu_id)) {
                __sched_remove_locked(best_victim, back);
                spinlock_release(&sched_queues[best_victim].lock, remote_flags);
                return back;
            }
        }
        spinlock_release(&sched_queues[best_victim].lock, remote_flags);
    }

    return NULL;
}

uint64_t sched_get_load(uint8_t cpu_id) {
    if (cpu_id >= MAX_CPUS) return 0;
    return (uint64_t)sched_queues[cpu_id].total_tasks;
}

void sched_debug_dump(void) {
    serial_write("\n[SCHED-DUMP] Runqueue Status:\n");
    int total_pending = 0;
    for (int i = 0; i < num_cpus; i++) {
        int count = sched_queues[i].total_tasks;
        total_pending += count;
        if (count > 0) {
            char msg[64];
            // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
            snprintf(msg, sizeof(msg), "  CPU %d: %d tasks pending\n", i, count);
            serial_write(msg);
        }
    }
    char msg[64];
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    snprintf(msg, sizeof(msg), "  Total Pending Tasks: %d\n", total_pending);
    serial_write(msg);
    serial_write("[SCHED-DUMP] End.\n");
}

void init_multitasking(void) {
    init_process_manager();

    for (int i = 0; i < MAX_CPUS; i++) {
        // NOLINTNEXTLINE(clang-diagnostic-missing-field-initializers)
        spinlock_init(&sched_queues[i].lock);
        sched_queues[i].total_tasks  = 0;
        sched_queues[i].min_vruntime = 0;
        sched_queues[i].sleep_queue  = NULL;
        sched_queues[i].active_queues = 0;
        for (int p = 0; p < PRIO_COUNT; p++) {
            sched_queues[i].queues[p].head = NULL;
            sched_queues[i].queues[p].tail = NULL;
        }
    }

    scheduler_active    = false;
    process_t* idle     = create_idle_task(0);
    current_process[0]  = idle;
    idle_tasks[0]       = idle;
    
    print_status("[ TASK ]", "SCS (Scalable Core Scheduler) Initialized", "INFO");
}

void enable_scheduler(void) {
    scheduler_active = true;
    print_status("[ TASK ]", "Scheduler ENABLED", "INFO");
}

void init_multitasking_ap(void) {
    uint8_t cpu_id = (uint8_t)get_cpu_id_fast();
    if (cpu_id >= MAX_CPUS) return;
    process_t* idle     = create_idle_task(cpu_id);
    current_process[cpu_id] = idle;
    idle_tasks[cpu_id]  = idle;
    if (per_cpu_data[cpu_id]) {
        per_cpu_data[cpu_id]->current_process = current_process[cpu_id];
    }
}

void update_task_priority(process_t* proc, bool used_full_slice) {
    if (proc->pid == 0) return;
    
    int p = (int)proc->priority;
    if (!used_full_slice) {
        if (p < (int)PRIO_HIGH) {
            proc->priority = (kernel_prio_t)(p + 1);
        }
    } else {
        if (p > (int)PRIO_LOW) {
            proc->priority = (kernel_prio_t)(p - 1);
        }
    }
}

void check_pending_task(void) {
    uint8_t cpu_id = (uint8_t)get_cpu_id_fast();
    if (cpu_id >= MAX_CPUS || !per_cpu_data[cpu_id]) return;
    process_t* pending = (process_t*)per_cpu_data[cpu_id]->pending_task;
    per_cpu_data[cpu_id]->pending_task = NULL;

    if (pending && pending->state == PROCESS_READY) {
        sched_enqueue(cpu_id, pending, pending->priority, pending->affinity);
    }
}

__attribute__((no_stack_protector))
void schedule(void) {
    if (unlikely(global_panic_active)) return;

    // GÜVENLİK YAMASI: Blind STI Koruma Zırhı
    // Zamanlayıcıya girerken RFLAGS kaydedilir. Eğer iptal edilip dönülecekse
    // kesmeler SADECE başlangıçta açıksa açılır. Körlemesine STI atılmaz!
    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags) : : "memory");

    if (unlikely(!scheduler_active)) {
        if (rflags & 0x200) __asm__ volatile("sti" ::: "memory");
        return;
    }

    uint8_t cpu_id = (uint8_t)get_cpu_id_fast();
    if (unlikely(cpu_id >= MAX_CPUS)) {
        if (rflags & 0x200) __asm__ volatile("sti" ::: "memory");
        return;
    }

    if (per_cpu_data[cpu_id] && per_cpu_data[cpu_id]->preempt_count > 0) {
        if (rflags & 0x200) __asm__ volatile("sti" ::: "memory");
        return;
    }

    paging_check_tlb_flush(cpu_id);

    process_t* curr = current_process[cpu_id];

    if (curr && curr->state == PROCESS_RUNNING && curr->pid != 0) {
        curr->state = PROCESS_READY;
        if (per_cpu_data[cpu_id] && per_cpu_data[cpu_id]->pending_task) {
            process_t* old_pending = (process_t*)per_cpu_data[cpu_id]->pending_task;
            if (old_pending->state == PROCESS_READY) {
                sched_enqueue(cpu_id, old_pending, old_pending->priority, old_pending->affinity);
            }
        }
        if (per_cpu_data[cpu_id]) per_cpu_data[cpu_id]->pending_task = curr;
    } else {
        if (per_cpu_data[cpu_id]) per_cpu_data[cpu_id]->pending_task = NULL;
    }

    process_t* next = sched_pick_next(cpu_id);

    while (next) {
        if (next->state == PROCESS_ZOMBIE || next->state == PROCESS_DEAD) {
            next = sched_pick_next(cpu_id);
            continue;
        }

        bool active_elsewhere = false;
        for (int i = 0; i < num_cpus; i++) {
            if (i == cpu_id) continue;
            if (current_process[i] == next || 
               (per_cpu_data[i] && per_cpu_data[i]->switching_task == next)) {
                active_elsewhere = true;
                break;
            }
        }

        if (unlikely(active_elsewhere)) {
            sched_enqueue(cpu_id, next, next->priority, next->affinity);
            next = idle_tasks[cpu_id];
            break;
        }
        
        break;
    }

    if (unlikely(!next)) {
        next = idle_tasks[cpu_id];
        if (unlikely(!next)) {
            panic_at("scheduler.c", __LINE__, KERR_NULL_DEREFERENCE, "CRITICAL: No runnable task and IDLE task is NULL!");
            __builtin_unreachable();
        }
    }

    next->cpu_id = cpu_id;
    next->state  = PROCESS_RUNNING;

    if (next != curr) {
        if (per_cpu_data[cpu_id]) per_cpu_data[cpu_id]->switching_task = curr;
        current_process[cpu_id] = next;

        uint64_t kernel_stack_top = (uint64_t)next->stack_base + KERNEL_STACK_SIZE;
        if (per_cpu_data[cpu_id]) per_cpu_data[cpu_id]->kernel_stack = kernel_stack_top;
        set_kernel_stack(cpu_id, kernel_stack_top);

        prefetch_stack((void*)next->rsp);
        prefetch_stack(next->fxsave_region);

        switch_to_task(&curr->rsp, next->rsp, curr->fxsave_region, next->fxsave_region, next);

        if (per_cpu_data[cpu_id]) per_cpu_data[cpu_id]->switching_task = NULL;
    } else {
        if (rflags & 0x200) __asm__ volatile("sti" ::: "memory");
    }

    check_pending_task();
}

void yield(void) {
    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags) : : "memory");
    
    uint8_t    cpu_id = (uint8_t)get_cpu_id_fast();
    process_t* curr   = current_process[cpu_id];
    if (curr && curr->pid != 0) update_task_priority(curr, false);
    
    schedule();
    
    if (rflags & 0x200) {
        __asm__ volatile("sti" ::: "memory");
    }
}

void yield_cpu(void) { yield(); }

void process_tick(void) {
    if (unlikely(global_panic_active)) return;

    uint8_t cpu_id = (uint8_t)get_cpu_id_fast();
    if (cpu_id >= MAX_CPUS) return;

    cpu_tick_counts[cpu_id]++;

    if (current_process[cpu_id] && current_process[cpu_id]->pid == 0) {
        if (per_cpu_data[cpu_id]) per_cpu_data[cpu_id]->idle_ticks++;
    }

    if (cpu_id == 0) system_ticks++;

    uint64_t  flags = spinlock_acquire(&sched_queues[cpu_id].lock);
    process_t* prev = NULL;
    process_t* p    = sched_queues[cpu_id].sleep_queue;

    while (p) {
        process_t* next_p = p->sleep_next;
        if (system_ticks >= p->wake_tick) {
            if (prev) prev->sleep_next = next_p;
            else sched_queues[cpu_id].sleep_queue = next_p;

            p->sleep_next = NULL;
            p->state      = PROCESS_READY;
            p->wake_tick  = 0;

            __sched_enqueue_locked(cpu_id, p, p->priority, p->affinity);
        } else {
            prev = p;
        }
        p = next_p;
    }
    spinlock_release(&sched_queues[cpu_id].lock, flags);

    process_t* curr = current_process[cpu_id];
    if (curr && curr->pid != 0 && curr->state == PROCESS_RUNNING) {
        update_task_priority(curr, true);
    }
    schedule();
}

void timer_handler(registers_t* regs) {
    (void)regs; 
    profiler_tick(regs->rip);
    process_tick();
}

void task_sleep(uint64_t ticks) {
    if (!scheduler_active) {
        uint64_t target = system_ticks + ticks;
        while (system_ticks < target) {
            __asm__ volatile("pause");
        }
        return;
    } else {
        uint64_t rflags;
        __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags) : : "memory");
        sched_sleep_current(ticks);
        schedule();
        if (rflags & 0x200) {
            __asm__ volatile("sti" ::: "memory");
        }
    }
}