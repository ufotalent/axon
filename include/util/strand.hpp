#pragma once
#include <cassert>
#include <atomic>
#include <functional>
#include <stack>
#include "util/lock.hpp"
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

public:
    Strand(axon::service::IOService* io_service): io_service_(io_service) {
        has_pending_tests_ = false;
    }

    void post(CallBack callback) {
        queue_.push(std::move(callback));

        spin_lock_.lock();
        bool should_reschedule = !has_pending_tests_;
        if (should_reschedule) {
            has_pending_tests_ = true;
        }
        spin_lock_.unlock();
        if (should_reschedule) {
            io_service_->post(std::bind(&Strand::perform, this));
        }
    }
    void perform() {
        while (true) {
            spin_lock_.lock();
            assert(has_pending_tests_);
            if (queue_.empty()) {
                has_pending_tests_ = false;
                spin_lock_.unlock();
                return;
            }
            spin_lock_.unlock();
            LockFreeQueue<CallBack>::Node* list = queue_.take_all();
            std::stack<LockFreeQueue<CallBack>::Node*> reverse_stack;
            while (list) {
                reverse_stack.push(list);
                list = list->next;
            }
            while (!reverse_stack.empty()) {
                reverse_stack.top()->data();
                reverse_stack.pop();
            }
        }
    
    }
private:
    bool has_pending_tests_;
    LockFreeQueue<axon::service::IOService::CallBack> queue_;
    axon::util::SpinLock spin_lock_;
    axon::service::IOService* io_service_;


};

}
}
