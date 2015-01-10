#include "socket/consistent_socket.hpp"
#include <cassert>
#include <cstring>
#include "buffer/nonfree_sequence_buffer.hpp"
#include "util/log.hpp"

using namespace axon::socket;
using namespace axon::util;
using namespace axon::buffer;
ConsistentSocket::ConsistentSocket(axon::service::IOService * service):
    io_service_(service),
    base_socket_(service),
    reconnect_timer_(service),
    wait_timer_(service),
    should_connect_(false),
    status_(0) {

    port_ = 0;
    init_coros();
    pthread_mutex_init(&mutex_, NULL);
}

ConsistentSocket::ConsistentSocket(axon::service::IOService * service, const std::string& addr, uint32_t port): ConsistentSocket(service) {

    addr_ = addr;
    port_ = port;
    should_connect_ = true;
}

void ConsistentSocket::start_connecting() {
    axon::util::ScopedLock lock(&mutex_);
    if (!(status_ & SOCKET_CONNECTING)) {
        connect_coro_();
    }
}


void ConsistentSocket::connect_loop() {
    while (should_connect_) {
        if (status_ & SOCKET_DOWN) { 
            status_ &= ~SOCKET_CONNECTING;
            break;
        }
        status_ |= SOCKET_CONNECTING;
        status_ &= ~SOCKET_READY;
        // do connection
        ErrorCode connect_ec = -1;
        base_socket_.async_connect(addr_, port_, safe_callback([&connect_ec, this](const ErrorCode& ec, size_t bt) {
            connect_ec = ec;
            connect_coro_();
        }));
        connect_coro_.yield();
        
        assert(connect_ec != -1);
        if (status_ & SOCKET_DOWN) { 
            status_ &= ~SOCKET_CONNECTING;
            break;
        }
        if (connect_ec != ErrorCode::success) {
            LOG_INFO("connection failed: code %s", connect_ec.str());
            if (should_connect_) {
                LOG_INFO("reconnect in 1000 ms");
                reconnect_timer_.expires_from_now(1000);
                Ptr ptr = shared_from_this();
                reconnect_timer_.async_wait([this, ptr](const ErrorCode& ec) {
                    Ptr _ref __attribute__((unused)) = ptr;
                    axon::util::ScopedLock lock(&mutex_);
                    connect_coro_(); 
                });
                ptr.reset();
                connect_coro_.yield();
                // start next connect
                continue;
            } else {
                status_ &= ~SOCKET_CONNECTING;
                return;
            }
        }

        // by this time the connection is done, however operation callbacks (may be cancelled), which continues coros,  may still be on fly, we must wait until read/write operation finish.
        while ((status_ & SOCKET_WRITING) || (status_ & SOCKET_READING)) {
            LOG_INFO("still writting/reading, wait");
            wait_timer_.expires_from_now(10);
            axon::util::ErrorCode wait_ec = -1;
            Ptr ptr = shared_from_this();
            wait_timer_.async_wait([this, &wait_ec, ptr](const axon::util::ErrorCode& ec) {
                Ptr _ref __attribute__((unused)) = ptr;
                axon::util::ScopedLock lock(&mutex_);
                wait_ec = ec;
                connect_coro_();
            });
            ptr.reset();
            connect_coro_.yield();
            assert(wait_ec != -1);
            if (status_ & SOCKET_DOWN) {
                status_ &= ~SOCKET_CONNECTING;
                return;
            }
        }
        // Connection Succeeded
        status_ |= SOCKET_READY;
        status_ &= ~SOCKET_CONNECTING;


        LOG_INFO("connected to %s:%d", addr_.c_str(), port_);
        io_service_->post(wrap(read_coro_, SOCKET_READING | SOCKET_DOWN));
        io_service_->post(wrap(write_coro_, SOCKET_WRITING | SOCKET_DOWN));

        connect_coro_.yield();
    }
    LOG_DEBUG("connect loop exiting");
}

void ConsistentSocket::read_loop() {
    while (!(status_ & SOCKET_DOWN)) {
        if (!(status_ & SOCKET_READY) || read_queue_.empty()) {
            status_ &= ~SOCKET_READING;
            read_coro_.yield();
            continue;
        }
        status_ |= SOCKET_READING;

        ReadOperation& op = read_queue_.front();
        Message& message = op.message;
        NonfreeSequenceBuffer<char> buffer;

        buffer.prepare(sizeof(Message::MessageHeader));
        axon::util::ErrorCode header_ec = -1;
        // base_socket_.async_recv_all(buffer, safe_callback([this, &header_ec](const ErrorCode& ec, size_t bt) {
        //    header_ec = ec;
        //    read_coro_();
        //}));
        
        base_socket_.async_recv_all(buffer, std::bind(&ConsistentSocket::safe_callback_quick, this, shared_from_this(), &read_coro_, std::ref(header_ec), std::placeholders::_1, std::placeholders::_2));
        read_coro_.yield();

        if (header_ec.code() == -1) {
            LOG_FATAL("corrupted read loop");
        }
        assert(header_ec.code() != -1);
        // by this time the socket may be reconnecting or shutdown
        if (!(status_ & SOCKET_READY) || header_ec != ErrorCode::success) {
            LOG_INFO("failed to get message header");
            handle_error();
            continue;
        }
        // check header valid
        Message::MessageHeader* header = reinterpret_cast<Message::MessageHeader*>(buffer.read_head());
        if (buffer.read_size() != sizeof(Message::MessageHeader) ||
            memcmp(Message::AXON_MESSAGE_SIGNATURE, header->signature, sizeof(Message::AXON_MESSAGE_SIGNATURE)) != 0) {
            LOG_INFO("invalid message header");
            handle_error();
            continue;
        }
        // read message body
        buffer.prepare(header->content_length);
        axon::util::ErrorCode body_ec = -1;
        base_socket_.async_recv_all(buffer, std::bind(&ConsistentSocket::safe_callback_quick, this, shared_from_this(), &read_coro_, std::ref(body_ec), std::placeholders::_1, std::placeholders::_2));
        /*
        base_socket_.async_recv_all(buffer, safe_callback([this, &body_ec](const ErrorCode& ec, size_t bt) {
            body_ec = ec;
            read_coro_();
        }));
        */
        read_coro_.yield();
        if (body_ec.code() == -1) {
            LOG_FATAL("corrupted read loop");
        }
        assert(body_ec.code() != -1);
        if (!(status_ & SOCKET_READY) || body_ec != ErrorCode::success) {
            LOG_INFO("failed to get message body");
            handle_error();
            continue;
        }

        message.set_data(buffer.read_head(), buffer.read_size());
        // read success
        io_service_->post(std::bind(op.callback, SocketResult::SUCCESS));
        read_queue_.pop();
    }
    LOG_DEBUG("read loop exiting");
}

