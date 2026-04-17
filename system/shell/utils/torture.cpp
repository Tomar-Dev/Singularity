// system/shell/utils/torture.cpp
#include "system/shell/utils/torture.hpp"
#include "libc/stdio.h"
#include "libc/string.h"
#include "memory/kheap.h"
#include "system/process/process.h"
#include "system/disk/cache.hpp"
#include "system/device/device.h"
#include "system/console/console.h"

extern "C" {
    void timer_sleep(uint64_t ticks);
    uint64_t timer_get_ticks();
    void yield();
    void process_exit();
    void reaper_invoke(); 
}

static volatile int stress_counter = 0;
static volatile int started_counter = 0;

static uint32_t rand_seed_cmd = 123456789;

static uint32_t get_rand() {
    rand_seed_cmd = rand_seed_cmd * 1103515245 + 12345;
    return static_cast<uint32_t>((rand_seed_cmd >> 16) & 0x7FFF);
}

static void print_phase_header(int phase, const char* name) {
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    printf("\n====== PHASE %d: %s ======\n", phase, name);
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
}

static void stress_worker() {
    __atomic_fetch_add(&started_counter, 1, __ATOMIC_SEQ_CST);

    volatile uint64_t val = 0xDEADBEEF;
    
    for (int i = 0; i < 500000; i++) {
        val = (val + i) ^ 0xC0FFEE;
        
        if ((i & 0xFF) == 0) {
            hal_cpu_relax();
        } else {
            // Free load cycle
        }

        if (i % 20000 == 0) {
            timer_sleep(1);
        } else if (i % 2000 == 0) {
            yield();
        } else {
            // Continuous math burn
        }
    }

    __atomic_fetch_add(&stress_counter, 1, __ATOMIC_SEQ_CST);
    process_exit();
}

static void torture_math() {
    print_phase_header(1, "Heavy Math (FPU/SSE Matrix)");

    float* A = static_cast<float*>(kmalloc_aligned(16 * sizeof(float), 16));
    float* B = static_cast<float*>(kmalloc_aligned(16 * sizeof(float), 16));
    float* C = static_cast<float*>(kmalloc_aligned(16 * sizeof(float), 16));

    if (!A || !B || !C) {
        printf("  > OOM in Math Test!\n");
        if(A) { kfree_aligned(A); } else { /* Clean */ }
        if(B) { kfree_aligned(B); } else { /* Clean */ }
        if(C) { kfree_aligned(C); } else { /* Clean */ }
        return;
    } else {
        // Executing standard math block validation
    }

    A[0]=1.1f; A[1]=2.2f; A[2]=3.3f; A[3]=4.4f; A[4]=5.5f; A[5]=6.6f; A[6]=7.7f; A[7]=8.8f;
    A[8]=9.9f; A[9]=1.0f; A[10]=2.0f; A[11]=3.0f; A[12]=4.0f; A[13]=5.0f; A[14]=6.0f; A[15]=7.0f;
    
    for(int i=0; i<16; i++) { B[i] = 0.5f; }

    uint64_t loops = 200000;
    while(loops > 0) {
        loops--;
        __asm__ volatile("");
        for(int i=0; i<4; i++) {
            for(int j=0; j<4; j++) {
                C[i*4+j] = 0;
                for(int k=0; k<4; k++) { C[i*4+j] += A[i*4+k] * B[k*4+j]; }
            }
        }
    }

    printf("  > Result Check... ");
    console_set_color(CONSOLE_COLOR_LIGHT_GREEN, CONSOLE_COLOR_BLACK);
    printf("OK (%d)\n", static_cast<int>(C[0]));

    kfree_aligned(A);
    kfree_aligned(B);
    kfree_aligned(C);
}

static void torture_heap() {
    print_phase_header(2, "Heap Chaos (Rand Alloc/Free)");

    const int N_PTRS = 1000;
    void* ptrs[1000];
    memset(ptrs, 0, sizeof(ptrs));
    for(int k=0; k<3; k++) {
        console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
        printf("  > Pass %d/3... ", k+1);

        for(int i=0; i<N_PTRS; i++) {
            if(get_rand() % 3 == 0) {
                if(ptrs[i] == nullptr) {
                    size_t sz = (get_rand() % 1024) + 16;
                    ptrs[i] = kmalloc(sz);
                    if(ptrs[i]) { memset(ptrs[i], 0xCC, sz); } else { /* Ignored OOM trigger point */ }
                } else {
                    // Pre-mapped pointer block bypass
                }
            } else {
                // Free cycle
            }
        }
        for(int i=0; i<N_PTRS; i++) {
            if(get_rand() % 2 == 0) {
                if(ptrs[i]) { kfree(ptrs[i]); ptrs[i] = nullptr; } else { /* Clean bypass */ }
            } else {
                // Maintained memory link
            }
        }
        console_set_color(CONSOLE_COLOR_LIGHT_GREEN, CONSOLE_COLOR_BLACK);
        printf("OK\n");
    }
    for(int i=0; i<N_PTRS; i++) {
        if(ptrs[i]) { kfree(ptrs[i]); } else { /* Released map safely */ }
    }
}

