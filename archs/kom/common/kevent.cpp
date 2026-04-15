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
        // Spinlock_acquire zaten RFLAGS'i kaydeder ve kesmeleri (CLI) kapatır.
        uint64_t flags = spinlock_acquire(&this->lock);
        
        if (signaled) {
            // Sinyal alındı! Olayı sıfırla ve thread'i özgür bırak.
            if (auto_reset) signaled = false;
            spinlock_release(&this->lock, flags);
            return;
        }
        
        // Sinyal yoksa thread'i uykuya yatır.
        process_t* proc = (process_t*)get_current_thread_fast();
        proc->state = PROCESS_BLOCKED;
        wait_queue_push(&wait_q, proc);
        
        // Spinlock bırakıldığı an kesmeler (eğer açıksa) otomatik geri gelir.
        spinlock_release(&this->lock, flags);
        
        // İşlemciyi terk et. Bir IRQ bizi uyandırana kadar buradayız.
        schedule();
    }
}

void KEvent::signal() {
    uint64_t flags = spinlock_acquire(&this->lock);
    
    signaled = true;
    process_t* next = wait_queue_pop_safe(&wait_q, nullptr);
    
    spinlock_release(&this->lock, flags);
    
    // Uyuyan bir görev varsa onu Runqueue'ya (Hazır Kuyruğuna) ekle.
    // Görev uyandığında yukarıdaki while(1) döngüsüne tekrar girecek 
    // ve signaled bayrağını kendisi temizleyecek.
    if (next) sched_wake_task(next);
}

void KEvent::reset() {
    uint64_t flags = spinlock_acquire(&this->lock);
    signaled = false;
    spinlock_release(&this->lock, flags);
}