// system/sync/rwlock.cpp
#include "system/sync/rwlock.h"
#include "system/process/process.h"
#include "kernel/fastops.h"
#include "memory/kheap.h"
#include "libc/string.h"
#include "kernel/debug.h"

struct native_rwlock {
    int          readers;
    bool         writer_active;
    int          write_waiters;
    spinlock_t   lock;
    wait_queue_t read_q;
    wait_queue_t write_q;
};

rwlock_t rwlock_create(void) {
    native_rwlock* l = (native_rwlock*)kmalloc(sizeof(native_rwlock));
    if (l) {
        l->readers       = 0;
        l->writer_active = false;
        l->write_waiters = 0;
        spinlock_init(&l->lock);
        wait_queue_init(&l->read_q);
        wait_queue_init(&l->write_q);
    } else {
        panic_at("RWLOCK", 0, KERR_MEM_OOM,
                 "rwlock_create: kmalloc failed -- Kernel Heap exhausted");
    }
    return l;
}

void rwlock_destroy(rwlock_t lock) {
    if (lock) {
        kfree(lock);
    } else {
        klog(LOG_WARN, "[RWLOCK] rwlock_destroy: called with NULL lock (ignored)\n");
    }
}

void rwlock_acquire_read(rwlock_t lock) {
    if (!lock) {
        panic_at("RWLOCK", 0, KERR_UNKNOWN, "rwlock_acquire_read: called with NULL lock");
        return;
    } else { /* Valid pointer */ }
    
    while (true) {
        uint64_t rflags;
        __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags) : : "memory");
        
        uint64_t flags = spinlock_acquire(&lock->lock);

        if (!lock->writer_active &&
            lock->write_waiters == 0 &&
            lock->readers < RWLOCK_MAX_READERS)
        {
            lock->readers++;
            spinlock_release(&lock->lock, flags);
            if (rflags & 0x200) { __asm__ volatile("sti" ::: "memory"); } else { /* CLI */ }
            return;
        } else {
            process_t* proc = (process_t*)get_current_thread_fast();
            if (proc) {
                proc->state = PROCESS_BLOCKED;
                wait_queue_push(&lock->read_q, proc);
                spinlock_release(&lock->lock, flags);
                schedule();
                if (rflags & 0x200) { __asm__ volatile("sti" ::: "memory"); } else { /* CLI */ }
            } else {
                spinlock_release(&lock->lock, flags);
                if (rflags & 0x200) { __asm__ volatile("sti" ::: "memory"); } else { /* CLI */ }
                hal_cpu_relax();
            }
        }
    }
}

// Bulgu 1.1 & 3.2 FIX: Atomic write_waiters adjustment and consolidated release
void rwlock_release_read(rwlock_t lock) {
    if (!lock) {
        panic_at("RWLOCK", 0, KERR_UNKNOWN, "rwlock_release_read: called with NULL lock");
        return;
    } else { /* Valid */ }
    
    uint64_t flags = spinlock_acquire(&lock->lock);

    if (lock->readers > 0) {
        lock->readers--;
    } else {
        spinlock_release(&lock->lock, flags);
        panic_at("RWLOCK", 0, KERR_UNKNOWN, "rwlock_release_read: reader count underflow");
        return;
    }

    if (lock->readers == 0 && lock->write_waiters > 0) {
        uint32_t skipped = 0;
        process_t* next_writer = wait_queue_pop_safe(&lock->write_q, &skipped);
        
        // Accurate counter synchronization
        int total_removed = (next_writer ? 1 : 0) + (int)skipped;
        if (lock->write_waiters >= total_removed) {
            lock->write_waiters -= total_removed;
        } else {
            lock->write_waiters = 0;
        }

        spinlock_release(&lock->lock, flags);
        if (next_writer) {
            sched_wake_task(next_writer);
        } else { /* All waiters were zombies */ }
    } else {
        spinlock_release(&lock->lock, flags);
    }
}

void rwlock_acquire_write(rwlock_t lock) {
    if (!lock) {
        panic_at("RWLOCK", 0, KERR_UNKNOWN, "rwlock_acquire_write: called with NULL lock");
        return;
    } else { /* Valid */ }
    
    while (true) {
        uint64_t rflags;
        __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags) : : "memory");
        
        uint64_t flags = spinlock_acquire(&lock->lock);

        if (!lock->writer_active && lock->readers == 0) {
            lock->writer_active = true;
            spinlock_release(&lock->lock, flags);
            if (rflags & 0x200) { __asm__ volatile("sti" ::: "memory"); } else { /* CLI */ }
            return;
        } else {
            lock->write_waiters++;
            process_t* proc = (process_t*)get_current_thread_fast();
            if (proc) {
                proc->state = PROCESS_BLOCKED;
                wait_queue_push(&lock->write_q, proc);
                spinlock_release(&lock->lock, flags);
                schedule();
                if (rflags & 0x200) { __asm__ volatile("sti" ::: "memory"); } else { /* CLI */ }
            } else {
                lock->write_waiters--;
                spinlock_release(&lock->lock, flags);
                if (rflags & 0x200) { __asm__ volatile("sti" ::: "memory"); } else { /* CLI */ }
                hal_cpu_relax();
            }
        }
    }
}

void rwlock_release_write(rwlock_t lock) {
    if (!lock) {
        panic_at("RWLOCK", 0, KERR_UNKNOWN, "rwlock_release_write: called with NULL lock");
        return;
    } else { /* Valid */ }
    
    uint64_t flags = spinlock_acquire(&lock->lock);
    lock->writer_active = false;

    if (lock->write_waiters > 0) {
        uint32_t skipped = 0;
        process_t* next_writer = wait_queue_pop_safe(&lock->write_q, &skipped);
        
        int total_removed = (next_writer ? 1 : 0) + (int)skipped;
        if (lock->write_waiters >= total_removed) {
            lock->write_waiters -= total_removed;
        } else {
            lock->write_waiters = 0;
        }

        spinlock_release(&lock->lock, flags);
        if (next_writer) {
            sched_wake_task(next_writer);
        } else { /* No valid writer found */ }
    } else {
        // No writers waiting, wake all readers
        spinlock_release(&lock->lock, flags);
        while (true) {
            process_t* r = wait_queue_pop_safe(&lock->read_q, nullptr);
            if (!r) break;
            else { sched_wake_task(r); }
        }
    }
}