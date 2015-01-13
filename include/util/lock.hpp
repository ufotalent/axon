#pragma once
#include <pthread.h>
#include <atomic>

namespace axon {
namespace util {

class  ScopedLock {
public:
    ScopedLock(pthread_mutex_t* mutex):mutex_(mutex) {
        pthread_mutex_lock(mutex_); 
    } 
    virtual ~ScopedLock() {
        pthread_mutex_unlock(mutex_);
    }
private:
    pthread_mutex_t *mutex_;
};

class SpinLock {
    std::atomic_bool spin_lock_;
public:
    SpinLock() {
        spin_lock_ = false;
    }
    void lock() {
        bool expected = false;
        do {
            expected = false;
        } while (!spin_lock_.compare_exchange_weak(expected, true, std::memory_order_acquire, std::memory_order_relaxed));
    }
    void unlock() {
        spin_lock_.store(false, std::memory_order_release);
    }
};

}
}
