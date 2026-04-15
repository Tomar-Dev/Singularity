// archs/kom/common/kevent.hpp
#ifndef KEVENT_HPP
#define KEVENT_HPP

#ifdef __cplusplus

#include "archs/kom/common/kobject.hpp"
#include "system/process/process.h"

class KEvent : public KObject {
protected:
    bool signaled;
    bool auto_reset;
    wait_queue_t wait_q;

public:
    KEvent(bool autoReset = true);
    virtual ~KEvent() override;

    // Engellenen (Blocking) bekleme fonksiyonu. Kesme gelene kadar Thread uyur.
    void wait();
    
    // Hard-IRQ tarafından tetiklenir, Thread'i uyandırır.
    void signal();
    
    void reset();
};

#endif // __cplusplus

#endif