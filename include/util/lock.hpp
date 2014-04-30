#pragma once
#include <pthread.h>
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

}
}
