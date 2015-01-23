#pragma once
#include <pthread.h>
#include <semaphore.h>
#include <queue>
#include <atomic>
#include <cstdlib>
#include "lock.hpp"
#include "util/log.hpp"

namespace axon {
namespace util {

template <typename T>
class BlockingQueue {
public:
    enum BlockingQueueReturnStatus {
        BlockingQueueSuccess = 0,
        BlockingQueueClosed = 1,
        BlockingQueueDrained = 2,
        BlockingQueueInterupted= 3,
    };
   
    BlockingQueue():closed_(false) {
        pthread_mutex_init(&queue_mutex_, NULL);
        sem_init(&sem_, 0, 0);
        count_ = 0;
    }
    void push_back(T& data) {
        if (closed_)
            return;
        {
            ScopedLock lock(&queue_mutex_);
            base_queue_.push(std::move(data));
        }
        count_++;
        sem_post(&sem_);
    }
    void push_back(T&& data) {
        if (closed_)
            return;
        {
            ScopedLock lock(&queue_mutex_);
            base_queue_.push(std::move(data));
        }
        count_++;
        sem_post(&sem_);
    }

    BlockingQueueReturnStatus try_pop_front(T& data) {
        ScopedLock lock(&queue_mutex_);
        if (closed_)
            return BlockingQueueClosed;
        if (base_queue_.empty()) {
            return BlockingQueueDrained;
        }
        data = std::move(base_queue_.front());
        base_queue_.pop();
        count_--;
        
        return BlockingQueueSuccess;
    }

    BlockingQueueReturnStatus pop_front(T& data) {
        if (closed_)
            return BlockingQueueClosed;
        sem_wait(&sem_);
        ScopedLock lock(&queue_mutex_);
        if (base_queue_.empty()) {
            if (closed_) {
                return BlockingQueueClosed;
            } else {
                return BlockingQueueInterupted;
            }
        }
        data = std::move(base_queue_.front());
        base_queue_.pop();
        count_--;
        
        return BlockingQueueSuccess;
    }

    void notify_all() {
        for (int i = 0; i < 1000; i++) {
           sem_post(&sem_);
        }
    }

    void close() {
        {
            ScopedLock lock(&queue_mutex_);
            closed_ = true;
        }
        notify_all();
    }

    bool empty() {
        if (closed_) 
            return true;
        return count_ == 0;
    }

    virtual ~BlockingQueue() {
        close();
        pthread_mutex_destroy(&queue_mutex_);
        sem_destroy(&sem_);
    }
private:
    std::queue<T> base_queue_;
    pthread_mutex_t queue_mutex_;
    sem_t sem_;
    std::atomic_bool closed_;
    std::atomic_int count_;
    
};

}
}
