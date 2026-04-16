// system/irq/kir.cpp
#include "system/irq/kir.hpp"
#include "libc/stdio.h"
#include "drivers/serial/serial.h"

static KInterrupt* irq_map[256] = {nullptr};
static spinlock_t kir_lock = {0, 0, {0}};

KInterrupt::KInterrupt(uint8_t vec) : KEvent(true), vector(vec) {
    this->type = KObjectType::INTERRUPT;
    KIR::registerInterrupt(this);
}

KInterrupt::~KInterrupt() {
    KIR::unregisterInterrupt(this);
}

namespace KIR {
    void init() {
        spinlock_init(&kir_lock);
    }

    void registerInterrupt(KInterrupt* irqObj) {
        if (!irqObj) return;
        uint64_t flags = spinlock_acquire(&kir_lock);
        irq_map[irqObj->getVector()] = irqObj;
        spinlock_release(&kir_lock, flags);
    }

    void unregisterInterrupt(KInterrupt* irqObj) {
        if (!irqObj) return;
        uint64_t flags = spinlock_acquire(&kir_lock);
        if (irq_map[irqObj->getVector()] == irqObj) {
            irq_map[irqObj->getVector()] = nullptr;
        }
        spinlock_release(&kir_lock, flags);
    }

    void dispatch(uint8_t vector) {
        uint64_t flags = spinlock_acquire(&kir_lock);
        KInterrupt* irqObj = irq_map[vector];
        if (irqObj) {
            irqObj->signal();
        } else {
        }
        spinlock_release(&kir_lock, flags);
    }
}

extern "C" void kir_init_c(void) {
    KIR::init();
}

extern "C" void kir_dispatch_irq(uint8_t vector) {
    KIR::dispatch(vector);
}

extern "C" void* kir_create_interrupt(uint8_t vector) {
    return new KInterrupt(vector);
}

extern "C" void kir_wait_interrupt(void* kint) {
    if (kint) {
        ((KInterrupt*)kint)->wait();
    } else {
    }
}