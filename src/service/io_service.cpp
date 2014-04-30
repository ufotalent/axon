#include "service/io_service.hpp"
#include "util/lock.hpp"

using namespace axon::service;

void IOService::post(const IOService::CallBack& handler) {
    handler_queue_.push_back(handler);
}

void IOService::poll() {
    while (true) {
        if (handler_queue_.empty()) {
            return;
        }
        dispatch_one();
    }

}

void IOService::run() {
    while (true) {
        if (handler_queue_.empty() && !has_work()) {
            return;
        }
        dispatch_one();
    }
}

void IOService::dispatch_one() {
    CallBack callback;
    if (handler_queue_.pop_front(callback) == decltype(handler_queue_)::BlockingQueueSuccess) {
        callback();
    }
}

void IOService::add_work(Work *work) {
    axon::util::ScopedLock lock(&works_mutex_);
    works_.insert(work);
}

void IOService::remove_work(Work *work) {
    axon::util::ScopedLock lock(&works_mutex_);
    if (!works_.count(work)) {
        throw std::runtime_error("no this work");
    }
    works_.erase(work);
    handler_queue_.notify_all();
}

bool IOService::has_work() {
    axon::util::ScopedLock lock(&works_mutex_);
    return !works_.empty();
}
