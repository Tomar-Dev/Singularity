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
    } else {
        while (true) {
            uint64_t flags = spinlock_acquire(&lock->lock);

            if (!lock->writer_active &&
                lock->write_waiters == 0 &&
                lock->readers < RWLOCK_MAX_READERS)
            {
                lock->readers++;
                spinlock_release(&lock->lock, flags);
                return;
            } else {
                process_t* proc = (process_t*)get_current_thread_fast();
                if (proc) {
                    proc->state = PROCESS_BLOCKED;
                    wait_queue_push(&lock->read_q, proc);
                    spinlock_release(&lock->lock, flags);
                    schedule();
                } else {
                    spinlock_release(&lock->lock, flags);
                    hal_cpu_relax();
                }
            }
        }
    }
}

void rwlock_release_read(rwlock_t lock) {
    if (!lock) {
        panic_at("RWLOCK", 0, KERR_UNKNOWN, "rwlock_release_read: called with NULL lock");
        return;
    } else {
        uint64_t flags = spinlock_acquire(&lock->lock);

        if (lock->readers > 0) {
            lock->readers--;
        } else {
            spinlock_release(&lock->lock, flags);
            panic_at("RWLOCK", 0, KERR_UNKNOWN, "rwlock_release_read: underflow -- more releases than acquires");
            return;
        }

        if (lock->readers == 0 && lock->write_waiters > 0) {
            process_t* next_writer = wait_queue_pop_safe(&lock->write_q, &lock->write_waiters);
            if (next_writer) {
                spinlock_release(&lock->lock, flags);
                sched_wake_task(next_writer);
                return;
            }
        }
        
        if (lock->write_waiters == 0) {
            spinlock_release(&lock->lock, flags);
            while (true) {
                process_t* r = wait_queue_pop_safe(&lock->read_q, nullptr);
                if (!r) break;
                sched_wake_task(r);
            }
            return;
        }

        spinlock_release(&lock->lock, flags);
    }
}

void rwlock_acquire_write(rwlock_t lock) {
    if (!lock) {
        panic_at("RWLOCK", 0, KERR_UNKNOWN, "rwlock_acquire_write: called with NULL lock");
        return;
    } else {
        while (true) {
            uint64_t flags = spinlock_acquire(&lock->lock);

            if (!lock->writer_active && lock->readers == 0) {
                lock->writer_active = true;
                spinlock_release(&lock->lock, flags);
                return;
            } else {
                lock->write_waiters++;
                process_t* proc = (process_t*)get_current_thread_fast();
                if (proc) {
                    proc->state = PROCESS_BLOCKED;
                    wait_queue_push(&lock->write_q, proc);
                    spinlock_release(&lock->lock, flags);
                    schedule();
                } else {
                    lock->write_waiters--;
                    spinlock_release(&lock->lock, flags);
                    hal_cpu_relax();
                }
            }
        }
    }
}

void rwlock_release_write(rwlock_t lock) {
    if (!lock) {
        panic_at("RWLOCK", 0, KERR_UNKNOWN, "rwlock_release_write: called with NULL lock");
        return;
    } else {
        uint64_t flags = spinlock_acquire(&lock->lock);
        lock->writer_active = false;

        if (lock->write_waiters > 0) {
            process_t* next_writer = wait_queue_pop_safe(&lock->write_q, &lock->write_waiters);
            if (next_writer) {
                spinlock_release(&lock->lock, flags);
                sched_wake_task(next_writer);
                return;
            }
        }
        
        spinlock_release(&lock->lock, flags);
        while (true) {
            process_t* r = wait_queue_pop_safe(&lock->read_q, nullptr);
            if (!r) break;
            sched_wake_task(r);
        }
    }
}
