// system/sync/mutex.cpp
#include "system/sync/mutex.h"
#include "system/process/process.h"
#include "kernel/fastops.h"
#include "memory/kheap.h"
#include "kernel/debug.h"

struct native_mutex {
    int locked; 
    spinlock_t lock;
    wait_queue_t wait_q;
};

struct native_semaphore {
    int count; 
    spinlock_t lock;
    wait_queue_t wait_q;
};

mutex_t mutex_create() {
    native_mutex* m = (native_mutex*)kmalloc(sizeof(native_mutex));
    if (m) {
        m->locked = 0;
        spinlock_init(&m->lock);
        wait_queue_init(&m->wait_q); 
    } else {
        panic_at("MUTEX", 0, KERR_MEM_OOM, "mutex_create: kmalloc failed -- Kernel Heap exhausted");
    }
    return m;
}

void mutex_destroy(mutex_t m) {
    if (m) {
        kfree(m);
    } else {
        // Null pointer passed, safely ignored
    }
}

void mutex_lock(mutex_t m) {
    if (!m) {
        panic_at("MUTEX", 0, KERR_NULL_DEREFERENCE, "mutex_lock: called with NULL mutex");
        return;
    } else {
        // Valid mutex
    }
    
    while (1) {
        uint64_t rflags;
        __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags) : : "memory");
        
        uint64_t flags = spinlock_acquire(&m->lock);

        if (m->locked == 0) {
            m->locked = 1;
            spinlock_release(&m->lock, flags);
            if (rflags & 0x200) {
                __asm__ volatile("sti" ::: "memory");
            } else {
                // Keep interrupts disabled
            }
            return; 
        } else {
            process_t* proc = (process_t*)get_current_thread_fast();
            if (proc) {
                proc->state = PROCESS_BLOCKED;
                wait_queue_push(&m->wait_q, proc);
                spinlock_release(&m->lock, flags);
                schedule(); 
                if (rflags & 0x200) {
                    __asm__ volatile("sti" ::: "memory");
                } else {
                    // Keep interrupts disabled
                }
            } else {
                spinlock_release(&m->lock, flags);
                if (rflags & 0x200) {
                    __asm__ volatile("sti" ::: "memory");
                } else {
                    // Keep interrupts disabled
                }
                hal_cpu_relax();
            }
        }
    }
}

void mutex_unlock(mutex_t m) {
    if (!m) {
        return;
    } else {
        // Valid mutex
    }
    
    uint64_t flags = spinlock_acquire(&m->lock);
    m->locked = 0;
    process_t* next = wait_queue_pop_safe(&m->wait_q, nullptr);
    spinlock_release(&m->lock, flags);

    if (next) {
        sched_wake_task(next);
    } else {
        // No waiting tasks
    }
}

semaphore_t sem_create(int initial) {
    native_semaphore* s = (native_semaphore*)kmalloc(sizeof(native_semaphore));
    if (s) {
        s->count = initial;
        spinlock_init(&s->lock);
        wait_queue_init(&s->wait_q);
    } else {
        panic_at("SEMAPHORE", 0, KERR_MEM_OOM, "sem_create: kmalloc failed -- Kernel Heap exhausted");
    }
    return s;
}

void sem_destroy(semaphore_t s) {
    if (s) {
        kfree(s);
    } else {
        // Null pointer passed, safely ignored
    }
}

void sem_wait(semaphore_t s) {
    if (!s) {
        panic_at("SEMAPHORE", 0, KERR_NULL_DEREFERENCE, "sem_wait: called with NULL semaphore");
        return;
    } else {
        // Valid semaphore
    }
    
    while (1) {
        uint64_t rflags;
        __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags) : : "memory");
        
        uint64_t flags = spinlock_acquire(&s->lock);

        if (s->count > 0) {
            s->count--; 
            spinlock_release(&s->lock, flags);
            if (rflags & 0x200) {
                __asm__ volatile("sti" ::: "memory");
            } else {
                // Keep interrupts disabled
            }
            return;
        } else {
            process_t* proc = (process_t*)get_current_thread_fast();
            if (proc) {
                proc->state = PROCESS_BLOCKED;
                wait_queue_push(&s->wait_q, proc);
                spinlock_release(&s->lock, flags);
                schedule();
                if (rflags & 0x200) {
                    __asm__ volatile("sti" ::: "memory");
                } else {
                    // Keep interrupts disabled
                }
            } else {
                spinlock_release(&s->lock, flags);
                if (rflags & 0x200) {
                    __asm__ volatile("sti" ::: "memory");
                } else {
                    // Keep interrupts disabled
                }
                hal_cpu_relax();
            }
        }
    }
}

void sem_signal(semaphore_t s) {
    if (!s) {
        return;
    } else {
        // Valid semaphore
    }
    
    uint64_t flags = spinlock_acquire(&s->lock);
    s->count++; 
    process_t* next = wait_queue_pop_safe(&s->wait_q, nullptr);
    spinlock_release(&s->lock, flags);

    if (next) {
        sched_wake_task(next);
    } else {
        // No waiting tasks
    }
}