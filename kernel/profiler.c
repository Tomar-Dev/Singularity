// kernel/profiler.c
#include "kernel/profiler.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "memory/kheap.h"
#include "archs/cpu/x86_64/sync/spinlock.h"
#include "archs/cpu/cpu_hal.h"
extern void print_status(const char* prefix, const char* msg, const char* status);

#define PROFILER_MEM_START 0xFFFF800000000000ULL     
#define PROFILER_MEM_END   0xFFFF800002000000ULL
#define PROFILER_BUCKET_SIZE 4096         
#define PROFILER_BUCKET_COUNT ((PROFILER_MEM_END - PROFILER_MEM_START) / PROFILER_BUCKET_SIZE)

static uint32_t* samples = NULL;
static bool profiler_running = false;
static uint64_t total_samples = 0;

static spinlock_t profiler_lock = {0, 0, {0}};

void profiler_init() {
    samples = (uint32_t*)kmalloc(PROFILER_BUCKET_COUNT * sizeof(uint32_t));
    if (samples) {
        // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
        memset(samples, 0, PROFILER_BUCKET_COUNT * sizeof(uint32_t));
        
        char msg[128];
        // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
        sprintf(msg, "Kernel Profiler Initialized (%d buckets covering 32MB)", (int)PROFILER_BUCKET_COUNT);
        print_status("[ PROF ]", msg, "INFO");
        
        profiler_running = true; 
    } else {
        printf("[PROF] Failed to allocate profiler buffer!\n");
    }
    spinlock_init(&profiler_lock);
}

void profiler_enable(bool enable) {
    profiler_running = enable;
    if (enable) {
        printf("[PROF] Profiling started/resumed.\n");
    } else {
        printf("[PROF] Profiling paused.\n");
    }
}

void profiler_tick(uint64_t rip) {
    if (!profiler_running || !samples) return;

    if (rip < PROFILER_MEM_START || rip >= PROFILER_MEM_END) {
        return;
    }

    uint64_t offset = rip - PROFILER_MEM_START;
    uint32_t idx = offset / PROFILER_BUCKET_SIZE;

    if (idx < PROFILER_BUCKET_COUNT) {
        __atomic_fetch_add(&samples[idx], 1, __ATOMIC_RELAXED);
        __atomic_fetch_add(&total_samples, 1, __ATOMIC_RELAXED);
    }
}

void profiler_print_report() {
    if (!samples) return;
    
    printf("\n--- Kernel Profiler Report ---\n");
    printf("Total Samples: %lu\n", total_samples);
    
    if (total_samples == 0) {
        printf("No data collected yet. (Is system idle?)\n");
        return;
    }

    printf("%-18s | %-10s | %s\n", "Address Range", "Samples", "Load %");
    printf("-------------------+------------+-------\n");

    for (uint32_t i = 0; i < PROFILER_BUCKET_COUNT; i++) {
        if (samples[i] > 0) {
            uint64_t start = PROFILER_MEM_START + (i * PROFILER_BUCKET_SIZE);
            uint64_t end = start + PROFILER_BUCKET_SIZE;
            
            uint64_t percent = (samples[i] * 100) / total_samples;
            
            if (percent >= 1 || samples[i] > 50) { 
                printf("0x%08lx-0x%05lx | %-10d | %d%%\n", 
                       start, end & 0xFFFFF, samples[i], (int)percent);
            }
        }
    }
    printf("------------------------------\n");
}
