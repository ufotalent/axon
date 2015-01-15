#pragma once
#include <pthread.h>
#include <queue>
#include <atomic>
#include <cstdlib>
#include "lock.hpp"

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
        pthread_cond_init(&queue_cond_, NULL);
        count_ = 0;
    }
    void push_back(T& data) {
        if (closed_)
            return;
        ScopedLock lock(&queue_mutex_);
        base_queue_.push(std::move(data));
        count_++;
        pthread_cond_signal(&queue_cond_);
    }
    void push_back(T&& data) {
        if (closed_)
            return;
        ScopedLock lock(&queue_mutex_);
        count_++;
        base_queue_.push(std::move(data));
        pthread_cond_signal(&queue_cond_);
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
        ScopedLock lock(&queue_mutex_);
        if (closed_)
            return BlockingQueueClosed;
        if (base_queue_.empty()) {
            if (!closed_) {
                pthread_cond_wait(&queue_cond_, &queue_mutex_);
            } 
            // Another if because the closed_ variable may be changed by close()
            if (closed_) {
                return BlockingQueueClosed;
            }
            // If the queue is still empty, return interupted
            if (base_queue_.empty()) {
                return BlockingQueueInterupted;
            }
        }
        data = std::move(base_queue_.front());
        base_queue_.pop();
        count_--;
        
        return BlockingQueueSuccess;
    }

    void notify_all() {
        pthread_cond_broadcast(&queue_cond_);
    }

    void close() {
        ScopedLock lock(&queue_mutex_);
        closed_ = true;
        pthread_cond_broadcast(&queue_cond_);
    }

    bool empty() {
        if (closed_) 
            return true;
        return count_ == 0;
    }

    virtual ~BlockingQueue() {
        close();
        pthread_mutex_destroy(&queue_mutex_);
        pthread_cond_destroy(&queue_cond_);
    }
private:
    std::queue<T> base_queue_;
    pthread_cond_t queue_cond_;
    pthread_mutex_t queue_mutex_;
    volatile bool closed_;
    std::atomic_int count_;
    
};

}
}
