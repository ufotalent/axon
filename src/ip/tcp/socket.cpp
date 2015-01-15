#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include "ip/tcp/socket.hpp"
#include "service/io_service.hpp"
#include "event/recv_event.hpp"
#include "event/send_event.hpp"
#include "event/connect_event.hpp"
#include "util/util.hpp"

using namespace axon::event;
using namespace axon::service;
using namespace axon::ip::tcp;


Socket::Socket(axon::service::IOService* io_service): fd_(-1), io_service_(io_service), ev_service_(&EventService::get_instance()), fd_ev_(NULL) {
    is_down_.store(true);
}

int Socket::do_connect(const std::string& remote_addr, uint32_t port) {
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, remote_addr.c_str(), &addr.sin_addr) != 1)
        throw std::runtime_error("invalid address");

    int ret = ::connect(fd_, (sockaddr*)&addr, sizeof(addr));
    if (ret < 0)
        return errno;
    else
        return 0;
}

void Socket::connect(std::string remote_addr, uint32_t port) {
    // create a new fd
    shutdown();
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    fd_ev_.reset(new EventService::fd_event(fd_, io_service_));
    ev_service_->register_fd(fd_, fd_ev_);
    is_down_.store(false);

    int ret = do_connect(remote_addr, port);
    // do connect
    if (ret > 0) {
        perror("connect failed");
        throw std::runtime_error("socket connect failed");
    }
}

void Socket::async_connect(std::string remote_addr, uint32_t port, CallBack callback, axon::util::Strand::Ptr callback_strand) {
    // create a new NON_BLOCKING fd 
    shutdown();
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    int flags = fcntl(fd_, F_GETFL);
    if (flags < 0)
        throw std::runtime_error("GETFL failed");
    ENSURE_RETURN_ZERO(fcntl(fd_, F_SETFL, flags | O_NONBLOCK));
    fd_ev_.reset(new EventService::fd_event(fd_, io_service_));
    ev_service_->register_fd(fd_, fd_ev_);
    is_down_.store(false);

    int code = do_connect(remote_addr, port);
    if (code != EINPROGRESS) {
        throw std::runtime_error("failed to initiate connection");
    }

    axon::event::ConnectEvent::Ptr ev(new axon::event::ConnectEvent(fd_, callback));
    ev->set_callback_strand(callback_strand);
    ev_service_->start_event(ev, fd_ev_);
}
void Socket::assign(int fd) {
    shutdown();
    fd_ = fd;
    fd_ev_.reset(new EventService::fd_event(fd_, io_service_));
    ev_service_->register_fd(fd_, fd_ev_);
    is_down_.store(false);
    LOG_INFO("assigning base socket %p %d", this, is_down_.load());
}

void Socket::shutdown() {
    bool expected = false;
    LOG_INFO("socket %p shutting down, now is_down_ = %d", this, is_down_.load());
    if (!is_down_.compare_exchange_strong(expected, true)) {
        LOG_INFO("not really shutting down");
        return;
    }
    is_down_.store(true);
    EventService::get_instance().unregister_fd(fd_, fd_ev_);
    close(fd_);
    fd_ev_.reset();
    fd_ = -1;
}

Socket::~Socket() {
    shutdown();
    io_service_ = NULL;
    ev_service_ = NULL;
}

int Socket::get_fd() const {
    return fd_;
}
/*
void Socket::async_recv(Buffer& buf,  CallBack callback) {
    buf.prepare(512);
    axon::event::RecvEvent<Buffer>::Ptr ev(new axon::event::RecvEvent<Buffer>(
            fd_, 
            axon::event::Event::EVENT_TYPE_READ,
            buf,
            callback));
    ev_service_->start_event(ev, fd_ev_);
}

void Socket::async_send(Buffer& buf,  CallBack callback) {
    //512 => Magic Number?? related to system buffer
    buf.prepare(512);
    axon::event::SendEvent<Buffer>::Ptr ev(new axon::event::SendEvent<Buffer>(
            fd_, 
            axon::event::Event::EVENT_TYPE_WRITE,
            buf,
            callback));
    ev_service_->start_event(ev, fd_ev_);
}
*/
