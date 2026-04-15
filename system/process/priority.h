// system/process/priority.h
#ifndef PRIORITY_H
#define PRIORITY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PRIO_IDLE      = 0,
    PRIO_LOW       = 1,
    PRIO_NORMAL    = 2,
    PRIO_HIGH      = 3,
    PRIO_REALTIME  = 4,
    PRIO_CRITICAL  = 5
} kernel_prio_t;

extern volatile kernel_prio_t current_system_priority;

static inline void set_system_priority(kernel_prio_t prio) {
    current_system_priority = prio;
}

static inline kernel_prio_t get_system_priority() {
    return current_system_priority;
}

#ifdef __cplusplus
}
#endif

#endif