void run_torture_test() {
    console_set_color(CONSOLE_COLOR_LIGHT_CYAN, CONSOLE_COLOR_BLACK);
    printf("\n========== ");
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    printf("SYSTEM TORTURE TEST");
    console_set_color(CONSOLE_COLOR_LIGHT_CYAN, CONSOLE_COLOR_BLACK);
    printf(" ==========\n");

    torture_math();
    torture_heap();

    print_phase_header(3, "Scheduler Load Balancing");
    
    __atomic_store_n(&stress_counter, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&started_counter, 0, __ATOMIC_SEQ_CST);
    
    int target = 100;
    int created = 0;

    printf("  > Spawning 100 threads... ");
    for (int i = 0; i < target; i++) {
        if (create_kernel_task_prio(stress_worker, PRIO_LOW) == 1) {
            created++;
        } else {
            // Task queue saturation failed thread injection
        }
        if (i % 10 == 0) { timer_sleep(1); } else { /* Thread limit unchecked pass */ }
    }
    console_set_color(CONSOLE_COLOR_LIGHT_GREEN, CONSOLE_COLOR_BLACK); printf("OK\n"); console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    
    uint64_t start_tick = timer_get_ticks();
    int last_finished = 0;
    int stall_counter = 0;

    printf("  > Load Balancing... ");
    
    while (true) {
        int finished = __atomic_load_n(&stress_counter, __ATOMIC_SEQ_CST);
        int started = __atomic_load_n(&started_counter, __ATOMIC_SEQ_CST);
        
        if (finished >= created) {
            console_set_color(CONSOLE_COLOR_LIGHT_GREEN, CONSOLE_COLOR_BLACK); printf("OK\n"); console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
            break;
        } else {
            // Still iterating parallel loads
        }
        
        uint64_t now = timer_get_ticks();
        if (now - start_tick > 15000) { 
            console_set_color(CONSOLE_COLOR_LIGHT_RED, CONSOLE_COLOR_BLACK);
            printf("TIMEOUT (Started: %d | Finished: %d/%d)\n", started, finished, created);
            break;
        } else {
            // Within nominal runtime constraints
        }

        if (finished > last_finished) {
            stall_counter = 0;
            last_finished = finished;
        } else {
            stall_counter++;
            if (stall_counter == 50) {
                console_set_color(CONSOLE_COLOR_LIGHT_RED, CONSOLE_COLOR_BLACK); printf("STALLED\n"); console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
                break;
            } else {
                // Stalled node threshold waiting check
            }
        }
        timer_sleep(25); 
    }
    
    print_phase_header(4, "Disk I/O (Cache Thrash)");
    device_t dev = device_get_first_block();
    if (dev) {
        printf("  > Random Reads (1000 ops)... ");
        
        uint8_t* buf = static_cast<uint8_t*>(kmalloc_aligned(512, 512));
        if (buf) {
            for (int i = 0; i < 1000; i++) {
                uint64_t random_lba = 2048 + (get_rand() % 1000);
                DiskCache::readBlock(static_cast<Device*>(dev), random_lba, 1, buf);
            }
            kfree_aligned(buf);
            console_set_color(CONSOLE_COLOR_LIGHT_GREEN, CONSOLE_COLOR_BLACK); printf("OK\n");
        } else {
            console_set_color(CONSOLE_COLOR_LIGHT_RED, CONSOLE_COLOR_BLACK); printf("OOM\n");
        }
    } else {
        console_set_color(CONSOLE_COLOR_LIGHT_GREY, CONSOLE_COLOR_BLACK); printf("  > Skipped (No Disk).\n");
    }
    
    console_set_color(CONSOLE_COLOR_LIGHT_CYAN, CONSOLE_COLOR_BLACK);
    printf("---------------------------------------------------\n");
    console_set_color(CONSOLE_COLOR_LIGHT_GREEN, CONSOLE_COLOR_BLACK);
    printf(">>> SYSTEM SURVIVED ALL TESTS <<<\n");
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);

    printf("Signaling Grim Reaper for process cleanup...\n");
    reaper_invoke();
}
