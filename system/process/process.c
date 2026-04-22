// system/process/process.c
#include "system/process/process.h"
#include "memory/kheap.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "archs/cpu/x86_64/sync/spinlock.h"
#include "archs/cpu/x86_64/smp/smp.h"
#include "archs/cpu/x86_64/interrupts/isr.h"
#include "archs/cpu/cpu_hal.h"
#include "archs/cpu/x86_64/memory/paging.h"
#include "kernel/fastops.h"
#include "kernel/profiler.h"
#include "archs/cpu/x86_64/context/tss.h"
#include "kernel/config.h"
#include "archs/cpu/x86_64/core/topology.h"
#include "kernel/debug.h"
#include "system/process/reaper.hpp"
#include "system/security/rng.h" 

#define KERNEL_STACK_PAGES (KERNEL_STACK_SIZE / PAGE_SIZE)
#define PROCESS_T_ALIGN 64

extern void print_status(const char* prefix, const char* msg, const char* status);
extern uint8_t num_cpus;
extern void prefetch_stack(void* ptr);
extern uintptr_t __stack_chk_guard; 

process_t*        process_list_head          = NULL;
process_t*        process_list_tail          = NULL;
rwlock_t          task_list_lock             = NULL; 
process_t*        current_process[MAX_CPUS]  = {0};
process_t*        idle_tasks[MAX_CPUS]       = {0};
volatile uint64_t system_ticks               = 0;
volatile uint64_t cpu_tick_counts[MAX_CPUS]  = {0};
bool              scheduler_active           = false;
static uint64_t   next_pid                   = 1;

#define MAX_STACK_POOL_SIZE 32
static void* stack_pool[MAX_STACK_POOL_SIZE];
static int stack_pool_top = 0;
static spinlock_t stack_pool_lock = {0, 0, {0}};

void wait_queue_init(wait_queue_t* q) {
    if (q) {
        spinlock_init(&q->lock);
        q->head = NULL;
        q->tail = NULL;
    } else {
        // Pointer failure
    }
}

void wait_queue_push(wait_queue_t* q, process_t* task) {
    if (q && task) {
        uint64_t flags = spinlock_acquire(&q->lock);
        task->wait_next = NULL;
        if (!q->head) {
            q->head = task;
            q->tail = task;
        } else {
            q->tail->wait_next = task;
            q->tail = task;
        }
        spinlock_release(&q->lock, flags);
    } else {
        // Ignored
    }
}

process_t* wait_queue_pop_safe(wait_queue_t* q, uint32_t* skipped_count) {
    if (!q) { 
        return NULL; 
    } else { 
        // Execute 
    }
    
    uint64_t flags = spinlock_acquire(&q->lock);
    uint32_t skipped = 0;
    
    while (q->head) {
        process_t* task = q->head;
        q->head = task->wait_next;
        
        if (!q->head) {
            q->tail = NULL;
        } else {
            // Moving queue sequence
        }
        
        task->wait_next = NULL;

        if (task->state == PROCESS_ZOMBIE || task->state == PROCESS_DEAD) {
            skipped++;
            continue;
        } else {
            spinlock_release(&q->lock, flags);
            if (skipped_count) { 
                *skipped_count = skipped; 
            } else { 
                // Skipped parameter is NULL 
            }
            return task;
        }
    }
    
    spinlock_release(&q->lock, flags);
    if (skipped_count) { 
        *skipped_count = skipped; 
    } else { 
        // Parameter ignored 
    }
    return NULL;
}

static void* alloc_kernel_stack() {
    uint64_t flags = spinlock_acquire(&stack_pool_lock);
    if (stack_pool_top > 0) {
        stack_pool_top--;
        void* stack = stack_pool[stack_pool_top];
        spinlock_release(&stack_pool_lock, flags);
        return stack;
    } else {
        // Pool is completely drained. Request memory hardware allocation.
    }
    spinlock_release(&stack_pool_lock, flags);
    return vmm_alloc_stack(KERNEL_STACK_PAGES);
}

void free_kernel_stack(void* stack) {
    if (!stack) { 
        return; 
    } else { 
        // Valid memory area 
    }
    
    uint64_t flags = spinlock_acquire(&stack_pool_lock);
    if (stack_pool_top < MAX_STACK_POOL_SIZE) {
        stack_pool[stack_pool_top] = stack;
        stack_pool_top++;
        spinlock_release(&stack_pool_lock, flags);
    } else {
        spinlock_release(&stack_pool_lock, flags);
        vmm_free_stack(stack, KERNEL_STACK_PAGES);
    }
}

