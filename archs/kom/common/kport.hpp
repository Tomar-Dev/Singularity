// archs/kom/common/kport.hpp
#ifndef KPORT_HPP
#define KPORT_HPP

#ifdef __cplusplus
#include "archs/kom/common/kobject.hpp"
#include "archs/kom/common/kom.h"
#include "system/process/process.h"

struct PortPacketNode {
    kom_port_packet_t packet;
    PortPacketNode* next;
};

class KPort : public KObject {
private:
    PortPacketNode* head;
    PortPacketNode* tail;
    wait_queue_t wait_q;

public:
    KPort();
    ~KPort() override;

    error_t queue(const kom_port_packet_t* packet);
    error_t wait(kom_port_packet_t* out_packet);
};

#endif // __cplusplus
#endif