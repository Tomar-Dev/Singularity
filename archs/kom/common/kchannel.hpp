// archs/kom/common/kchannel.hpp
#ifndef KCHANNEL_HPP
#define KCHANNEL_HPP

#ifdef __cplusplus
#include "archs/kom/common/kobject.hpp"
#include "system/process/process.h"

struct KMessage {
    void* data;
    uint32_t data_size;
    handle_t* handles;
    uint32_t handle_count;
    KMessage* next;
};

struct ChannelState {
    spinlock_t lock;
    KMessage* head[2];
    KMessage* tail[2];
    wait_queue_t wait_q[2];
    bool closed[2];
    uint32_t ref_count;
};

class KChannelEndpoint : public KObject {
private:
    ChannelState* state;
    uint8_t my_id;
    uint8_t peer_id;

public:
    KChannelEndpoint(ChannelState* shared_state, uint8_t id);
    ~KChannelEndpoint() override;

    error_t write(const void* data, uint32_t data_size, const handle_t* handles, uint32_t handle_count);
    error_t read(void* data, uint32_t* data_size, handle_t* handles, uint32_t* handle_count);
};

error_t kchannel_create(KChannelEndpoint** out_a, KChannelEndpoint** out_b);

#endif // __cplusplus
#endif