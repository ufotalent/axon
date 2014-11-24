#pragma once
#include <unistd.h>
#include <string.h>
#include "event/event.hpp"
#include "service/io_service.hpp"
#include "util/util.hpp"
#include <sys/time.h>

namespace axon {
namespace event {

class TimerWaitEvent: public Event {
public:
    typedef std::function<void(const axon::util::ErrorCode&)> CallBack;

    TimerWaitEvent(int fd, CallBack callback): Event(fd, EVENT_TYPE_READ), callback_(callback) {
    }

    bool perform() {
        uint64_t buffer;
        int ret = read(this->fd_, &buffer, sizeof(buffer));
        if (ret == sizeof(buffer)) {
            ec_ = axon::util::ErrorCode::success;
            return true;
        }
        if (ret < 0 && errno == EAGAIN) {
            return false;
        } else {
            ec_ = axon::util::ErrorCode::unknown;
            return true;
        }
    }

    void complete() {
        callback_(ec_);
    }

protected:
    CallBack callback_;


};

}
}
