#pragma once
#include "ip/tcp/socket.hpp"
#include "util/coroutine.hpp"
#include "buffer/nonfree_sequence_buffer.hpp"

namespace axon {
namespace socket {

class MessageSocket: public axon::ip::tcp::Socket {
public:
    MessageSocket(axon::service::IOService* service);

    struct MessageResult {
        enum message_result {
            SUCCESS = 0,
            SOCKET_FAIL = 1,
            INVALID_HEADER = 2
        };
        message_result result_;
        MessageResult(int result): result_(static_cast<message_result>(result)) {}
        operator int() const { return result_; }
    };

    class Message {
    public:
        struct MessageHeader {
            char signature[8];
            uint32_t content_length;
            uint32_t token;
        };
        const static char AXON_MESSAGE_SIGNATURE[8];
        Message();
        // create an empty message
        Message(uint32_t content_length);

        // create from raw data, including header
        Message(const char* data, uint32_t len);

        void set_data(const char* data, uint32_t len);

        MessageHeader* header() { return reinterpret_cast<MessageHeader*>(&holder_[0]);}
        const MessageHeader* header() const { return reinterpret_cast<const MessageHeader*>(&holder_[0]);}
        char* content_ptr() { return (&holder_[0]) + sizeof(MessageHeader);}
        uint32_t content_length() const { return holder_.size() - sizeof(MessageHeader);}

        char* data() { return &holder_[0]; }
        uint32_t length() const { return holder_.size(); }

        bool valid() const;
    private:
        std::vector<char> holder_;
    };

    typedef std::function<void(MessageResult)> CallBack;

    // Hint: both the following two methods are not reenterable until callback initiated
    void async_recv(Message& msg, CallBack callback);

    // content of buffer will be held by socket
    void async_send(Message& msg, CallBack callback);
private:
    axon::util::Coroutine coro_recv_;
    axon::buffer::NonfreeSequenceBuffer<char> send_buffer_;
    void async_recv_impl(Message& msg, CallBack callback);
};

}
}