void init_process_manager() {
    spinlock_init(&stack_pool_lock);
    task_list_lock = rwlock_create(); 
    
    for(int i=0; i<8; i++) {
        void* stack = vmm_alloc_stack(KERNEL_STACK_PAGES);
        if(stack) {
            stack_pool[stack_pool_top++] = stack;
        } else {
            // Out of memory bounds
        }
    }
}

void oom_reaper_scan() {
    serial_write("[OOM] Reaper Scanning... Looking for safe victims.\n");
    
    if (task_list_lock) { 
        rwlock_acquire_write(task_list_lock); 
    } else { 
        // Lock not init'ed 
    }
    
    process_t* victim = NULL;
    uint64_t max_badness = 0;
    process_t* curr = process_list_head;
    
    if (curr) {
        do {
            if (curr->pid > 10 && curr->state != PROCESS_ZOMBIE && curr->state != PROCESS_DEAD) {
                if (curr->flags & PROC_FLAG_CRITICAL) { 
                    curr = curr->next; 
                    continue; 
                } else { 
                    // Safe victim candidate 
                }
                
                if (curr->priority >= PRIO_HIGH) { 
                    curr = curr->next; 
                    continue; 
                } else { 
                    // Priority checks out 
                }

                uint64_t badness = curr->used_pages * 10;
                
                if (curr->priority == PRIO_IDLE) { 
                    badness += OOM_PENALTY_IDLE; 
                } else if (curr->priority == PRIO_LOW) { 
                    badness += OOM_PENALTY_LOW; 
                } else { 
                    // Minimal badness added 
                }
                
                uint64_t age = hal_timer_get_ticks() - curr->start_tick;
                if (age < OOM_YOUNG_TICKS) { 
                    badness += OOM_PENALTY_YOUNG; 
                } else { 
                    // Stable aged task 
                }
                
                if (badness > max_badness) {
                    max_badness = badness;
                    victim = curr;
                } else {
                    // Older or more suitable victims present
                }
            } else {
                // Pre-checked system critical or zombie nodes.
            }
            curr = curr->next;
        } while (curr != process_list_head);
    } else {
        // Head structure missing
    }
    
    if (task_list_lock) { 
        rwlock_release_write(task_list_lock); 
    } else { 
        // Lock missing 
    }
    
    if (victim) {
        char buf[128];
        snprintf(buf, sizeof(buf), "[OOM] Sacrificing PID %lu (Score: %lu) to save system.\n", victim->pid, max_badness);
        serial_write(buf);
        victim->state = PROCESS_ZOMBIE;
        sched_remove(victim->cpu_id, victim);
    } else {
        serial_write("[OOM] CRITICAL: No safe victim found! System may deadlock.\n");
    }
    reaper_invoke();
}

__attribute__((no_stack_protector))
void kthread_starter() {
    uint8_t cpu_id = (uint8_t)get_cpu_id_fast();
    
    if (per_cpu_data[cpu_id]) {
        per_cpu_data[cpu_id]->switching_task = NULL;
    } else {
        // CPU structure hasn't loaded fully
    }
    
    check_pending_task();
    __asm__ volatile("sti");
    
    process_t* curr = current_process[cpu_id];
    
    if (likely(curr && curr->thread_fn)) {
        curr->thread_fn();
    } else {
        serial_write("[SCHED] CRITICAL: Task with NULL thread_fn attempted to run!\n");
    }
    
    process_exit();
}

static void idle_task_fn() {
    while (1) { 
        __asm__ volatile("sti; hlt"); 
        yield(); 
    }
}

