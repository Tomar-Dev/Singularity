// archs/kom/common/kchannel.cpp
#include "archs/kom/common/kchannel.hpp"
#include "memory/kheap.h"
#include "libc/string.h"
#include "kernel/debug.h"

error_t kchannel_create(KChannelEndpoint** out_a, KChannelEndpoint** out_b) {
    if (!out_a || !out_b) {
        return KOM_ERR_INVALID_TYPE;
    } else {
        // Valid pointers
    }

    ChannelState* state = (ChannelState*)kmalloc(sizeof(ChannelState));
    if (!state) {
        return KOM_ERR_NO_MEMORY;
    } else {
        // State allocated
    }

    spinlock_init(&state->lock);
    state->head[0] = nullptr; state->head[1] = nullptr;
    state->tail[0] = nullptr; state->tail[1] = nullptr;
    state->closed[0] = false; state->closed[1] = false;
    wait_queue_init(&state->wait_q[0]);
    wait_queue_init(&state->wait_q[1]);
    state->ref_count = 2; 

    *out_a = new KChannelEndpoint(state, 0);
    *out_b = new KChannelEndpoint(state, 1);

    if (!*out_a || !*out_b) {
        if (*out_a) delete *out_a;
        if (*out_b) delete *out_b;
        kfree(state);
        return KOM_ERR_NO_MEMORY;
    } else {
        return KOM_OK;
    }
}

KChannelEndpoint::KChannelEndpoint(ChannelState* shared_state, uint8_t id) 
    : KObject(KObjectType::CHANNEL), state(shared_state), my_id(id) 
{
    peer_id = (my_id == 0) ? 1 : 0;
}

KChannelEndpoint::~KChannelEndpoint() {
    uint64_t flags = spinlock_acquire(&state->lock);
    
    state->closed[my_id] = true;
    
    KMessage* curr = state->head[my_id];
    while (curr) {
        KMessage* next = curr->next;
        if (curr->data) kfree(curr->data);
        if (curr->handles) kfree(curr->handles);
        kfree(curr);
        curr = next;
    }
    state->head[my_id] = nullptr;
    state->tail[my_id] = nullptr;

    process_t* waiting_peer = wait_queue_pop_safe(&state->wait_q[peer_id], nullptr);
    
    state->ref_count--;
    bool destroy_state = (state->ref_count == 0);
    
    spinlock_release(&state->lock, flags);

    if (waiting_peer) {
        sched_wake_task(waiting_peer);
    } else {
        // No peer waiting
    }

    if (destroy_state) {
        kfree(state);
    } else {
        // Peer still holds reference
    }
}

error_t KChannelEndpoint::write(const void* data, uint32_t data_size, const handle_t* handles, uint32_t handle_count) {
    if (data_size > 0 && !data) return KOM_ERR_INVALID_TYPE;
    if (handle_count > 0 && !handles) return KOM_ERR_INVALID_TYPE;

    KMessage* msg = (KMessage*)kmalloc(sizeof(KMessage));
    if (!msg) return KOM_ERR_NO_MEMORY;

    msg->data_size = data_size;
    msg->handle_count = handle_count;
    msg->next = nullptr;

    if (data_size > 0) {
        msg->data = kmalloc(data_size);
        if (!msg->data) {
            kfree(msg);
            return KOM_ERR_NO_MEMORY;
        } else {
            memcpy(msg->data, data, data_size);
        }
    } else {
        msg->data = nullptr;
    }

    if (handle_count > 0) {
        msg->handles = (handle_t*)kmalloc(handle_count * sizeof(handle_t));
        if (!msg->handles) {
            if (msg->data) kfree(msg->data);
            kfree(msg);
            return KOM_ERR_NO_MEMORY;
        } else {
            memcpy(msg->handles, handles, handle_count * sizeof(handle_t));
        }
    } else {
        msg->handles = nullptr;
    }

    uint64_t flags = spinlock_acquire(&state->lock);

    if (state->closed[peer_id]) {
        spinlock_release(&state->lock, flags);
        if (msg->data) kfree(msg->data);
        if (msg->handles) kfree(msg->handles);
        kfree(msg);
        return KOM_ERR_PEER_CLOSED;
    } else {
        // Peer is alive, enqueue message
    }

    if (!state->head[peer_id]) {
        state->head[peer_id] = msg;
        state->tail[peer_id] = msg;
    } else {
        state->tail[peer_id]->next = msg;
        state->tail[peer_id] = msg;
    }

    process_t* waiting_peer = wait_queue_pop_safe(&state->wait_q[peer_id], nullptr);
    
    spinlock_release(&state->lock, flags);

    if (waiting_peer) {
        sched_wake_task(waiting_peer);
    } else {
        // Peer was not blocking
    }

    return KOM_OK;
}

error_t KChannelEndpoint::read(void* data, uint32_t* data_size, handle_t* handles, uint32_t* handle_count) {
    if (!data_size || !handle_count) return KOM_ERR_INVALID_TYPE;

    while (1) {
        uint64_t rflags;
        __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags) : : "memory");
        
        uint64_t flags = spinlock_acquire(&state->lock);

        KMessage* msg = state->head[my_id];

        if (msg) {
            if (*data_size < msg->data_size || *handle_count < msg->handle_count) {
                *data_size = msg->data_size;
                *handle_count = msg->handle_count;
                spinlock_release(&state->lock, flags);
                if (rflags & 0x200) __asm__ volatile("sti" ::: "memory");
                return KOM_ERR_BUFFER_TOO_SMALL;
            } else {
                // Buffer is sufficient
            }

            state->head[my_id] = msg->next;
            if (!state->head[my_id]) {
                state->tail[my_id] = nullptr;
            } else {
                // Queue still has items
            }

            spinlock_release(&state->lock, flags);

            *data_size = msg->data_size;
            *handle_count = msg->handle_count;

            if (msg->data_size > 0 && data) {
                memcpy(data, msg->data, msg->data_size);
            } else {
                // No data payload
            }

            if (msg->handle_count > 0 && handles) {
                memcpy(handles, msg->handles, msg->handle_count * sizeof(handle_t));
            } else {
                // No handle payload
            }

            if (msg->data) kfree(msg->data);
            if (msg->handles) kfree(msg->handles);
            kfree(msg);

            if (rflags & 0x200) __asm__ volatile("sti" ::: "memory");
            return KOM_OK;

        } else if (state->closed[peer_id]) {
            spinlock_release(&state->lock, flags);
            if (rflags & 0x200) __asm__ volatile("sti" ::: "memory");
            return KOM_ERR_PEER_CLOSED;
        } else {
            process_t* proc = (process_t*)get_current_thread_fast();
            proc->state = PROCESS_BLOCKED;
            wait_queue_push(&state->wait_q[my_id], proc);
            spinlock_release(&state->lock, flags);
            
            schedule();
            
            if (rflags & 0x200) __asm__ volatile("sti" ::: "memory");
        }
    }
}