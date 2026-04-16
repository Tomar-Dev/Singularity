// system/process/process.c
#include "system/process/process.h"
#include "memory/kheap.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "archs/cpu/x86_64/sync/spinlock.h"
#include "archs/cpu/x86_64/smp/smp.h"
#include "archs/cpu/x86_64/interrupts/isr.h"
#include "drivers/serial/serial.h"
#include "memory/paging.h"
#include "kernel/fastops.h"
#include "kernel/profiler.h"
#include "archs/cpu/x86_64/context/tss.h"
#include "kernel/config.h"
#include "archs/cpu/x86_64/core/topology.h"
#include "kernel/debug.h"
#include "system/process/reaper.hpp"

#define KERNEL_STACK_PAGES (KERNEL_STACK_SIZE / PAGE_SIZE)
#define PROCESS_T_ALIGN 64

extern uint64_t timer_get_ticks();
extern void print_status(const char* prefix, const char* msg, const char* status);
extern uint8_t num_cpus;
extern void prefetch_stack(void* ptr);

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
    spinlock_init(&q->lock);
    q->head = NULL;
    q->tail = NULL;
}

void wait_queue_push(wait_queue_t* q, process_t* task) {
    // MİMARİ NOT: q->lock bir "Leaf Lock" (Yaprak Kilit)'tir. 
    // Daha üst seviye kilitler (rwlock->lock veya mutex->lock) tutulurken 
    // alınması güvenlidir ve AB-BA Deadlock yaratmaz.
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
}

// FIX: Race Condition önlendi. Sayacı dışarıdan değiştirmek yerine atlanan görev sayısını döner.
process_t* wait_queue_pop_safe(wait_queue_t* q, uint32_t* skipped_count) {
    uint64_t flags = spinlock_acquire(&q->lock);
    uint32_t skipped = 0;
    
    while (q->head) {
        process_t* task = q->head;
        q->head = task->wait_next;
        
        if (!q->head) {
            q->tail = NULL;
        }
        
        task->wait_next = NULL;

        if (task->state == PROCESS_ZOMBIE || task->state == PROCESS_DEAD) {
            skipped++;
            continue;
        } else {
            spinlock_release(&q->lock, flags);
            if (skipped_count) *skipped_count = skipped;
            return task;
        }
    }
    
    spinlock_release(&q->lock, flags);
    if (skipped_count) *skipped_count = skipped;
    return NULL;
}

static void* alloc_kernel_stack() {
    uint64_t flags = spinlock_acquire(&stack_pool_lock);
    if (stack_pool_top > 0) {
        stack_pool_top--;
        void* stack = stack_pool[stack_pool_top];
        spinlock_release(&stack_pool_lock, flags);
        return stack;
    }
    spinlock_release(&stack_pool_lock, flags);
    return vmm_alloc_stack(KERNEL_STACK_PAGES);
}

void free_kernel_stack(void* stack) {
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
        }
    }
}

void oom_reaper_scan() {
    serial_write("[OOM] Reaper Scanning... Looking for safe victims.\n");
    
    if (task_list_lock) rwlock_acquire_write(task_list_lock); 
    process_t* victim = NULL;
    uint64_t max_badness = 0;
    process_t* curr = process_list_head;
    
    if (curr) {
        do {
            if (curr->pid > 10 && curr->state != PROCESS_ZOMBIE && curr->state != PROCESS_DEAD) {
                if (curr->flags & PROC_FLAG_CRITICAL) { curr = curr->next; continue; }
                if (curr->priority >= PRIO_HIGH) { curr = curr->next; continue; }

                uint64_t badness = curr->used_pages * 10;
                
                if (curr->priority == PRIO_IDLE) badness += 1000;
                else if (curr->priority == PRIO_LOW) badness += 500;
                
                uint64_t age = timer_get_ticks() - curr->start_tick;
                if (age < 500) badness += 200; 
                
                if (badness > max_badness) {
                    max_badness = badness;
                    victim = curr;
                }
            }
            curr = curr->next;
        } while (curr != process_list_head);
    }
    if (task_list_lock) rwlock_release_write(task_list_lock);
    
    if (victim) {
        char buf[128];
        // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
        sprintf(buf, "[OOM] Sacrificing PID %lu (Score: %lu) to save system.\n", victim->pid, max_badness);
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
    while (1) { __asm__ volatile("sti; hlt"); yield(); }
}

process_t* create_idle_task(uint8_t cpu_id) {
    process_t* idle = (process_t*)kmalloc_aligned(sizeof(process_t), PROCESS_T_ALIGN);
    if (unlikely(!idle)) return NULL;
    
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    memset(idle, 0, sizeof(process_t));
    idle->pid = 0; 
    idle->parent_id = 0;
    idle->state = PROCESS_RUNNING;
    idle->cpu_id = cpu_id;
    idle->priority = PRIO_IDLE;
    idle->affinity = cpu_id; 
    idle->thread_fn = idle_task_fn;
    idle->flags = PROC_FLAG_KERNEL | PROC_FLAG_CRITICAL;
    
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    memset(idle->fxsave_region, 0, FPU_BUFFER_SIZE);
    *((uint16_t*)&idle->fxsave_region[0]) = 0x037F; 
    *((uint32_t*)&idle->fxsave_region[24]) = FPU_DEFAULT_MXCSR;
    
    uint64_t current_rsp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(current_rsp));
    idle->rsp = current_rsp;
    
    if (task_list_lock) rwlock_acquire_write(task_list_lock);
    if (process_list_tail) {
        idle->next = process_list_head;
        process_list_tail->next = idle;
        process_list_tail = idle;
    } else {
        process_list_head = idle;
        process_list_tail = idle;
        idle->next = idle;
    }
    if (task_list_lock) rwlock_release_write(task_list_lock);
    return idle;
}

