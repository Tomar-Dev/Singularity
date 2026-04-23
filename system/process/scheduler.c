// system/process/scheduler.c
#include "system/process/process.h"
#include "archs/cpu/x86_64/sync/spinlock.h"
#include "archs/cpu/x86_64/smp/smp.h"
#include "archs/cpu/x86_64/interrupts/isr.h"
#include "archs/cpu/cpu_hal.h"
#include "archs/cpu/x86_64/memory/paging.h"
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
#define SCHED_NO_CPU 255
#define SCHED_MAX_DISTANCE 255
#define SCHED_VRUNTIME_STEP (100 * 1024)

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

static void __sched_enqueue_locked(uint8_t cpu_id, process_t* task,
                                   kernel_prio_t prio, int16_t affinity) {
    if (task->is_queued) {
        return;
    } else {
        // Enqueue process safely
    }

    task->priority = prio;
    task->affinity = affinity;
    task->cpu_id   = cpu_id;

    int p = (int)prio;
    if (p >= PRIO_COUNT) {
        p = PRIO_COUNT - 1;
    } else {
        // Valid priority range
    }

    uint64_t now = hal_timer_get_ticks();
    task->last_queued = now;

    if (task->vruntime < sched_queues[cpu_id].min_vruntime) {
        task->vruntime = sched_queues[cpu_id].min_vruntime;
    } else {
        // vruntime already satisfies constraint
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
    } else {
        // Safe context
    }
    uint64_t flags = spinlock_acquire(&sched_queues[cpu_id].lock);
    __sched_enqueue_locked(cpu_id, task, prio, affinity);
    spinlock_release(&sched_queues[cpu_id].lock, flags);
}

static void __sched_remove_locked(uint8_t cpu_id, process_t* task) {
    if (!task->is_queued) {
        return;
    } else {
        // It is currently queued
    }

    int p = (int)task->priority;
    if (p >= PRIO_COUNT) {
        p = PRIO_COUNT - 1;
    } else {
        // Priority bounded
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
    } else {
        // Queue still has members
    }

    task->rq_next    = NULL;
    task->rq_prev    = NULL;
    task->is_queued  = false;

    if (sched_queues[cpu_id].total_tasks > 0) {
        sched_queues[cpu_id].total_tasks--;
    } else {
        // Underflow protection
    }
}

void sched_remove(uint8_t cpu_id, process_t* task) {
    if (unlikely(cpu_id >= MAX_CPUS || !task)) {
        return;
    } else {
        // Proceed securely
    }
    uint64_t flags = spinlock_acquire(&sched_queues[cpu_id].lock);
    __sched_remove_locked(cpu_id, task);
    spinlock_release(&sched_queues[cpu_id].lock, flags);
}

void sched_wake_task(process_t* task) {
    if (!task) {
        return;
    } else {
        // Has a valid task
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
                } else {
                    // Traverse sleep line
                }
                prev = p;
                p    = p->sleep_next;
            }
        } else {
            // Task is blocked, not in sleep chain
        }

        task->state    = PROCESS_READY;
        task->wake_tick = 0;
        __sched_enqueue_locked(target_cpu, task, task->priority, task->affinity);
    } else {
        // Unnecessary wakeup signal
    }

    spinlock_release(&sched_queues[target_cpu].lock, flags);
}

void sched_sleep_current(uint64_t ticks) {
    uint8_t    cpu_id = (uint8_t)get_cpu_id_fast();
    process_t* curr   = current_process[cpu_id];

    if (curr && curr->pid != 0) {
        uint64_t flags  = spinlock_acquire(&sched_queues[cpu_id].lock);
        
        uint64_t now_ticks = hal_timer_get_ticks();
        uint64_t target = now_ticks + ticks;

        if (target < now_ticks) {
            target = 0xFFFFFFFFFFFFFFFFULL;
        } else {
            // No tick overflow 
        }

        curr->state    = PROCESS_SLEEPING;
        curr->wake_tick = target;

        __sched_remove_locked(cpu_id, curr);

        curr->sleep_next                    = sched_queues[cpu_id].sleep_queue;
        sched_queues[cpu_id].sleep_queue    = curr;

        if (per_cpu_data[cpu_id]) {
            per_cpu_data[cpu_id]->pending_task = NULL;
        } else {
            // Uninitialized struct bounds
        }

        spinlock_release(&sched_queues[cpu_id].lock, flags);
    } else {
        // Idle task cannot sleep!
    }
}

static inline int get_cpu_distance(uint8_t cpu_a, uint8_t cpu_b) {
    if (cpu_a == cpu_b) { 
        return 0; 
    } else { 
        // Proceed 
    }
    
    cpu_topology_t* ta = &cpu_topologies[cpu_a];
    cpu_topology_t* tb = &cpu_topologies[cpu_b];
    
    if (ta->package_id != tb->package_id) { 
        return 3; 
    } else { 
        // Closer 
    }
    
    if (ta->core_id != tb->core_id) { 
        return 2; 
    } else { 
        // Nearest 
    }
    
    return 1;
}

