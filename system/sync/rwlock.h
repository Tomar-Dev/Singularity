// system/sync/rwlock.h
#ifndef RWLOCK_H
#define RWLOCK_H

// Maximum number of concurrent readers allowed to hold the lock at once.
// This cap prevents writer starvation: new readers are blocked once this
// limit is reached, giving waiting writers a chance to acquire the lock.
// 1024 is chosen as a conservative upper bound; a real system should never
// legitimately hit this number. If it does, something is architecturally wrong.
#define RWLOCK_MAX_READERS 1024

#ifdef __cplusplus
extern "C" {
#endif

struct native_rwlock;
typedef struct native_rwlock* rwlock_t;

rwlock_t rwlock_create();
void rwlock_destroy(rwlock_t lock);
void rwlock_acquire_read(rwlock_t lock);
void rwlock_release_read(rwlock_t lock);
void rwlock_acquire_write(rwlock_t lock);
void rwlock_release_write(rwlock_t lock);

#ifdef __cplusplus
}

class ScopedReadLock {
private:
    rwlock_t lock;
public:
    explicit ScopedReadLock(rwlock_t l) : lock(l) {
        rwlock_acquire_read(lock);
    }
    ~ScopedReadLock() {
        rwlock_release_read(lock);
    }

    // Non-copyable, non-movable — owning a lock scope must be explicit.
    ScopedReadLock(const ScopedReadLock&)            = delete;
    ScopedReadLock& operator=(const ScopedReadLock&) = delete;
};

class ScopedWriteLock {
private:
    rwlock_t lock;
public:
    explicit ScopedWriteLock(rwlock_t l) : lock(l) {
        rwlock_acquire_write(lock);
    }
    ~ScopedWriteLock() {
        rwlock_release_write(lock);
    }

    // Non-copyable, non-movable.
    ScopedWriteLock(const ScopedWriteLock&)            = delete;
    ScopedWriteLock& operator=(const ScopedWriteLock&) = delete;
};

#endif
#endif