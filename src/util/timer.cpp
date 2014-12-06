#include <cstdio>
#include <stdexcept>
#include "util/timer.hpp"
#include "util/util.hpp"
#include "event/event_service.hpp"
#include "event/timer_wait_event.hpp"

using namespace axon::util;
using namespace axon::service;
using namespace axon::event;

Timer::Timer(IOService* service): 
    io_service_(service),
    ev_service_(&EventService::get_instance()) {
    
    fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (fd_ < 0) {
        perror("failed to create timer fd");
        throw std::runtime_error("failed to create timer fd");
    }
    fd_ev_.reset(new EventService::fd_event(fd_, io_service_));
    ev_service_->register_fd(fd_, fd_ev_);
}

Timer::~Timer() {
    EventService::get_instance().unregister_fd(fd_, fd_ev_);
    close(fd_);
    fd_ev_.reset();
    fd_ = -1;
    io_service_ = NULL;
    ev_service_ = NULL;
}

void Timer::expires_from_now(uint64_t msec) {
    itimerspec tnew, told;
    tnew= {{0, 0}, {0, 0}};
    told= {{0, 0}, {0, 0}};
    tnew.it_value.tv_sec = msec / 1000;
    tnew.it_value.tv_nsec = (msec % 1000) * 1000000;
    ENSURE_RETURN_ZERO_PERROR(timerfd_settime(fd_, 0, &tnew, &told));
}

void Timer::async_wait(CallBack callback) {
    axon::event::TimerWaitEvent::Ptr ev(new axon::event::TimerWaitEvent(fd_, callback));
    ev_service_->start_event(ev, fd_ev_);
}