void ConsistentSocket::write_loop() {
    while (!(status_ & SOCKET_DOWN)) {
        if (!(status_ & SOCKET_READY) || write_queue_.empty()) {
            status_ &= ~SOCKET_WRITING;
            write_coro_.yield();
            continue;
        }
        status_ |= SOCKET_WRITING;

        // prepare data
        WriteOperation& op = write_queue_.front();
        Message& message = op.message;
        send_buffer_.reset();
        send_buffer_.prepare(message.length());
        memcpy(send_buffer_.write_head(), message.data(), message.length());
        send_buffer_.accept(message.length());

        ErrorCode send_ec = -1;
        base_socket_.async_send_all(send_buffer_, std::bind(&ConsistentSocket::safe_callback_quick, this, shared_from_this(), &write_coro_, std::ref(send_ec), std::placeholders::_1, std::placeholders::_2));
        /*
        base_socket_.async_send_all(send_buffer_, safe_callback([this, &send_ec](const ErrorCode& ec, size_t bt) {
            send_ec = ec;
            write_coro_();
        }));
        */
        write_coro_.yield();

        assert(send_ec != -1);
        // by this time the socket may be reconnecting or shutdown
        if (!(status_ & SOCKET_READY) || send_ec!= ErrorCode::success) {
            LOG_INFO("send failed");
            handle_error();
            continue;
        }

        // write success
        io_service_->post(std::bind(op.callback, SocketResult::SUCCESS));
        write_queue_.pop();
    }
    LOG_DEBUG("write loop exiting");
}

void ConsistentSocket::init_coros() {
    connect_coro_.set_function(std::bind(&ConsistentSocket::connect_loop, this));
    read_coro_.set_function(std::bind(&ConsistentSocket::read_loop, this));
    write_coro_.set_function(std::bind(&ConsistentSocket::write_loop, this));
}


void ConsistentSocket::async_recv(axon::socket::Message& msg, CallBack callback) {
    axon::util::ScopedLock lock(&mutex_);
    if (status_ & SOCKET_DOWN) {
        io_service_->post(std::bind(callback,SocketResult::DOWN));
    } else if (queue_full(read_queue_)) {
        io_service_->post(std::bind(callback,SocketResult::BUFFER_FULL));
    } else {
        read_queue_.push(ReadOperation(msg, callback));
        if (!(status_ & SOCKET_READING) && (status_ & SOCKET_READY)) {
            read_coro_();
        }
    }
}

void ConsistentSocket::async_send(axon::socket::Message& msg, CallBack callback) {
    axon::util::ScopedLock lock(&mutex_);
    if (status_ & SOCKET_DOWN) {
        io_service_->post(std::bind(callback,SocketResult::DOWN));
    } else if (queue_full(write_queue_)) {
        io_service_->post(std::bind(callback,SocketResult::BUFFER_FULL));
    } else {
        write_queue_.push(WriteOperation(msg, callback));
        if (!(status_ & SOCKET_WRITING) && (status_ & SOCKET_READY)) {
            write_coro_();
        }
    }
}

void ConsistentSocket::shutdown() {
    axon::util::ScopedLock lock(&mutex_);
    // already shutdown
    if (status_ & SOCKET_DOWN) {
        return;
    }
    should_connect_ = false;
    status_ |= SOCKET_DOWN;
    status_ &= ~SOCKET_READY;
    // cancel all callbacks
    while (!read_queue_.empty()) {
        io_service_->post(std::bind(read_queue_.front().callback, SocketResult::CANCELED));
        read_queue_.pop();
    }
    while (!write_queue_.empty()) {
        io_service_->post(std::bind(write_queue_.front().callback, SocketResult::CANCELED));
        write_queue_.pop();
    }
    LOG_DEBUG("Consistent socket shutting down with status %d", int(status_));
    base_socket_.shutdown();
    if (!(status_ & SOCKET_CONNECTING)) {
        connect_coro_();
    }
    if (!(status_ & SOCKET_READING)) {
        read_coro_();
    }
    if (!(status_ & SOCKET_WRITING)) {
        write_coro_();
    }
}

ConsistentSocket::~ConsistentSocket() {
    pthread_mutex_destroy(&mutex_);
}
