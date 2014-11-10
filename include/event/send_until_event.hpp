#pragma once
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include "event/send_event.hpp"

namespace axon {
namespace event {

template <typename BufferType, typename CompletionCondition>
class SendUntilEvent: public axon::event::SendEvent<BufferType> {
public:
    typedef std::function<void(const axon::util::ErrorCode&, size_t)> CallBack;
    typedef SendEvent<BufferType> BaseType;
    SendUntilEvent(int fd, int type, BufferType &buffer, CallBack callback, CompletionCondition condition): 
        BaseType(fd, type, buffer, callback), 
        condition_(condition),
        stored_buffer_(buffer.read_head(), buffer.read_head() + buffer.read_size() ){
        last_check_ = 0;
    }

    bool perform() {
        if (condition_(this->ec_, this->bytes_transfered_, &stored_buffer_[0], last_check_)) {
            return true;
        }
        if (this->buffer_.read_size() == 0) {
            return false;
        }
        while (true) {
            if (!BaseType::perform()) 
                break;
            printf("true until: ec %d bt %lu\n", this->ec_, this->bytes_transfered_);
            if (this->ec_ || condition_(this->ec_, this->bytes_transfered_, &stored_buffer_[0], last_check_)) {
                return true;
            }
            last_check_ = this->bytes_transfered_;
        }

        // printf("false until: ec %d bt %lu\n", this->ec_, this->bytes_transfered_);
        return false;
    }

protected:
    CompletionCondition condition_;
    ssize_t last_check_;

    // Store buffer content for checking
    std::vector<typename BufferType::ElementType> stored_buffer_;

};

}
}
