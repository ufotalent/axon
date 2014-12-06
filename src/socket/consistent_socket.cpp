#include "socket/consistent_socket.hpp"
#include <cassert>

using namespace axon::socket;
using namespace axon::util;
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

    printf("create %p\n", this);
    // invoke connection
    io_service_->post([this]() {
    });
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
        printf("%p make share 2\n", this);
        Ptr ptr = shared_from_this();
        base_socket_.async_connect(addr_, port_, [&connect_ec, ptr, this](const ErrorCode& ec, size_t bt) {
            Ptr _ref __attribute__((unused)) = ptr;
            printf("async connect callback\n");
            axon::util::ScopedLock lock(&mutex_);
            connect_ec = ec;
            connect_coro_();
        });
        ptr.reset();
        connect_coro_.yield();
        
        assert(connect_ec != -1);
        if (status_ & SOCKET_DOWN) { 
            status_ &= ~SOCKET_CONNECTING;
            break;
        }
        if (connect_ec != ErrorCode::success) {
            printf("connection failed: code %d\n", connect_ec.code());
            if (should_connect_) {
                printf("reconnect in 1000 ms\n");
                reconnect_timer_.expires_from_now(1000);
                printf("make share 3\n");
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
            printf("still writting/reading, wait\n");
            wait_timer_.expires_from_now(10);
            axon::util::ErrorCode wait_ec = -1;
            printf("make share 4\n");
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


        printf("connected to %s:%d\n", addr_.c_str(), port_);
        io_service_->post(wrap(read_coro_, SOCKET_READING | SOCKET_DOWN));
        io_service_->post(wrap(write_coro_, SOCKET_WRITING | SOCKET_DOWN));

        connect_coro_.yield();
    }
    printf("connect loop exiting\n");
}

void ConsistentSocket::read_loop() {
    while (!(status_ & SOCKET_DOWN)) {
        if (!(status_ & SOCKET_READY) || read_queue_.empty()) {
            status_ &= ~SOCKET_READING;
            read_coro_.yield();
            continue;
        }
        status_ |= SOCKET_READING;
        printf("entering read loop\n");

        Operation& op = read_queue_.front();
        MessageSocket::MessageResult recv_result = MessageSocket::MessageResult::UNKNOWN;
        printf("make share 5\n");
        Ptr ptr = shared_from_this();
        base_socket_.async_recv(op.message, [this, ptr, &recv_result](const MessageSocket::MessageResult& mr) {
            Ptr _ref __attribute__((unused)) = ptr;
            axon::util::ScopedLock lock(&mutex_);
            recv_result = mr;
            printf("enter by callback\n");
            read_coro_();
        });
        ptr.reset();
        read_coro_.yield();

        if (recv_result == MessageSocket::MessageResult::UNKNOWN) {
            printf("ERROR: corrupted read loop\n");
        }
        assert(int(recv_result) != MessageSocket::MessageResult::UNKNOWN);
        // by this time the socket may be reconnecting or shutdown
        if (!(status_ & SOCKET_READY) || recv_result != MessageSocket::MessageResult::SUCCESS) {
            status_ &= ~SOCKET_READY;
            // read failed, initiate connection
            if (should_connect_ && !(status_ & SOCKET_CONNECTING)) {
                printf("read failed, do reconnection\n");
                connect_coro_();
            }
            continue;
        }

        // read success
        io_service_->post(std::bind(op.callback, SocketResult::SUCCESS));
        read_queue_.pop();
    }
    printf("read loop exiting\n");
}

void ConsistentSocket::write_loop() {
    while (!(status_ & SOCKET_DOWN)) {
        if (!(status_ & SOCKET_READY) || write_queue_.empty()) {
            status_ &= ~SOCKET_WRITING;
            write_coro_.yield();
            continue;
        }
        printf("write_queue_ cnt %lu\n", write_queue_.size());
        status_ |= SOCKET_WRITING;

        Operation& op = write_queue_.front();
        MessageSocket::MessageResult send_result = MessageSocket::MessageResult::UNKNOWN;
        printf("make share 1\n");
        Ptr ptr = shared_from_this();
        base_socket_.async_send(op.message, [this, ptr, &send_result](const MessageSocket::MessageResult& mr) {
            Ptr _ref __attribute__((unused)) = ptr;
            axon::util::ScopedLock lock(&mutex_);
            send_result = mr;
            write_coro_();
        });
        ptr.reset();
        write_coro_.yield();

        assert(int(send_result) != MessageSocket::MessageResult::UNKNOWN);
        // by this time the socket may be reconnecting or shutdown
        if (!(status_ & SOCKET_READY) || send_result != MessageSocket::MessageResult::SUCCESS) {
            status_ &= ~SOCKET_READY;
            // write failed, initiate connection
            if (should_connect_ && !(status_ & SOCKET_CONNECTING)) {
                printf("send failed, do reconnection\n");
                connect_coro_();
            }
            continue;
        }

        // write success
        io_service_->post(std::bind(op.callback, SocketResult::SUCCESS));
        write_queue_.pop();
    }
    printf("write loop exiting\n");
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
        read_queue_.push(Operation(msg, callback));
        if (!(status_ & SOCKET_READING) && (status_ & SOCKET_READY)) {
            printf("enter by operation\n");
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
        write_queue_.push(Operation(msg, callback));
        if (!(status_ & SOCKET_WRITING) && (status_ & SOCKET_READY)) {
            write_coro_();
        }
    }
}

void ConsistentSocket::shutdown() {
    axon::util::ScopedLock lock(&mutex_);
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
    printf("%d\n", int(status_));
    base_socket_.shutdown();
    printf("terminate connect \n");
    if (!(status_ & SOCKET_CONNECTING)) {
        connect_coro_();
    }
    printf("terminate read\n");
    if (!(status_ & SOCKET_READING)) {
        printf("enter by shutdown\n");
        read_coro_();
    }
    printf("terminate write\n");
    if (!(status_ & SOCKET_WRITING)) {
        write_coro_();
    }
}

ConsistentSocket::~ConsistentSocket() {
    printf("deleted\n");
    pthread_mutex_destroy(&mutex_);
}
