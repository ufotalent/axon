#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include "ip/tcp/socket.hpp"
#include "service/io_service.hpp"
#include "event/recv_event.hpp"
#include "event/send_event.hpp"

using namespace axon::event;
using namespace axon::service;
using namespace axon::ip::tcp;


Socket::Socket(axon::service::IOService* io_service): fd_(-1), io_service_(io_service), ev_service_(&EventService::get_instance()), fd_ev_(NULL) {

}

void Socket::connect(std::string remote_addr, uint32_t port) {
    // create a new fd
    shutdown();
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    fd_ev_.reset(new EventService::fd_event(fd_, io_service_));
    ev_service_->register_fd(fd_, fd_ev_);

    // do connect
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, remote_addr.c_str(), &addr.sin_addr);

    int ret = ::connect(fd_, (sockaddr*)&addr, sizeof(addr));
    if (ret < 0) {
        perror("connect failed");
        throw std::runtime_error("socket connect failed");
    }
}

void Socket::assign(int fd) {
    shutdown();
    fd_ = fd;
    fd_ev_.reset(new EventService::fd_event(fd_, io_service_));
    ev_service_->register_fd(fd_, fd_ev_);
}

void Socket::shutdown() {
    if (!fd_ev_) {
        return;
    }
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
