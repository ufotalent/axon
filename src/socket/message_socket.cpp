#include "socket/message_socket.hpp"
#include <cstring>
#include <cassert>
#include "buffer/nonfree_sequence_buffer.hpp"


using namespace axon::socket;
using namespace axon::service;
using namespace axon::buffer;
using namespace axon::util;



MessageSocket::MessageSocket(IOService *service):Socket(service) {
    pthread_mutex_init(&coro_exit_mutex_, NULL);
}

void MessageSocket::async_recv(Message& msg, CallBack callback) {
    axon::util::ScopedLock lock(&coro_exit_mutex_);
    recv_callback_ = callback;
    printf("start async_recv\n");
    coro_recv_.set_function(std::bind(&MessageSocket::async_recv_impl, this, std::ref(msg)));
    printf("do coro %p\n", &coro_recv_);
    coro_recv_();
    printf("coro done %p\n", &coro_recv_);
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

void MessageSocket::async_recv_impl(Message& msg) {
    printf("enter recv\n");
    NonfreeSequenceBuffer<char> buffer;

    // read header
    buffer.prepare(sizeof(Message::MessageHeader));
    axon::util::ErrorCode header_ec;
    printf("recv_all header\n");
    async_recv_all(buffer, [this, &header_ec](const axon::util::ErrorCode& ec, size_t bt) {
        axon::util::ScopedLock lock(&coro_exit_mutex_);
        header_ec = ec;
        coro_recv_();
    });
    coro_recv_.yield();
    printf("got header\n");
    if (header_ec != ErrorCode::success) {
        printf("get header failed\n");
        io_service_->post(std::bind(std::move(recv_callback_), MessageResult::SOCKET_FAIL));
        return;
    }

    // check header
    Message::MessageHeader* header = reinterpret_cast<Message::MessageHeader*>(buffer.read_head());
    if (memcmp(Message::AXON_MESSAGE_SIGNATURE, header->signature, sizeof(Message::AXON_MESSAGE_SIGNATURE)) != 0) {
        io_service_->post(std::bind(std::move(recv_callback_), MessageResult::INVALID_HEADER));
        return;
    }

    // read message body
    buffer.prepare(header->content_length);
    axon::util::ErrorCode body_ec;
    async_recv_all(buffer, [this, &body_ec](const axon::util::ErrorCode& ec, size_t bt) {
        axon::util::ScopedLock lock(&coro_exit_mutex_);
        body_ec = ec;
        coro_recv_();
    });
    coro_recv_.yield();
    printf("got body\n");
    if (body_ec != ErrorCode::success) {
        io_service_->post(std::bind(std::move(recv_callback_), MessageResult::SOCKET_FAIL));
    }

    msg.set_data(buffer.read_head(), buffer.read_size());
    // schedule callback
    io_service_->post(std::bind(std::move(recv_callback_), MessageResult::SUCCESS));
}

MessageSocket::~MessageSocket() {
    // wait for coroutine to exit
    axon::util::ScopedLock lock(&coro_exit_mutex_);
    pthread_mutex_destroy(&coro_exit_mutex_);
}