process_t* sched_pick_next(uint8_t cpu_id) {
    if (unlikely(cpu_id >= MAX_CPUS)) { 
        return NULL; 
    } else { 
        // Bound check passed 
    }

    uint64_t flags = spinlock_acquire(&sched_queues[cpu_id].lock);
    uint64_t now   = hal_timer_get_ticks();
    uint32_t aq    = sched_queues[cpu_id].active_queues;

    if (aq != 0) {
        uint32_t check_aq = aq;
        while (check_aq) {
            int p = 31 - __builtin_clz(check_aq); 
            process_t* front = sched_queues[cpu_id].queues[p].head;
            if (front) {
                if ((now > front->last_queued) && (now - front->last_queued > STARVATION_THRESHOLD)) {
                    __sched_remove_locked(cpu_id, front);
                    spinlock_release(&sched_queues[cpu_id].lock, flags);
                    return front;
                } else {
                    // Process didn't starve long enough to preempt strictly
                }
            } else {
                // Ghost bit enabled
            }
            check_aq &= ~(1U << p);
        }

        int p = 31 - __builtin_clz(aq); 
        process_t* front = sched_queues[cpu_id].queues[p].head;
        if (front) {
            __sched_remove_locked(cpu_id, front);

            uint64_t weight = (uint64_t)(PRIO_COUNT - p);
            if (weight == 0) { 
                weight = 1; 
            } else { 
                // Weight normalized 
            }

            front->vruntime += SCHED_VRUNTIME_STEP / weight;
            if (front->vruntime > sched_queues[cpu_id].min_vruntime) {
                sched_queues[cpu_id].min_vruntime = front->vruntime;
            } else {
                // Kept bounds
            }

            spinlock_release(&sched_queues[cpu_id].lock, flags);
            return front;
        } else {
            // Safety fallback
        }
    } else {
        // Proceed to Work Stealing
    }
    
    spinlock_release(&sched_queues[cpu_id].lock, flags);

    uint8_t best_victim  = SCHED_NO_CPU;
    int     min_distance = SCHED_MAX_DISTANCE;
    int     max_load     = 0;

    uint8_t start_cpu = (uint8_t)(hal_timer_get_ticks() % num_cpus);

    for (uint8_t i = 0; i < num_cpus; i++) {
        uint8_t target = (start_cpu + i) % num_cpus;
        if (target == cpu_id) { 
            continue; 
        } else { 
            // Other node selected 
        }
        
        int count = __atomic_load_n(&sched_queues[target].total_tasks, __ATOMIC_RELAXED);
        
        if (count <= 1) { 
            continue; 
        } else { 
            // Viable Stealing Target Found 
        }

        uint64_t remote_flags;
        if (spinlock_try_acquire(&sched_queues[target].lock, &remote_flags)) {
            count = sched_queues[target].total_tasks;
            if (count > 1) {
                int dist = get_cpu_distance(cpu_id, target);
                if (dist == 1) {
                    best_victim = target;
                    spinlock_release(&sched_queues[target].lock, remote_flags);
                    break;
                } else if (count > max_load || (count == max_load && dist < min_distance)) {
                    max_load     = count;
                    min_distance = dist;
                    best_victim  = target;
                } else {
                    // Inferior target, looking on...
                }
            } else {
                // Load dropped between lock operations
            }
            spinlock_release(&sched_queues[target].lock, remote_flags);
        } else {
            // Livelock Avoidance: Target is busy modifying its queue, skip.
        }
    }

    if (best_victim != SCHED_NO_CPU) {
        uint64_t remote_flags = spinlock_acquire(&sched_queues[best_victim].lock);
        uint32_t raq = sched_queues[best_victim].active_queues;
        if (raq != 0) {
            int p = 31 - __builtin_clz(raq);
            process_t* back = sched_queues[best_victim].queues[p].tail;
            if (back && (back->affinity == -1 || back->affinity == cpu_id)) {
                __sched_remove_locked(best_victim, back);
                spinlock_release(&sched_queues[best_victim].lock, remote_flags);
                return back;
            } else {
                // Task is bound to the target CPU via affinity. Cannot steal.
            }
        } else {
            // Active Queues suddenly dried up
        }
        spinlock_release(&sched_queues[best_victim].lock, remote_flags);
    } else {
        // No stealable threads in the entire SMP grid
    }

    return NULL;
}

uint64_t sched_get_load(uint8_t cpu_id) {
    if (cpu_id >= MAX_CPUS) { 
        return 0; 
    } else { 
        return (uint64_t)sched_queues[cpu_id].total_tasks; 
    }
}

