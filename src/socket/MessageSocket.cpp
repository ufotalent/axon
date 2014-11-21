#include "socket/MessageSocket.hpp"
#include <cstring>
#include <cassert>
#include "buffer/nonfree_sequence_buffer.hpp"


using namespace axon::socket;
using namespace axon::service;
using namespace axon::buffer;
using namespace axon::util;

const char MessageSocket::Message::AXON_MESSAGE_SIGNATURE[8] = {'A', 'X', 'O', 'N', 'M', 'S', 'G', 0};
MessageSocket::Message::Message() :
    Message(0) {
}

MessageSocket::Message::Message(uint32_t content_length):
    holder_(content_length + sizeof(MessageHeader), 0) {

    MessageHeader *header_ = header();
    memcpy(header_->signature, AXON_MESSAGE_SIGNATURE, sizeof(AXON_MESSAGE_SIGNATURE));
    header_->content_length = content_length;
}

MessageSocket::Message::Message(const char* data, uint32_t len):
    holder_(data, data + len) {
}

void MessageSocket::Message::set_data(const char* data, uint32_t len) {
    holder_ = std::vector<char>(data, data + len);
}

bool MessageSocket::Message::valid() const {
    if (length() < sizeof(MessageHeader)) {
        return false;
    }
    const MessageHeader *header_ = header();
    return (header_->content_length == content_length()) && (memcmp(AXON_MESSAGE_SIGNATURE, header_->signature, sizeof(AXON_MESSAGE_SIGNATURE)) == 0);
}


MessageSocket::MessageSocket(IOService *service):Socket(service) {

}

void MessageSocket::async_recv(Message& msg, CallBack callback) {
    io_service_->post([this, &msg, callback]() {
        coro_recv_.set_function(std::bind(&MessageSocket::async_recv_impl, this, std::ref(msg), callback));
        coro_recv_();
    });
}

void MessageSocket::async_send(Message& msg, CallBack callback) {
    send_buffer_.reset();
    send_buffer_.prepare(msg.length());
    memcpy(send_buffer_.write_head(), msg.data(), msg.length());
    send_buffer_.accept(msg.length());
    async_send_all(send_buffer_, [this, callback](const axon::util::ErrorCode& ec, size_t bt) {
        if (ec != ErrorCode::success) {
            io_service_->post(std::bind(callback, MessageResult::SOCKET_FAIL));
        } else {
            io_service_->post(std::bind(callback, MessageResult::SUCCESS));
        }
    });
}

void MessageSocket::async_recv_impl(Message& msg, CallBack callback) {
    NonfreeSequenceBuffer<char> buffer;

    // read header
    buffer.prepare(sizeof(Message::MessageHeader));
    axon::util::ErrorCode header_ec;
    async_recv_all(buffer, [this, &header_ec](const axon::util::ErrorCode& ec, size_t bt) {
        header_ec = ec;
        coro_recv_();
    });
    coro_recv_.yield();
    if (header_ec != ErrorCode::success) {
        io_service_->post(std::bind(callback, MessageResult::SOCKET_FAIL));
        return;
    }

    // check header
    Message::MessageHeader* header = reinterpret_cast<Message::MessageHeader*>(buffer.read_head());
    if (memcmp(Message::AXON_MESSAGE_SIGNATURE, header->signature, sizeof(Message::AXON_MESSAGE_SIGNATURE)) != 0) {
        io_service_->post(std::bind(callback, MessageResult::INVALID_HEADER));
        return;
    }

    // read message body
    buffer.prepare(header->content_length);
    axon::util::ErrorCode body_ec;
    async_recv_all(buffer, [this, &body_ec](const axon::util::ErrorCode& ec, size_t bt) {
        body_ec = ec;
        coro_recv_();
    });
    coro_recv_.yield();
    if (body_ec != ErrorCode::success) {
        io_service_->post(std::bind(callback, MessageResult::SOCKET_FAIL));
    }

    msg.set_data(buffer.read_head(), buffer.read_size());
    // schedule callback
    io_service_->post(std::bind(callback, MessageResult::SUCCESS));
}







