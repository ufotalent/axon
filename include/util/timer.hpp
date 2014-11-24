#pragma once
#include <sys/timerfd.h>
#include <functional>
#include "util/error_code.hpp"
#include "event/event_service.hpp"

namespace axon {
namespace util {

class Timer {
public:
    Timer(axon::service::IOService* service);
    ~Timer();
    typedef std::function<void(const axon::util::ErrorCode& ec)> CallBack;

    void expires_from_now(uint64_t msec);
    void async_wait(CallBack callback);
private:
    int fd_;
    axon::service::IOService* io_service_;
    axon::event::EventService* ev_service_;
    axon::event::EventService::fd_event::Ptr fd_ev_;

};

}
}