process_t* create_idle_task(uint8_t cpu_id) {
    process_t* idle = (process_t*)kmalloc_aligned(sizeof(process_t), PROCESS_T_ALIGN);
    if (unlikely(!idle)) { 
        return NULL; 
    } else { 
        // Verified and mapped 
    }
    
    memset(idle, 0, sizeof(process_t));
    idle->pid = 0; 
    idle->parent_id = 0;
    idle->state = PROCESS_RUNNING;
    idle->cpu_id = cpu_id;
    idle->priority = PRIO_IDLE;
    idle->base_priority = PRIO_IDLE;
    idle->affinity = cpu_id; 
    idle->thread_fn = idle_task_fn;
    idle->flags = PROC_FLAG_KERNEL | PROC_FLAG_CRITICAL;
    
    idle->stack_canary = get_secure_random();
    if (idle->stack_canary == 0) { idle->stack_canary = __stack_chk_guard; } else { /* Valid */ }
    
    memset(idle->fxsave_region, 0, FPU_BUFFER_SIZE);
    *((uint16_t*)&idle->fxsave_region[0]) = 0x037F; 
    extern uint32_t fpu_default_mxcsr;
    *((uint32_t*)&idle->fxsave_region[24]) = fpu_default_mxcsr;
    
    uint64_t current_rsp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(current_rsp));
    idle->rsp = current_rsp;
    
    if (task_list_lock) { 
        rwlock_acquire_write(task_list_lock); 
    } else { 
        // No task lock deployed yet 
    }
    
    if (process_list_tail) {
        idle->next = process_list_head;
        process_list_tail->next = idle;
        process_list_tail = idle;
    } else {
        process_list_head = idle;
        process_list_tail = idle;
        idle->next = idle;
    }
    
    if (task_list_lock) { 
        rwlock_release_write(task_list_lock); 
    } else { 
        // Clean lock release 
    }
    
    return idle;
}

static int create_task_internal(void (*entry_point)(), kernel_prio_t prio, int16_t affinity) {
    process_t* new_proc = (process_t*)kmalloc_aligned(sizeof(process_t), PROCESS_T_ALIGN);
    if (unlikely(!new_proc)) { 
        oom_reaper_scan(); 
        new_proc = (process_t*)kmalloc_aligned(sizeof(process_t), PROCESS_T_ALIGN);
        if (!new_proc) { 
            return 0; 
        } else { 
            // Harvest succeeded 
        }
    } else {
        // Proceed with regular task setup
    }

    uint8_t current_cpu = (uint8_t)get_cpu_id_fast();
    process_t* parent = current_process[current_cpu];

    memset(new_proc, 0, sizeof(process_t));
    new_proc->pid = next_pid++;
    if (next_pid == 0) { 
        next_pid = 1; 
    } else { 
        // Linear incrementation 
    }
    
    if (parent && parent->pid != 0) { 
        new_proc->parent_id = parent->pid; 
    } else { 
        new_proc->parent_id = 0; 
    }
    
    new_proc->flags = PROC_FLAG_KERNEL;
    if (prio >= PRIO_HIGH) { 
        new_proc->flags |= PROC_FLAG_CRITICAL; 
    } else { 
        // Non-vital assignment 
    }
    
    new_proc->state = PROCESS_READY;
    new_proc->thread_fn = entry_point;
    new_proc->priority = prio; 
    new_proc->base_priority = prio;
    new_proc->affinity = affinity;
    new_proc->wake_tick = 0; 
    new_proc->start_tick = hal_timer_get_ticks();
    new_proc->used_pages = KERNEL_STACK_PAGES; 
    new_proc->is_queued = false;
    
    new_proc->stack_canary = get_secure_random() ^ ((uint64_t)new_proc->pid << 32) ^ hal_timer_get_ticks();
    if (new_proc->stack_canary == 0) { new_proc->stack_canary = 0xDEADBEEFCAFEBABEULL; } else { /* Valid */ }
    
    static volatile uint32_t rr_cpu = 0;
    uint8_t target_cpu;
    if (affinity != -1) {
        target_cpu = (uint8_t)affinity;
    } else {
        if (num_cpus == 0) { 
            target_cpu = 0; 
        } else { 
            target_cpu = (uint8_t)(__atomic_fetch_add(&rr_cpu, 1, __ATOMIC_RELAXED) % num_cpus); 
        }
    }
    new_proc->cpu_id = target_cpu;

    memset(new_proc->fxsave_region, 0, FPU_BUFFER_SIZE);
    *((uint16_t*)&new_proc->fxsave_region[0]) = 0x037F; 
    extern uint32_t fpu_default_mxcsr;
    *((uint32_t*)&new_proc->fxsave_region[24]) = fpu_default_mxcsr;

    void* stack_addr = alloc_kernel_stack();
    if (unlikely(!stack_addr)) {
        reaper_invoke();
        stack_addr = alloc_kernel_stack();
        if (!stack_addr) { 
            kfree_aligned(new_proc); 
            return 0; 
        } else {
            new_proc->stack_base = stack_addr;
        }
    } else {
        new_proc->stack_base = stack_addr;
    }
    
    uint64_t random_pad = (get_secure_random() % 512) & ~15ULL;
    uint64_t* stack_top = (uint64_t*)((uint64_t)stack_addr + KERNEL_STACK_SIZE - random_pad);
    
    setup_stack_frame(stack_top, entry_point, NULL, &new_proc->rsp);

    if (task_list_lock) { 
        rwlock_acquire_write(task_list_lock); 
    } else { 
        // Free flow 
    }
    
    if (!process_list_head) {
        process_list_head = new_proc;
        process_list_tail = new_proc;
        new_proc->next = new_proc;
    } else {
        new_proc->next = process_list_head;
        process_list_tail->next = new_proc;
        process_list_tail = new_proc;
    }
    
    if (task_list_lock) { 
        rwlock_release_write(task_list_lock); 
    } else { 
        // Clean flow 
    }
    
    sched_enqueue(target_cpu, new_proc, prio, affinity);
    return 1;
}

