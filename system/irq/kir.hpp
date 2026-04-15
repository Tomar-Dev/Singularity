// system/irq/kir.hpp
#ifndef KIR_HPP
#define KIR_HPP

#include <stdint.h>

#ifdef __cplusplus

#include "archs/kom/common/kevent.hpp"

class KInterrupt : public KEvent {
private:
    uint8_t vector;

public:
    KInterrupt(uint8_t vec);
    ~KInterrupt() override;
    uint8_t getVector() const { return vector; }
};

namespace KIR {
    void init();
    void registerInterrupt(KInterrupt* irqObj);
    void unregisterInterrupt(KInterrupt* irqObj);
    void dispatch(uint8_t vector);
}

#endif // __cplusplus

#ifdef __cplusplus
extern "C" {
#endif

void kir_init_c(void);
void kir_dispatch_irq(uint8_t vector);

// C Sürücüleri (Örn: Fare) için arayüz
void* kir_create_interrupt(uint8_t vector);
void  kir_wait_interrupt(void* kint);

#ifdef __cplusplus
}
#endif

#endif