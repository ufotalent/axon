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

    }

    bool perform() {
        while (true) {
            if (this->buffer_.write_size() == 0)  {
                this->buffer_.prepare(512);
            }
            if (!BaseType::perform()) 
                break;
            // printf("true until: ec %d bt %lu\n", this->ec_, this->bytes_transfered_);
            if (condition_(this->ec_, this->bytes_transfered_)) {
                return true;
            }
        }

        // printf("false until: ec %d bt %lu\n", this->ec_, this->bytes_transfered_);
        return false;
    }

protected:
    CompletionCondition condition_;

};

}
}
