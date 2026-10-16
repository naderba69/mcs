#ifndef CWLOCK_H
#define CWLOCK_H

/*
 * CW Lock — آلية القفل لحل Race Conditions
 * 
 * التوصية الفورية #1: إصلاح race conditions
 * 
 * المشكلة: Concurrent access إلى global state
 * الحل: إضافة locking mechanism
 * 
 * الميزة: thread-safety، استقرار، لا data corruption
 */

#include <pthread.h>
#include <stdint.h>

/* أنواع الأقفال */
#define CWLOCK_READ    0
#define CWLOCK_WRITE   1

/* Lock structure */
typedef struct {
    pthread_rwlock_t rwlock;
    int initialized;
    int read_count;
    int write_count;
    int contention_count;
} cwlock_t;

/* Global locks for different subsystems */
static cwlock_t g_cwlock_stats = {0};
static cwlock_t g_cwlock_srcscore = {0};
static cwlock_t g_cwlock_learn = {0};
static cwlock_t g_cwlock_rollback = {0};
static cwlock_t g_cwlock_multibuf = {0};
static cwlock_t g_cwlock_analytics = {0};

/* تهيئة lock */
static inline int cwlock_init(cwlock_t *lock) {
    if (!lock || lock->initialized) return -1;
    
    if (pthread_rwlock_init(&lock->rwlock, NULL) != 0) {
        return -1;
    }
    
    lock->initialized = 1;
    lock->read_count = 0;
    lock->write_count = 0;
    lock->contention_count = 0;
    
    return 0;
}

/* تدمير lock */
static inline int cwlock_destroy(cwlock_t *lock) {
    if (!lock || !lock->initialized) return -1;
    
    pthread_rwlock_destroy(&lock->rwlock);
    lock->initialized = 0;
    
    return 0;
}

/* Read lock (shared) */
static inline int cwlock_read_lock(cwlock_t *lock) {
    if (!lock || !lock->initialized) return -1;
    
    int result = pthread_rwlock_rdlock(&lock->rwlock);
    if (result == 0) {
        lock->read_count++;
    } else {
        lock->contention_count++;
    }
    
    return result;
}

/* Read unlock */
static inline int cwlock_read_unlock(cwlock_t *lock) {
    if (!lock || !lock->initialized) return -1;
    return pthread_rwlock_unlock(&lock->rwlock);
}

/* Write lock (exclusive) */
static inline int cwlock_write_lock(cwlock_t *lock) {
    if (!lock || !lock->initialized) return -1;
    
    int result = pthread_rwlock_wrlock(&lock->rwlock);
    if (result == 0) {
        lock->write_count++;
    } else {
        lock->contention_count++;
    }
    
    return result;
}

/* Write unlock */
static inline int cwlock_write_unlock(cwlock_t *lock) {
    if (!lock || !lock->initialized) return -1;
    return pthread_rwlock_unlock(&lock->rwlock);
}

/* Try read lock (non-blocking) */
static inline int cwlock_try_read_lock(cwlock_t *lock) {
    if (!lock || !lock->initialized) return -1;
    
    int result = pthread_rwlock_tryrdlock(&lock->rwlock);
    if (result == 0) {
        lock->read_count++;
    }
    
    return result;
}

/* Try write lock (non-blocking) */
static inline int cwlock_try_write_lock(cwlock_t *lock) {
    if (!lock || !lock->initialized) return -1;
    
    int result = pthread_rwlock_trywrlock(&lock->rwlock);
    if (result == 0) {
        lock->write_count++;
    }
    
    return result;
}

/* تهيئة كل الأقفال */
static inline void cwlock_init_all(void) {
    cwlock_init(&g_cwlock_stats);
    cwlock_init(&g_cwlock_srcscore);
    cwlock_init(&g_cwlock_learn);
    cwlock_init(&g_cwlock_rollback);
    cwlock_init(&g_cwlock_multibuf);
    cwlock_init(&g_cwlock_analytics);
}

/* تدمير كل الأقفال */
static inline void cwlock_destroy_all(void) {
    cwlock_destroy(&g_cwlock_stats);
    cwlock_destroy(&g_cwlock_srcscore);
    cwlock_destroy(&g_cwlock_learn);
    cwlock_destroy(&g_cwlock_rollback);
    cwlock_destroy(&g_cwlock_multibuf);
    cwlock_destroy(&g_cwlock_analytics);
}

/* الحصول على إحصائيات lock */
static inline void cwlock_get_stats(cwlock_t *lock, int *reads, int *writes, int *contentions) {
    if (!lock) return;
    if (reads) *reads = lock->read_count;
    if (writes) *writes = lock->write_count;
    if (contentions) *contentions = lock->contention_count;
}

#endif /* CWLOCK_H */