void sched_debug_dump(void) {
    serial_write("\n[SCHED-DUMP] Runqueue Status:\n");
    int total_pending = 0;
    for (int i = 0; i < num_cpus; i++) {
        int count = sched_queues[i].total_tasks;
        total_pending += count;
        if (count > 0) {
            char msg[64];
            snprintf(msg, sizeof(msg), "  CPU %d: %d tasks pending\n", i, count);
            serial_write(msg);
        } else {
            // CPU clear
        }
    }
    char msg[64];
    snprintf(msg, sizeof(msg), "  Total Pending Tasks: %d\n", total_pending);
    serial_write(msg);
    serial_write("[SCHED-DUMP] End.\n");
}

void init_multitasking(void) {
    init_process_manager();

    for (int i = 0; i < MAX_CPUS; i++) {
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
    if (cpu_id >= MAX_CPUS) { 
        return; 
    } else { 
        // Init Task Process 
    }
    
    process_t* idle     = create_idle_task(cpu_id);
    current_process[cpu_id] = idle;
    idle_tasks[cpu_id]  = idle;
    
    if (per_cpu_data[cpu_id]) {
        per_cpu_data[cpu_id]->current_process = current_process[cpu_id];
    } else {
        // Critical GS mapping fail
    }
}

void update_task_priority(process_t* proc, bool used_full_slice) {
    if (proc->pid == 0 || (proc->flags & PROC_FLAG_CRITICAL)) { 
        return; 
    } else { 
        // Proceed to demote/promote 
    }
    
    int p = (int)proc->priority;
    int bp = (int)proc->base_priority;
    
    if (!used_full_slice) {
        if (p < (int)PRIO_HIGH) {
            proc->priority = (kernel_prio_t)(p + 1);
        } else {
            // Already Max Prio
        }
    } else {
        if (p > bp) {
            proc->priority = (kernel_prio_t)(p - 1);
        } else if (p > (int)PRIO_IDLE + 1) {
            proc->priority = (kernel_prio_t)(p - 1);
        } else {
            // Already Min Prio
        }
    }
}

void check_pending_task(void) {
    uint8_t cpu_id = (uint8_t)get_cpu_id_fast();
    if (cpu_id >= MAX_CPUS || !per_cpu_data[cpu_id]) { 
        return; 
    } else { 
        // Check Ghost Tasks 
    }
    
    process_t* pending = (process_t*)per_cpu_data[cpu_id]->pending_task;
    per_cpu_data[cpu_id]->pending_task = NULL;

    if (pending && pending->state == PROCESS_READY) {
        sched_enqueue(cpu_id, pending, pending->priority, pending->affinity);
    } else {
        // Task either exited or properly destroyed
    }
}

__attribute__((no_stack_protector))
void schedule(void) {
    if (unlikely(global_panic_active)) { 
        return; 
    } else { 
        // Scheduler permitted 
    }

    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags) : : "memory");

    if (unlikely(!scheduler_active)) {
        if (rflags & 0x200) { 
            __asm__ volatile("sti" ::: "memory"); 
        } else { 
            // Proceed Muted 
        }
        return;
    } else {
        // Safe to context switch
    }

    uint8_t cpu_id = (uint8_t)get_cpu_id_fast();
    if (unlikely(cpu_id >= MAX_CPUS)) {
        if (rflags & 0x200) { 
            __asm__ volatile("sti" ::: "memory"); 
        } else { 
            // Check Rflags 
        }
        return;
    } else {
        // Legitimate execution node
    }

    if (per_cpu_data[cpu_id] && per_cpu_data[cpu_id]->preempt_count > 0) {
        if (rflags & 0x200) { 
            __asm__ volatile("sti" ::: "memory"); 
        } else { 
            // Skip context switch (RCU Read lock active) 
        }
        return;
    } else {
        // Pass through
    }

    paging_check_tlb_flush(cpu_id);

    process_t* curr = current_process[cpu_id];

    if (curr && curr->state == PROCESS_RUNNING && curr->pid != 0) {
        curr->state = PROCESS_READY;
        if (per_cpu_data[cpu_id] && per_cpu_data[cpu_id]->pending_task) {
            process_t* old_pending = (process_t*)per_cpu_data[cpu_id]->pending_task;
            if (old_pending->state == PROCESS_READY) {
                sched_enqueue(cpu_id, old_pending, old_pending->priority, old_pending->affinity);
            } else {
                // Not runnable
            }
        } else {
            // No pending collisions
        }
        if (per_cpu_data[cpu_id]) { 
            per_cpu_data[cpu_id]->pending_task = curr; 
        } else { 
            // Failsafe 
        }
    } else {
        if (per_cpu_data[cpu_id]) { 
            per_cpu_data[cpu_id]->pending_task = NULL; 
        } else { 
            // Failsafe 
        }
    }

    process_t* next = sched_pick_next(cpu_id);

    while (next) {
        if (next->state == PROCESS_ZOMBIE || next->state == PROCESS_DEAD) {
            next = sched_pick_next(cpu_id);
            continue;
        } else {
            // Task is ready
        }

        bool active_elsewhere = false;
        for (int i = 0; i < num_cpus; i++) {
            if (i == cpu_id) { 
                continue; 
            } else { 
                // Inspect other nodes 
            }
            
            if (current_process[i] == next || 
               (per_cpu_data[i] && per_cpu_data[i]->switching_task == next)) {
                active_elsewhere = true;
                break;
            } else {
                // Cross-core isolation passed
            }
        }

        if (unlikely(active_elsewhere)) {
            sched_enqueue(cpu_id, next, next->priority, next->affinity);
            next = idle_tasks[cpu_id];
            break;
        } else {
            // Guaranteed single execution thread constraint holds.
        }
        
        break;
    }

    if (unlikely(!next)) {
        next = idle_tasks[cpu_id];
        if (unlikely(!next)) {
            panic_at("scheduler.c", __LINE__, KERR_NULL_DEREFERENCE, "CRITICAL: No runnable task and IDLE task is NULL!");
            __builtin_unreachable();
        } else {
            // Rescued by Idle node
        }
    } else {
        // Proceeding smoothly with task 
    }

    next->cpu_id = cpu_id;
    next->state  = PROCESS_RUNNING;

    if (next != curr) {
        if (per_cpu_data[cpu_id]) { 
            per_cpu_data[cpu_id]->switching_task = curr; 
            per_cpu_data[cpu_id]->stack_canary = next->stack_canary; 
        } else { 
            // Valid struct 
        }
        current_process[cpu_id] = next;

        uint64_t kernel_stack_top = (uint64_t)next->stack_base + KERNEL_STACK_SIZE;
        if (per_cpu_data[cpu_id]) { 
            per_cpu_data[cpu_id]->kernel_stack = kernel_stack_top; 
        } else { 
            // Valid 
        }
        set_kernel_stack(cpu_id, kernel_stack_top);

        prefetch_stack((void*)next->rsp);
        prefetch_stack(next->fxsave_region);

        switch_to_task(&curr->rsp, next->rsp, curr->fxsave_region, next->fxsave_region, next);

        if (per_cpu_data[cpu_id]) { 
            per_cpu_data[cpu_id]->switching_task = NULL; 
        } else { 
            // Switch complete 
        }
    } else {
        if (rflags & 0x200) { 
            __asm__ volatile("sti" ::: "memory"); 
        } else { 
            // Unmodified 
        }
    }

    check_pending_task();
}

