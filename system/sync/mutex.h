// system/sync/mutex.h
#ifndef MUTEX_H
#define MUTEX_H

#ifdef __cplusplus
extern "C" {
#endif

struct native_mutex;
typedef struct native_mutex* mutex_t;

struct native_semaphore;
typedef struct native_semaphore* semaphore_t;

mutex_t mutex_create();
void mutex_destroy(mutex_t m);
void mutex_lock(mutex_t m);
void mutex_unlock(mutex_t m);

semaphore_t sem_create(int initial_count);
void sem_destroy(semaphore_t s);
void sem_wait(semaphore_t s);
void sem_signal(semaphore_t s);

#ifdef __cplusplus
}
#endif

#endif
