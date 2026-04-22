// system/process/process.h
#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <stdbool.h>
#include "system/process/priority.h"
#include "archs/cpu/x86_64/interrupts/isr.h"
#include "archs/cpu/x86_64/sync/spinlock.h"
#include "system/sync/rwlock.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_CPUS 32

#define FPU_BUFFER_SIZE 1024

#define KERNEL_STACK_SIZE 16384

#define PROC_FLAG_KERNEL    (1 << 0) 
#define PROC_FLAG_USER      (1 << 1) 
#define PROC_FLAG_CRITICAL  (1 << 2) 
#define PROC_FLAG_SILENT    (1 << 3) 
#define PROC_FLAG_TAINTED   (1 << 4) 

#define OOM_PENALTY_IDLE    1000
#define OOM_PENALTY_LOW     500
#define OOM_PENALTY_YOUNG   200
#define OOM_YOUNG_TICKS     500

typedef enum {
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_ZOMBIE, 
    PROCESS_DEAD,
    PROCESS_BLOCKED,
    PROCESS_SLEEPING
} process_state_t;

typedef struct process {
    uint8_t fxsave_region[FPU_BUFFER_SIZE] __attribute__((aligned(64)));

    uint64_t rsp;          
    uint64_t pid;
    uint64_t parent_id;
    
    process_state_t state; 
    uint8_t cpu_id;
    
    kernel_prio_t priority;
    kernel_prio_t base_priority; 
    int16_t affinity;
    
    uint32_t flags; 
    
    uint64_t wake_tick;
    uint64_t start_tick;
    uint64_t used_pages;
    
    uint64_t vruntime;
    uint64_t last_queued;
    bool is_queued;
    
    uint64_t stack_canary; 
    
    void* stack_base;
    void (*thread_fn)(void);
    
    struct process* next;       
    struct process* wait_next;  
    struct process* rq_next;
    struct process* rq_prev;
    struct process* sleep_next;
} __attribute__((aligned(64))) process_t;

typedef struct wait_queue {
    spinlock_t lock;
    process_t* head;
    process_t* tail;
} wait_queue_t;

extern process_t* idle_tasks[MAX_CPUS];
extern bool scheduler_active;
extern process_t* process_list_head;
extern process_t* process_list_tail;
extern rwlock_t task_list_lock; 
extern process_t* current_process[MAX_CPUS];
extern volatile uint64_t system_ticks;
extern volatile uint64_t cpu_tick_counts[MAX_CPUS];

void init_process_manager();
void create_kernel_task(void (*entry_point)());
int create_kernel_task_prio(void (*entry_point)(), kernel_prio_t prio);
int create_kernel_task_pinned(void (*entry_point)(), kernel_prio_t prio, int cpu_id);
process_t* create_idle_task(uint8_t cpu_id); 
void process_exit();
void reaper_invoke(); 
void process_get_info(int* total, int* running, int* zombie);
void kthread_starter();
int process_get_zombie_count();
void oom_reaper_scan(); 

void init_multitasking();
void init_multitasking_ap();
void enable_scheduler();
void schedule();
void yield();
void yield_cpu();
void process_tick(); 
void timer_handler(registers_t* regs); 
void task_sleep(uint64_t ticks);

void wait_queue_init(wait_queue_t* q);
void wait_queue_push(wait_queue_t* q, process_t* task);
process_t* wait_queue_pop_safe(wait_queue_t* q, uint32_t* skipped_count);

void sched_enqueue(uint8_t cpu_id, process_t* task, kernel_prio_t prio, int16_t affinity);
void sched_remove(uint8_t cpu_id, process_t* task);
void sched_wake_task(process_t* task);
void sched_sleep_current(uint64_t ticks);
process_t* sched_pick_next(uint8_t cpu_id);
uint64_t sched_get_load(uint8_t cpu_id);
void sched_debug_dump(void);

void update_task_priority(process_t* proc, bool used_full_slice);
void check_pending_task();

void switch_to_task(uint64_t* current_rsp, uint64_t next_rsp, void* current_fpu, void* next_fpu, void* next_task);
void setup_stack_frame(uint64_t* stack_top, void (*entry)(), void (*exit_handler)(), uint64_t* out_rsp);

#ifdef __cplusplus
}
#endif

#endif