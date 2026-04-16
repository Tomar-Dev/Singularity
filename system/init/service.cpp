// system/init/service.cpp
#include "system/init/service.hpp"
#include "libc/string.h"
#include "libc/stdio.h"
#include "system/process/process.h"
#include "drivers/serial/serial.h"

extern "C" {
    void print_status(const char* prefix, const char* msg, const char* status);
    void timer_sleep(uint64_t ticks);
    uint64_t timer_get_ticks();
    void yield();
}

ServiceUnit ServiceManager::services[MAX_SERVICES];
int ServiceManager::service_count = 0;
spinlock_t ServiceManager::lock = {0, 0, {0}};

void ServiceManager::init() {
    spinlock_init(&lock);
    service_count = 0;
    print_status("[ SINGD]", "Service Manager v6.9 Initialized", "INFO");
}

void ServiceManager::registerService(const char* name, void (*func)(), ServiceType type, RestartPolicy policy, const char* dep) {
    if (service_count >= MAX_SERVICES) {
        serial_write("[SINGD] Error: Max service limit reached!\n");
        return;
    } else {
        // Proceed with registration
    }
    
    ServiceUnit* s = &services[service_count++];
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    strncpy(s->name, name, 31);
    s->name[31] = '\0';
    s->entry_point = func;
    s->state = SERVICE_STOPPED;
    s->type = type;
    s->policy = policy;
    s->pid = -1;
    s->restarts = 0;
    s->dependency = dep;
}

void ServiceManager::startAll() {
    print_status("[ SINGD]", "Starting System Services...", "INFO");
    
    for (int i = 0; i < service_count; i++) {
        ServiceUnit* s = &services[i];
        
        if (s->dependency) {
            bool dep_ok = false;
            for (int j = 0; j < service_count; j++) {
                if (strcmp(services[j].name, s->dependency) == 0 && 
                   (services[j].state == SERVICE_RUNNING || services[j].state == SERVICE_FINISHED)) {
                    dep_ok = true;
                    break;
                } else {
                    // Not the dependency or not ready
                }
            }
            if (!dep_ok) {
                serial_printf("[SINGD] Delaying %s (waiting for %s)\n", s->name, s->dependency);
                continue;
            } else {
                // Dependency is met, proceed
            }
        } else {
            // No dependency
        }
        
        if (s->state == SERVICE_STOPPED) {
            s->state = SERVICE_STARTING;
            create_kernel_task(s->entry_point);
            s->state = SERVICE_RUNNING;
            
            char buf[64];
            // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
            snprintf(buf, sizeof(buf), "Started %s", s->name);
            print_status("[ SINGD]", buf, "OK");
        } else {
            // Service already started or in another state
        }
    }
}

void ServiceManager::signalFinished(const char* name) {
    uint64_t flags = spinlock_acquire(&lock);
    for(int i=0; i<service_count; i++) {
        if(strcmp(services[i].name, name) == 0) {
            if (services[i].type == SERVICE_TYPE_ONESHOT || services[i].type == SERVICE_TYPE_BACKGROUND) {
                services[i].state = SERVICE_FINISHED;
            } else {
                // Daemon services don't "finish" normally
            }
            break;
        } else {
            // Not the target service
        }
    }
    spinlock_release(&lock, flags);
}

void ServiceManager::monitor() {
    while (true) {
        timer_sleep(250);
        
        for (int i = 0; i < service_count; i++) {
            ServiceUnit* s = &services[i];
            
            if (s->state == SERVICE_RUNNING) {
                bool alive = false;
                
                rwlock_acquire_read(task_list_lock);
                process_t* curr = process_list_head;
                if (curr) {
                    do {
                        if (curr->thread_fn == s->entry_point && 
                            curr->state != PROCESS_ZOMBIE && 
                            curr->state != PROCESS_DEAD) 
                        {
                            alive = true;
                            break;
                        } else {
                            // Not the target thread or thread is dead
                        }
                        curr = curr->next;
                    } while (curr != process_list_head);
                } else {
                    // No processes
                }
                rwlock_release_read(task_list_lock);
                
                if (!alive) {
                    if (s->type == SERVICE_TYPE_ONESHOT || s->type == SERVICE_TYPE_BACKGROUND) {
                        s->state = SERVICE_FINISHED;
                    } else {
                        s->state = SERVICE_DEAD;
                        serial_printf("[SINGD] Service %s died unexpectedly!\n", s->name);
                        
                        if (s->policy == RESTART_ALWAYS || s->policy == RESTART_ON_FAILURE) {
                            serial_printf("[SINGD] Restarting %s (%d)...\n", s->name, s->restarts + 1);
                            create_kernel_task(s->entry_point);
                            s->state = SERVICE_RUNNING;
                            s->restarts++;
                        } else {
                            // Do not restart
                        }
                    }
                } else {
                    // Service is still alive
                }
            } else {
                // Service is not running, no need to monitor liveness
            }
        }
    }
}

void ServiceManager::waitForOneshots() {
    uint64_t start_tick = timer_get_ticks();
    uint64_t timeout_ticks = 1000; // 4 seconds timeout
    
    while (true) {
        bool all_done = true;
        for (int i = 0; i < service_count; i++) {
            if (services[i].type == SERVICE_TYPE_ONESHOT && services[i].state == SERVICE_RUNNING) {
                all_done = false;
                break;
            } else {
                // Not a running oneshot
            }
        }
        
        if (all_done) {
            break; 
        } else {
            // Keep waiting
        }
        
        if (timer_get_ticks() - start_tick > timeout_ticks) {
            serial_write("[SINGD] Warning: Timeout waiting for ONESHOT services.\n");
            break;
        } else {
            // Still within timeout
        }
        
        yield(); 
    }
}

void ServiceManager::printStatus() {
    printf("\n%-20s | %-10s | %-8s\n", "SERVICE", "STATUS", "RESTARTS");
    printf("---------------------+------------+----------\n");
    for (int i = 0; i < service_count; i++) {
        // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
        const char* st = "UNKNOWN";
        switch(services[i].state) {
            case SERVICE_RUNNING:  st = "RUNNING";  break;
            case SERVICE_STOPPED:  st = "STOPPED";  break;
            case SERVICE_STARTING: st = "STARTING"; break;
            case SERVICE_FINISHED: st = "FINISHED"; break;
            case SERVICE_FAILED:   st = "FAILED";   break;
            case SERVICE_DEAD:     st = "DEAD";     break;
            default:               st = "UNKNOWN";  break;
        }
        printf("%-20s | %-10s | %d\n", services[i].name, st, services[i].restarts);
    }
}

extern "C" {
    void service_manager_init() { ServiceManager::init(); }
    void service_register(const char* name, void (*func)(), ServiceType type, RestartPolicy policy) {
        ServiceManager::registerService(name, func, type, policy, nullptr);
    }
    void service_start_all() { ServiceManager::startAll(); }
    void service_monitor() { ServiceManager::monitor(); }
    void service_wait_oneshots() { ServiceManager::waitForOneshots(); }
    void service_signal_finished(const char* name) { ServiceManager::signalFinished(name); }
}