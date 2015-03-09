#include "service/io_service.hpp"
#include <pthread.h>
#include <unistd.h>
#include <cassert>
#include "util/lock.hpp"
#include "util/log.hpp"

using namespace axon::service;
namespace axon {
namespace service {

void* notify(void* arg) {
    IOService* service = static_cast<IOService*>(arg);
    while (true) {
        if (service->handler_queue_.empty()) {
            service->handler_queue_.notify_all();
        }
        if (service->stoped_) {
            break;
        }
        usleep(1000);
    }
    return NULL;
}

}
}

IOService::IOService():stoped_(false) {
    work_count_.store(0);
    job_count_.store(0);
    pthread_create(&notify_thread_, NULL, &notify, this);
}

IOService::~IOService() {
    // LOG_INFO("IOService handled %d callbacks", job_count_.load());
    stop();
}

void IOService::post(IOService::CallBack handler) {
    job_count_++;
    handler_queue_.push_back(std::move(handler));
    assert(((bool)handler) == false);
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
            add_work();
            callback();
            remove_work();
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
            add_work();
            callback();
            remove_work();
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
