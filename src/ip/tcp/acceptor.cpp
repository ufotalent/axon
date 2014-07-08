#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include "ip/tcp/acceptor.hpp"
#include "event/accept_event.hpp"
#include "service/io_service.hpp"

using namespace axon::ip::tcp;
using namespace axon::event;
using namespace axon::service;

Acceptor::Acceptor(axon::service::IOService* io_service): io_service_(io_service), ev_service_(&EventService::get_instance()) {
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    fd_ev_.reset(new EventService::fd_event(fd_, io_service_));
    ev_service_->register_fd(fd_, fd_ev_);
}

Acceptor::~Acceptor() {
    ev_service_->unregister_fd(fd_, fd_ev_);
    close(fd_);
    fd_ev_.reset();
    io_service_ = NULL;
    ev_service_ = NULL;
    fd_ = -1;
}

void Acceptor::bind(std::string addr, uint32_t port) {
    memset(&addr_, 0, sizeof(addr_));
    addr_.sin_family = AF_INET;
    addr_.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr_.sin_addr);

    int ret = ::bind(fd_, (sockaddr*)&addr_, sizeof(addr_));
    if (ret < 0) {
        perror("bind failed");
        throw std::runtime_error("socket bind failed");
    }
}

void Acceptor::listen() {
    if (::listen(fd_, 128) < 0) {
        perror("listen failed");
        throw std::runtime_error("socket listen failed");
    }
}

void Acceptor::accept(Socket& sock) {

    sockaddr_in peer;
    socklen_t peer_len = sizeof(peer);
    int rfd = ::accept(fd_, (sockaddr*)&peer, &peer_len);
    if (rfd < 0) {
        perror("accept failed");
        throw std::runtime_error("accept failed");
    }
    sock.assign(rfd);
}

void Acceptor::async_accept(Socket &sock, CallBack callback) {
    typename axon::event::AcceptEvent<Socket>::Ptr ev(new axon::event::AcceptEvent<Socket>(
            fd_,
            sock,
            callback));
    ev_service_->start_event(ev, fd_ev_);
}
