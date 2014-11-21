#pragma once
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include "event/recv_event.hpp"

namespace axon {
namespace event {

template <typename BufferType, typename CompletionCondition>
class RecvUntilEvent: public axon::event::RecvEvent<BufferType> {
public:
    typedef std::function<void(const axon::util::ErrorCode&, size_t)> CallBack;
    typedef RecvEvent<BufferType> BaseType;
    RecvUntilEvent(int fd, int type, BufferType &buffer, CallBack callback, CompletionCondition condition): 
        BaseType(fd, type, buffer, callback), 
        condition_(condition) {
        data_head_ = buffer.write_head() - buffer.read_head();
        last_check_ = 0;
    }

    bool perform() {
        if (condition_(this->ec_, this->bytes_transfered_, this->buffer_.read_head() + data_head_, last_check_)) {
            return true;
        }
        if (this->buffer_.write_size() == 0) {
            return false;
        }
        while (true) {
            if (!BaseType::perform()) 
                break;
            if (this->ec_ || condition_(this->ec_, this->bytes_transfered_, this->buffer_.read_head() + data_head_, last_check_)) {
                return true;
            }
            last_check_ = this->bytes_transfered_;
        }

        // printf("false until: ec %d bt %lu\n", this->ec_, this->bytes_transfered_);
        return false;
    }

protected:
    CompletionCondition condition_;
    ssize_t data_head_;
    ssize_t last_check_;

};

}
}