static int create_task_internal(void (*entry_point)(), kernel_prio_t prio, int16_t affinity) {
    process_t* new_proc = (process_t*)kmalloc_aligned(sizeof(process_t), PROCESS_T_ALIGN);
    if (unlikely(!new_proc)) { 
        oom_reaper_scan(); 
        new_proc = (process_t*)kmalloc_aligned(sizeof(process_t), PROCESS_T_ALIGN);
        if (!new_proc) return 0; 
    }

    uint8_t current_cpu = (uint8_t)get_cpu_id_fast();
    process_t* parent = current_process[current_cpu];

    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    memset(new_proc, 0, sizeof(process_t));
    new_proc->pid = next_pid++;
    if (next_pid == 0) next_pid = 1; 
    
    if (parent && parent->pid != 0) new_proc->parent_id = parent->pid;
    else new_proc->parent_id = 0;
    
    new_proc->flags = PROC_FLAG_KERNEL;
    if (prio >= PRIO_HIGH) new_proc->flags |= PROC_FLAG_CRITICAL;
    
    new_proc->state = PROCESS_READY;
    new_proc->thread_fn = entry_point;
    new_proc->priority = prio; 
    new_proc->affinity = affinity;
    new_proc->wake_tick = 0; 
    new_proc->start_tick = timer_get_ticks();
    new_proc->used_pages = KERNEL_STACK_PAGES; 
    new_proc->is_queued = false;
    
    static volatile uint32_t rr_cpu = 0;
    uint8_t target_cpu;
    if (affinity != -1) {
        target_cpu = (uint8_t)affinity;
    } else {
        if (num_cpus == 0) target_cpu = 0;
        else target_cpu = (uint8_t)(__atomic_fetch_add(&rr_cpu, 1, __ATOMIC_RELAXED) % num_cpus);
    }
    new_proc->cpu_id = target_cpu;

    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    memset(new_proc->fxsave_region, 0, FPU_BUFFER_SIZE);
    *((uint16_t*)&new_proc->fxsave_region[0]) = 0x037F; 
    *((uint32_t*)&new_proc->fxsave_region[24]) = FPU_DEFAULT_MXCSR;

    void* stack_addr = alloc_kernel_stack();
    if (unlikely(!stack_addr)) {
        reaper_invoke();
        stack_addr = alloc_kernel_stack();
        if (!stack_addr) { kfree_aligned(new_proc); return 0; }
        new_proc->stack_base = stack_addr;
    } else {
        new_proc->stack_base = stack_addr;
    }
    
    uint64_t* stack_top = (uint64_t*)((uint64_t)stack_addr + KERNEL_STACK_SIZE);
    setup_stack_frame(stack_top, entry_point, NULL, &new_proc->rsp);

    if (task_list_lock) rwlock_acquire_write(task_list_lock);
    if (!process_list_head) {
        process_list_head = new_proc;
        process_list_tail = new_proc;
        new_proc->next = new_proc;
    } else {
        new_proc->next = process_list_head;
        process_list_tail->next = new_proc;
        process_list_tail = new_proc;
    }
    if (task_list_lock) rwlock_release_write(task_list_lock);
    
    sched_enqueue(target_cpu, new_proc, prio, affinity);
    return 1;
}

int create_kernel_task_prio(void (*entry_point)(), kernel_prio_t prio) { return create_task_internal(entry_point, prio, -1); }
int create_kernel_task_pinned(void (*entry_point)(), kernel_prio_t prio, int cpu_id) { return create_task_internal(entry_point, prio, (int16_t)cpu_id); }
void create_kernel_task(void (*entry_point)()) { create_kernel_task_prio(entry_point, PRIO_NORMAL); }

__attribute__((no_stack_protector))
void process_exit() {
    uint8_t cpu_id = (uint8_t)get_cpu_id_fast();
    __asm__ volatile("cli");
    process_t* curr = current_process[cpu_id];
    
    if (unlikely(!scheduler_active || !curr || curr->pid == 0)) {
        panic_at("process.c", __LINE__, KERR_UNKNOWN, "CRITICAL: Attempted to kill PID 0 or exit before scheduler start! (Silent Hang Prevented)");
    }
    
    curr->state = PROCESS_ZOMBIE;
    sched_remove(cpu_id, curr);
    
    if (per_cpu_data[cpu_id]) {
        per_cpu_data[cpu_id]->pending_task = NULL;
    }
    
    reaper_invoke();
    schedule();
    while(1) hal_cpu_halt();
}

void reaper_invoke() { signal_reaper(); }

void process_get_info(int* total, int* running, int* zombie) {
    *total = 0; *running = 0; *zombie = 0;
    if (task_list_lock) rwlock_acquire_write(task_list_lock); 
    process_t* curr = process_list_head;
    if (curr) {
        do {
            (*total)++;
            if (curr->state == PROCESS_RUNNING) (*running)++;
            if (curr->state == PROCESS_ZOMBIE || curr->state == PROCESS_DEAD) (*zombie)++;
            curr = curr->next;
        } while (curr != process_list_head);
    }
    if (task_list_lock) rwlock_release_write(task_list_lock);
}

int process_get_zombie_count() {
    int zombie = 0;
    if (task_list_lock) rwlock_acquire_write(task_list_lock); 
    process_t* curr = process_list_head;
    if (curr) {
        do {
            if (curr->state == PROCESS_ZOMBIE || curr->state == PROCESS_DEAD) zombie++;
            curr = curr->next;
        } while (curr != process_list_head);
    }
    if (task_list_lock) rwlock_release_write(task_list_lock);
    return zombie;
}