void yield(void) {
    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags) : : "memory");
    
    uint8_t    cpu_id = (uint8_t)get_cpu_id_fast();
    process_t* curr   = current_process[cpu_id];
    if (curr && curr->pid != 0) { 
        update_task_priority(curr, false); 
    } else { 
        // Idle skip 
    }
    
    schedule();
    
    if (rflags & 0x200) {
        __asm__ volatile("sti" ::: "memory");
    } else {
        // Do nothing to avoid breaking lock guarantees!
    }
}

void yield_cpu(void) { 
    yield(); 
}

void process_tick(void) {
    if (unlikely(global_panic_active)) { 
        return; 
    } else { 
        // Tick active 
    }

    uint8_t cpu_id = (uint8_t)get_cpu_id_fast();
    if (cpu_id >= MAX_CPUS) { 
        return; 
    } else { 
        // Valid 
    }

    cpu_tick_counts[cpu_id]++;

    if (current_process[cpu_id] && current_process[cpu_id]->pid == 0) {
        if (per_cpu_data[cpu_id]) { 
            per_cpu_data[cpu_id]->idle_ticks++; 
        } else { 
            // Verified structure 
        }
    } else {
        // Target is busy, idle stays put
    }

    if (cpu_id == 0) {
        extern volatile uint64_t system_ticks;
        system_ticks++; 
    } else {
        // Only core 0 manages monolithic global timer reference
    }

    uint64_t now_ticks = hal_timer_get_ticks();

    uint64_t  flags = spinlock_acquire(&sched_queues[cpu_id].lock);
    process_t* prev = NULL;
    process_t* p    = sched_queues[cpu_id].sleep_queue;

    while (p) {
        process_t* next_p = p->sleep_next;
        if (now_ticks >= p->wake_tick) {
            if (prev) { 
                prev->sleep_next = next_p; 
            } else { 
                sched_queues[cpu_id].sleep_queue = next_p; 
            }

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
    } else {
        // Pass
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
        uint64_t now_ticks = hal_timer_get_ticks();
        uint64_t target = now_ticks + ticks;
        while (hal_timer_get_ticks() < target) {
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
        } else {
            // Keep sealed
        }
    }
}