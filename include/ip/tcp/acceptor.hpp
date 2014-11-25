#pragma once
#include <string>
#include <arpa/inet.h>
#include "socket.hpp"
#include "event/event_service.hpp"
#include "service/io_service.hpp"

namespace axon {
namespace ip {
namespace tcp {

class Acceptor: public axon::util::Noncopyable {
public:
    typedef std::function<void(const axon::util::ErrorCode&)> CallBack;
    void bind(std::string addr, uint32_t port);
    void listen();
    void accept(Socket &sock);

    void async_accept(Socket &sock, CallBack callback);
    Acceptor(axon::service::IOService* io_service);
    virtual ~Acceptor();
private:
    bool block_;
    int fd_;
    sockaddr_in addr_;

    axon::service::IOService* io_service_;
    axon::event::EventService* ev_service_;
    axon::event::EventService::fd_event::Ptr fd_ev_;
};

}
}
}
