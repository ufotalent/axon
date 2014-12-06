#pragma once
#include "ip/tcp/socket.hpp"
#include "util/coroutine.hpp"
#include "buffer/nonfree_sequence_buffer.hpp"
#include "socket/message.hpp"

namespace axon {
namespace socket {

class MessageSocket: public axon::ip::tcp::Socket {
public:
    MessageSocket(axon::service::IOService* service);
    ~MessageSocket();

    struct MessageResult {
        enum message_result_t {
            SUCCESS = 0,
            SOCKET_FAIL = 1,
            INVALID_HEADER = 2,
            UNKNOWN = 3
        };
        message_result_t result_;
        MessageResult(): MessageResult(0) {}
        MessageResult(int result): result_(static_cast<message_result_t>(result)) {}
        operator int() const { return result_; }
    };


    typedef std::function<void(const MessageResult&)> CallBack;

    // Hint: both the following two methods are not reenterable until callback initiated
    void async_recv(Message& msg, CallBack callback);

    // content of buffer will be held by socket
    void async_send(Message& msg, CallBack callback);
private:
    pthread_mutex_t coro_exit_mutex_;
    axon::util::Coroutine coro_recv_;
    CallBack recv_callback_;
    axon::buffer::NonfreeSequenceBuffer<char> send_buffer_;
    void async_recv_impl(Message& msg);
};

}
}
