// system/security/rng.h
#ifndef RNG_H
#define RNG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void init_rng();
uint64_t get_secure_random();

#ifdef __cplusplus
}
#endif

#endif
