#pragma once
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include "event/event.hpp"
#include "service/io_service.hpp"

namespace axon {
namespace event {

template <class BufferType>
class RecvEvent: public Event {
public:
    typedef std::function<void(const axon::util::ErrorCode&, size_t)> CallBack;

    RecvEvent(int fd, int type, BufferType &buffer, CallBack callback): Event(fd, type), buffer_(buffer), callback_(callback) {
        bytes_transfered_ = 0;
    }

    bool perform() {
        ssize_t br = ::recv(fd_, buffer_.write_head(), buffer_.write_size(), MSG_DONTWAIT);
        if (br > 0) {
            buffer_.accept(br);
            ec_ = axon::util::ErrorCode::success;
            bytes_transfered_ += br;
            return true;
        } else if (br == 0) {
            ec_ = axon::util::ErrorCode::socket_closed;
            return true;
        }

        // read directly returns -1 without any data
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return false;
        }

        perror("recv error");
        switch (errno) {
        default: 
            ec_ = axon::util::ErrorCode::unknown;
            return true;
        }
    }

    void complete() {
        callback_(ec_, bytes_transfered_);
    }

private:
    BufferType& buffer_;
    CallBack callback_;

    size_t bytes_transfered_;

};

}
}
