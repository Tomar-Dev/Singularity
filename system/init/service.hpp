// system/init/service.hpp
#ifndef SERVICE_HPP
#define SERVICE_HPP

#include <stdint.h>

#ifdef __cplusplus
#include "archs/cpu/x86_64/sync/spinlock.h"
#endif

typedef enum {
    SERVICE_STOPPED,
    SERVICE_STARTING,
    SERVICE_RUNNING,
    SERVICE_FINISHED, 
    SERVICE_FAILED,
    SERVICE_DEAD
} ServiceState;

typedef enum {
    RESTART_NEVER,
    RESTART_ON_FAILURE,
    RESTART_ALWAYS
} RestartPolicy;

typedef enum {
    SERVICE_TYPE_DAEMON,
    SERVICE_TYPE_ONESHOT,
    SERVICE_TYPE_BACKGROUND
} ServiceType;

#ifdef __cplusplus
struct ServiceUnit {
    char name[32];
    void (*entry_point)();
    ServiceState state;
    RestartPolicy policy;
    ServiceType type;
    int pid;
    uint32_t restarts;
    const char* dependency;
};

class ServiceManager {
private:
    static const int MAX_SERVICES = 32;
    static ServiceUnit services[MAX_SERVICES];
    static int service_count;
    static spinlock_t lock;

public:
    static void init();
    static void registerService(const char* name, void (*func)(), ServiceType type, RestartPolicy policy, const char* dep = nullptr);
    static void startAll();
    static void monitor();
    static void printStatus();
    
    static void waitForOneshots();
    static void signalFinished(const char* name);
};
#endif

#ifdef __cplusplus
extern "C" {
#endif

void service_manager_init(void);
void service_register(const char* name, void (*func)(void), ServiceType type, RestartPolicy policy);
void service_start_all(void);
void service_monitor(void);
void service_wait_oneshots(void);
void service_signal_finished(const char* name);

#ifdef __cplusplus
}
#endif

#endif // FIX: Eksik olan endif eklendi