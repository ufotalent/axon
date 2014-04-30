#pragma once

#include <pthread.h>
#include <functional>
#include <set>
#include "util/blocking_queue.hpp"
#include "util/lock.hpp"
namespace axon {
namespace service {

class IOService {
public:
    IOService() {
        pthread_mutex_init(&works_mutex_, NULL);
    }
    virtual ~IOService() {
        pthread_mutex_destroy(&works_mutex_);
    }

    typedef std::function<void()> CallBack;

    // Work object is used to prevent run() calls from exiting before event handlers are posted
    // For instance when a single thread called async_read on a socket, the handler queue is 
    // empty until read success, we need a Work object to indicate the read work.
    class Work {
    public:
        Work(IOService &service):service_(service) {
            service_.add_work(this);
        }
        virtual ~Work() {
            service_.remove_work(this);
        }
        Work(const Work&) = delete;
        Work& operator=(const Work&) = delete;
    private:
        IOService& service_;
    };

public:
    void poll();
    void run();
    void post(const CallBack& handler);
    IOService(const IOService &) = delete;
    IOService& operator=(const IOService &) = delete;

private:
    void add_work(Work *work);
    void remove_work(Work *work);
    bool has_work();
    axon::util::BlockingQueue<CallBack> handler_queue_;

    void dispatch_one();
    std::set<Work*> works_;
    pthread_mutex_t works_mutex_;
};


}
}
