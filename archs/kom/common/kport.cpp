// archs/kom/common/kport.cpp
#include "archs/kom/common/kport.hpp"
#include "memory/kheap.h"
#include "libc/string.h"
#include "kernel/debug.h"

KPort::KPort() : KObject(KObjectType::PORT), head(nullptr), tail(nullptr) {
    wait_queue_init(&wait_q);
}

KPort::~KPort() {
    uint64_t flags = spinlock_acquire(&lock);
    
    PortPacketNode* curr = head;
    while (curr) {
        PortPacketNode* next = curr->next;
        kfree(curr);
        curr = next;
    }
    head = nullptr;
    tail = nullptr;

    while (true) {
        process_t* waiter = wait_queue_pop_safe(&wait_q, nullptr);
        if (!waiter) break;
        sched_wake_task(waiter);
    }
    
    spinlock_release(&lock, flags);
}

error_t KPort::queue(const kom_port_packet_t* packet) {
    if (!packet) return KOM_ERR_INVALID_TYPE;

    PortPacketNode* node = (PortPacketNode*)kmalloc(sizeof(PortPacketNode));
    if (!node) return KOM_ERR_NO_MEMORY;

    node->packet = *packet;
    node->next = nullptr;

    uint64_t flags = spinlock_acquire(&lock);

    if (!head) {
        head = node;
        tail = node;
    } else {
        tail->next = node;
        tail = node;
    }

    process_t* waiter = wait_queue_pop_safe(&wait_q, nullptr);
    
    spinlock_release(&lock, flags);

    if (waiter) {
        sched_wake_task(waiter);
    }

    return KOM_OK;
}

error_t KPort::wait(kom_port_packet_t* out_packet) {
    if (!out_packet) return KOM_ERR_INVALID_TYPE;

    while (true) {
        uint64_t rflags;
        __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags) : : "memory");
        
        uint64_t flags = spinlock_acquire(&lock);

        if (head) {
            PortPacketNode* node = head;
            head = node->next;
            if (!head) {
                tail = nullptr;
            }
            
            *out_packet = node->packet;
            
            spinlock_release(&lock, flags);
            kfree(node);
            
            if (rflags & 0x200) __asm__ volatile("sti" ::: "memory");
            return KOM_OK;
        }

        process_t* proc = (process_t*)get_current_thread_fast();
        proc->state = PROCESS_BLOCKED;
        wait_queue_push(&wait_q, proc);
        
        spinlock_release(&lock, flags);
        schedule();
        
        if (rflags & 0x200) __asm__ volatile("sti" ::: "memory");
    }
}