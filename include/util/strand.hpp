#pragma once
#include <cassert>
#include <unistd.h>
#include <atomic>
#include <functional>
#include <stack>
#include "util/lock.hpp"
#include "util/log.hpp"
#include "service/io_service.hpp"

namespace axon {
namespace util {

class Strand {
    template <typename T>
    class LockFreeQueue{
    public:
        struct Node {
            Node* next;
            T data;
            Node(T&& d): next(NULL), data(std::move(d)) {
            }
        };

        LockFreeQueue(): head_(NULL) {
        }

        // it seems that we do not suffer ABA problem here
        void push(T&& data) {
            Node* new_head = new Node(std::move(data));
            new_head->next = head_.load(std::memory_order_relaxed);

            Node* old_head  = head_;
            do {
                new_head->next = old_head;
            } while (!head_.compare_exchange_weak(old_head, new_head, std::memory_order_release, std::memory_order_relaxed));
        }

        void push(T& data) {
            push(std::move(data));
        }

        // following calls are not synced, only one thread can enter
        Node* take_all() {
            return head_.exchange(NULL);
        }

        bool empty() {
            return head_.load() == NULL;
        }
    private:
        std::atomic<Node* > head_;
    };

    typedef axon::service::IOService::CallBack CallBack;

    LockFreeQueue<CallBack>::Node* reverse_list(LockFreeQueue<CallBack>::Node* head) {
        if (head == NULL || head->next == NULL) {
            return head;
        }
        LockFreeQueue<CallBack>::Node* last = head;
        LockFreeQueue<CallBack>::Node* p = head->next;
        head->next = NULL;
        while (p) {
            LockFreeQueue<CallBack>::Node* next = p->next;
            p->next = last;
            last = p;
            p = next;
        }
        return last;
    }

    void do_dispatch() {
        bool should_perform = false;
        bool expected = false;
        pthread_rwlock_rdlock(&rwlock_);
        if (has_pending_tests_.compare_exchange_strong(expected, true)) {
            should_perform = true;
        }
        pthread_rwlock_unlock(&rwlock_);
        if (should_perform) {
            perform();
        }
    }
public:
    Strand(axon::service::IOService* io_service): io_service_(io_service) {
        has_pending_tests_ = false;
        pthread_rwlock_init(&rwlock_, NULL);
    }
    virtual ~Strand() {
        pthread_rwlock_destroy(&rwlock_);
    }

    void dispatch(CallBack callback) {
        queue_.push(std::move(callback));
        do_dispatch();
    }
    
    void perform() {
        while (true) {
            assert(has_pending_tests_);

            pthread_rwlock_wrlock(&rwlock_);
            bool queue_empty = queue_.empty();
            // here has_pending_tests_ = true, hence if has_pending_tests_ == queue_empty, mark it false and return
            if (has_pending_tests_.compare_exchange_strong(queue_empty, false)) {
                pthread_rwlock_unlock(&rwlock_);
                return;
            }
            pthread_rwlock_unlock(&rwlock_);

            LockFreeQueue<CallBack>::Node* list = queue_.take_all();
            LockFreeQueue<CallBack>::Node* reversed_list = reverse_list(list);
            while (reversed_list) {
                reversed_list->data();
                auto last = reversed_list;
                reversed_list = reversed_list->next;
                // deleting last here may destroy the last shared_ptr of consistent socket, which contains this strand, this is a bug
                delete last;
            }
        }
    }

    void post(CallBack callback) {
        queue_.push(std::move(callback));
        pthread_rwlock_rdlock(&rwlock_);
        bool should_post = !has_pending_tests_;
        pthread_rwlock_unlock(&rwlock_);
        if (should_post) {
            io_service_->post(std::bind(&Strand::do_dispatch, this));
        }
        
    }

    template <class ...Args>
    std::function<void(Args...)> wrap(std::function<void(Args...)> &&f) {
        return [f, this](Args... args) {
            dispatch(std::bind(f, args...));
        };
    }
private:
    std::atomic_bool has_pending_tests_;
    LockFreeQueue<axon::service::IOService::CallBack> queue_;
    axon::service::IOService* io_service_;
    pthread_rwlock_t rwlock_;
};

}
}
