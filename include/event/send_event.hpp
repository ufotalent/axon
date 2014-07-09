#pragma once
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include "event/event.hpp"
#include "service/io_service.hpp"

namespace axon {
namespace event {

template <class BufferType>
class SendEvent: public Event {
public:
    typedef std::function<void(const axon::util::ErrorCode&, size_t)> CallBack;

    SendEvent(int fd, int type, BufferType &buffer, CallBack callback): Event(fd, type), buffer_(buffer), callback_(callback), bytes_transfered_(0) {
    }

    bool perform() {
        ssize_t br = ::send(fd_, buffer_.read_head(), buffer_.read_size(), ::MSG_DONTWAIT | ::MSG_NOSIGNAL);
        if (br > 0 || (br == 0 && buffer_.read_size() == 0)) {
            buffer_.consume(br);
            ec_ = axon::util::ErrorCode::success;
            bytes_transfered_ += br;
            return true;
        } 

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return false;
        }

        perror("send error");
        switch(errno) {
        case EPIPE:
            ec_ = axon::util::ErrorCode::socket_closed;
            return true;
        //TODO: Other errors
        default:
            ec_ = axon::util::ErrorCode::unknown;
            return true;
        }
    }

    void complete() {
        callback_(ec_, bytes_transfered_);
    }

protected:
    BufferType& buffer_;
    CallBack callback_;

    size_t bytes_transfered_;

};

}
}
