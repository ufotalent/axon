#include "service/io_service.hpp"
#include "util/lock.hpp"
#include <pthread.h>
#include <unistd.h>

using namespace axon::service;
namespace axon {
namespace service {

void* notify(void* arg) {
    IOService* service = static_cast<IOService*>(arg);
    while (true) {
        if (service->stoped_) {
            break;
        }
        if (service->handler_queue_.empty()) {
            service->handler_queue_.notify_all();
        }
        sleep(1);
    }
    return NULL;
}

}
}

IOService::IOService():stoped_(false) {
    work_count_.store(0);
    pthread_create(&notify_thread_, NULL, &notify, this);
}

IOService::~IOService() {
    stop();
}

void IOService::post(const IOService::CallBack& handler) {
    handler_queue_.push_back(handler);
}

void IOService::poll() {
    while (true) {
        if (stoped_ || handler_queue_.empty()) {
            return;
        }
        if (!poll_one()) {
            return;
        };
    }

}

bool IOService::poll_one() {
    if (stoped_) {
        return false;
    }
    CallBack callback;
    if (handler_queue_.try_pop_front(callback) == decltype(handler_queue_)::BlockingQueueSuccess) {
        callback();
        return true;
    }
    return false;
}


void IOService::run() {
    while (true) {
        if (stoped_)
            return;
        if (handler_queue_.empty() && !has_work())
            return;
        CallBack callback;
        auto retcode = handler_queue_.pop_front(callback);
        if (retcode == decltype(handler_queue_)::BlockingQueueSuccess) {
            callback();
        } else if (retcode == decltype(handler_queue_)::BlockingQueueClosed) {
            return;
        }
    }
}

bool IOService::run_one() {
    while (true) {
        if (stoped_)
            return false;
        if (handler_queue_.empty() && !has_work())
            return false;
        CallBack callback;
        auto retcode = handler_queue_.pop_front(callback);
        if (retcode == decltype(handler_queue_)::BlockingQueueSuccess) {
            callback();
            return true;
        } else if (retcode == decltype(handler_queue_)::BlockingQueueClosed) {
            return false;
        }
    }
}

void IOService::stop() {
    stoped_ = true;
    pthread_join(notify_thread_, NULL);
    handler_queue_.close();
}

void IOService::add_work() {
    work_count_++;
}

void IOService::remove_work() {
    work_count_--;
}

bool IOService::has_work() {
    return work_count_ != 0;
}