int create_kernel_task_prio(void (*entry_point)(), kernel_prio_t prio) { 
    return create_task_internal(entry_point, prio, -1); 
}

int create_kernel_task_pinned(void (*entry_point)(), kernel_prio_t prio, int cpu_id) { 
    return create_task_internal(entry_point, prio, (int16_t)cpu_id); 
}

void create_kernel_task(void (*entry_point)()) { 
    create_kernel_task_prio(entry_point, PRIO_NORMAL); 
}

__attribute__((no_stack_protector))
void process_exit() {
    uint8_t cpu_id = (uint8_t)get_cpu_id_fast();
    __asm__ volatile("cli");
    process_t* curr = current_process[cpu_id];
    
    if (unlikely(!scheduler_active || !curr || curr->pid == 0)) {
        panic_at("process.c", __LINE__, KERR_UNKNOWN, "CRITICAL: Attempted to kill PID 0 or exit before scheduler start! (Silent Hang Prevented)");
    } else {
        // Ready for clean destruction
    }
    
    curr->state = PROCESS_ZOMBIE;
    sched_remove(cpu_id, curr);
    
    if (per_cpu_data[cpu_id]) {
        per_cpu_data[cpu_id]->pending_task = NULL;
    } else {
        // Fallback state mapping preserved
    }
    
    reaper_invoke();
    schedule();
    while(1) { hal_cpu_halt(); }
}

void reaper_invoke() { 
    extern void signal_reaper();
    signal_reaper(); 
}

void process_get_info(int* total, int* running, int* zombie) {
    *total = 0; *running = 0; *zombie = 0;
    if (task_list_lock) { 
        rwlock_acquire_read(task_list_lock); 
    } else { 
        // No lock mechanism deployed yet 
    }
    
    process_t* curr = process_list_head;
    if (curr) {
        do {
            (*total)++;
            if (curr->state == PROCESS_RUNNING) { 
                (*running)++; 
            } else { 
                // Waiting or Zombie 
            }
            
            if (curr->state == PROCESS_ZOMBIE || curr->state == PROCESS_DEAD) { 
                (*zombie)++; 
            } else { 
                // Structurally Alive 
            }
            curr = curr->next;
        } while (curr != process_list_head);
    } else {
        // No threads detected
    }
    
    if (task_list_lock) { 
        rwlock_release_read(task_list_lock); 
    } else { 
        // Clean bypass 
    }
}

int process_get_zombie_count() {
    int zombie = 0;
    if (task_list_lock) { 
        rwlock_acquire_read(task_list_lock); 
    } else { 
        // Valid operation unhindered 
    }
    
    process_t* curr = process_list_head;
    if (curr) {
        do {
            if (curr->state == PROCESS_ZOMBIE || curr->state == PROCESS_DEAD) { 
                zombie++; 
            } else { 
                // Operational Node 
            }
            curr = curr->next;
        } while (curr != process_list_head);
    } else {
        // System clear
    }
    
    if (task_list_lock) { 
        rwlock_release_read(task_list_lock); 
    } else { 
        // Complete 
    }
    return zombie;
}