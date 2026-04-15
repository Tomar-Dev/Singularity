// archs/kom/common/kom.h
#ifndef KOM_H
#define KOM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void kom_init(void);

uint64_t kom_open(const char* path, uint16_t requested_caps);
int kom_close(uint64_t handle);
uint64_t kom_duplicate(uint64_t handle, uint16_t requested_caps);

#ifdef __cplusplus
}
#endif

#endif
