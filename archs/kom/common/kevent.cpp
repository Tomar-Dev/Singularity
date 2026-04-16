// archs/kom/common/kevent.cpp
#include "archs/kom/common/kevent.hpp"
#include "kernel/fastops.h"
#include "archs/cpu/cpu_hal.h"

KEvent::KEvent(bool autoReset) : KObject(KObjectType::EVENT), signaled(false), auto_reset(autoReset) {
    wait_queue_init(&wait_q);
}

KEvent::~KEvent() {}

void KEvent::wait() {
    while (1) {
        // FIX: Lost Wakeup Koruma Zırhı
        uint64_t rflags;
        __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags) : : "memory");
        
        uint64_t flags = spinlock_acquire(&this->lock);
        
        if (signaled) {
            if (auto_reset) signaled = false;
            spinlock_release(&this->lock, flags);
            if (rflags & 0x200) __asm__ volatile("sti" ::: "memory");
            return;
        }
        
        process_t* proc = (process_t*)get_current_thread_fast();
        proc->state = PROCESS_BLOCKED;
        wait_queue_push(&wait_q, proc);
        
        spinlock_release(&this->lock, flags);
        schedule();
        if (rflags & 0x200) __asm__ volatile("sti" ::: "memory");
    }
}

void KEvent::signal() {
    uint64_t flags = spinlock_acquire(&this->lock);
    
    signaled = true;
    process_t* next = wait_queue_pop_safe(&wait_q, nullptr);
    
    spinlock_release(&this->lock, flags);
    
    if (next) sched_wake_task(next);
}

void KEvent::reset() {
    uint64_t flags = spinlock_acquire(&this->lock);
    signaled = false;
    spinlock_release(&this->lock, flags);
}