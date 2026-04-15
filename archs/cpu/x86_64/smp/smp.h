// archs/cpu/x86_64/smp/smp.h
#ifndef SMP_H
#define SMP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_CPUS 32

struct PerCPU;

typedef struct PerCPU {
    struct PerCPU* self;
    uint64_t kernel_stack;
    uint64_t user_rsp_scratch;
    uint32_t lapic_id;
    uint32_t cpu_id;
    void* current_process;
    
    uintptr_t stack_canary;
    
    void* switching_task;
    void* pending_task;
    
    uint64_t idle_ticks;
    
    volatile int32_t preempt_count;
    
    uint8_t _pad[64 - (0x4C % 64)]; 
} __attribute__((packed, aligned(64))) per_cpu_t;

typedef struct {
    uint8_t lapic_id;
    uint8_t acpi_id;
    uint32_t status; 
    uint64_t stack_top; 
    void* tss;          
} smp_cpu_t;

extern smp_cpu_t cpus[MAX_CPUS];
extern uint8_t num_cpus;
extern per_cpu_t* per_cpu_data[MAX_CPUS];

void init_smp();
void ap_startup();
uint8_t get_current_cpu_id();
void smp_load_gs(uint8_t cpu_id);

static inline per_cpu_t* this_cpu_ptr() {
    per_cpu_t* cpu;
    __asm__ volatile("mov %%gs:0, %0" : "=r"(cpu));
    return cpu;
}

static inline per_cpu_t* per_cpu_ptr(int cpu_id) {
    if (cpu_id >= MAX_CPUS) return 0;
    return per_cpu_data[cpu_id];
}

static inline uint32_t this_cpu_id() {
    uint32_t id;
    __asm__ volatile("mov %%gs:0x1C, %0" : "=r"(id));
    return id;
}

#define get_cpu_var(member) (this_cpu_ptr()->member)

#ifdef __cplusplus
}
#endif

#endif
