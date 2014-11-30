#include "socket/consistent_socket.hpp"

using namespace axon::socket;
using namespace axon::util;
ConsistentSocket::ConsistentSocket(axon::service::IOService * service):
    io_service_(service),
    base_socket_(service),
    reconnect_timer_(service),
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

    // invoke connection
    io_service_->post([this]() {
        axon::util::ScopedLock lock(&mutex_);
        connect_coro_();
    });
}


void ConsistentSocket::connect_loop() {
    Ptr _ref __attribute__((unused)) = shared_from_this();
    while (should_connect_) {
        if (status_ & SOCKET_DOWN) break;
        status_ |= SOCKET_CONNECTING;
        status_ &= ~SOCKET_READY;
        // do connection
        ErrorCode connect_ec = 0;
        base_socket_.async_connect(addr_, port_, [&connect_ec, this](const ErrorCode& ec, size_t bt) {
            axon::util::ScopedLock lock(&mutex_);
            connect_ec = ec;
            connect_coro_();
        });
        connect_coro_.yield();
        
        if (status_ & SOCKET_DOWN) break;
        if (connect_ec != ErrorCode::success) {
            printf("connection failed: code %d\n", connect_ec.code());
            if (should_connect_) {
                printf("reconnect in 1000 ms\n");
                reconnect_timer_.expires_from_now(1000);
                reconnect_timer_.async_wait([this](const ErrorCode& ec) {
                    axon::util::ScopedLock lock(&mutex_);
                    connect_coro_(); 
                });
                connect_coro_.yield();
                // start next connect
                continue;
            } else {
                status_ &= ~SOCKET_CONNECTING;
                return;
            }
        }

        // Connection Succeeded
        status_ |= SOCKET_READY;
        status_ &= ~SOCKET_CONNECTING;


        printf("connected to %s:%d\n", addr_.c_str(), port_);
        io_service_->post(wrap(read_coro_));
        io_service_->post(wrap(write_coro_));

        connect_coro_.yield();
    }
    printf("connect loop exiting\n");
}

void ConsistentSocket::read_loop() {
    Ptr _ref __attribute__((unused)) = shared_from_this();
    while (!(status_ & SOCKET_DOWN)) {
        if (!(status_ & SOCKET_READY) || read_queue_.empty()) {
            status_ &= ~SOCKET_READING;
            read_coro_.yield();
        }
        status_ |= SOCKET_READING;

        Operation& op = read_queue_.front();
        MessageSocket::MessageResult recv_result;
        base_socket_.async_recv(op.message, [this, &recv_result](const MessageSocket::MessageResult& mr) {
            axon::util::ScopedLock lock(&mutex_);
            recv_result = mr;
            read_coro_();
        });
        read_coro_.yield();

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
    Ptr _ref __attribute__((unused)) = shared_from_this();
    printf("write loop exiting\n");

}

void ConsistentSocket::init_coros() {
    connect_coro_.set_function(std::bind(&ConsistentSocket::connect_loop, this));
    read_coro_.set_function(std::bind(&ConsistentSocket::read_loop, this));
    write_coro_.set_function(std::bind(&ConsistentSocket::write_loop, this));
}


void ConsistentSocket::async_recv(axon::socket::Message& msg, CallBack callback) {
    io_service_->post([this, &msg, callback](){
        axon::util::ScopedLock lock(&mutex_);
        printf("adding\n");
        if (status_ & SOCKET_DOWN) {
            callback(SocketResult::DOWN);
        } else if (queue_full(read_queue_)) {
            callback(SocketResult::BUFFER_FULL);
        } else {
            read_queue_.push(Operation(msg, callback));
            if (!(status_ & SOCKET_READING) && (status_ & SOCKET_READY)) {
                read_coro_();
            }
        }
    });
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
    if (!(status_ & SOCKET_CONNECTING)) {
        connect_coro_();
    }
    if (!(status_ & SOCKET_READING)) {
        read_coro_();
    }
}
