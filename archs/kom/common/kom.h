// archs/kom/common/kom.h
#ifndef KOM_H
#define KOM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t key;
    uint32_t type;
    int32_t  status;
} kom_port_packet_t;

void kom_init(void);

uint64_t kom_open(const char* path, uint16_t requested_caps);
int kom_close(uint64_t handle);
uint64_t kom_duplicate(uint64_t handle, uint16_t requested_caps);

int kom_channel_create(uint64_t* out_handle_a, uint64_t* out_handle_b);
int kom_channel_write(uint64_t handle, const void* data, uint32_t data_size, const uint64_t* handles, uint32_t handle_count);
int kom_channel_read(uint64_t handle, void* data, uint32_t* data_size, uint64_t* handles, uint32_t* handle_count);

// YENİ: KPort Sistem Çağrıları
int kom_port_create(uint64_t* out_handle);
int kom_port_queue(uint64_t handle, const kom_port_packet_t* packet);
int kom_port_wait(uint64_t handle, kom_port_packet_t* out_packet);

#ifdef __cplusplus
}
#endif

#endif