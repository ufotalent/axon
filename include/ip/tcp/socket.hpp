#pragma once

#include "service/io_service.hpp"
#include "event/event_service.hpp"
#include "util/buffer.hpp"
#include "event/recv_event.hpp"
#include "event/recv_until_event.hpp"
#include "event/send_event.hpp"

namespace axon {
namespace ip {
namespace tcp {
class Socket {
public:
    typedef std::function<void(const axon::util::ErrorCode&, size_t)> CallBack;
    Socket(axon::service::IOService* io_service);
    ~Socket();

    template <class Buffer>
    void async_recv(Buffer& buf, CallBack callback) {
        buf.prepare(512);
        typename axon::event::RecvEvent<Buffer>::Ptr ev(new axon::event::RecvEvent<Buffer>(
                fd_, 
                axon::event::Event::EVENT_TYPE_READ,
                buf,
                callback));
        ev_service_->start_event(ev, fd_ev_);
    }

    template <class Buffer, class CompletionCondition>
    void async_recv_until(Buffer& buf, CallBack callback, CompletionCondition& condition) {
        buf.prepare(512);
        typename axon::event::RecvUntilEvent<Buffer, CompletionCondition>::Ptr ev(new axon::event::RecvUntilEvent<Buffer, CompletionCondition>(
                fd_, 
                axon::event::Event::EVENT_TYPE_READ,
                buf,
                callback,
                condition));
        ev_service_->start_event(ev, fd_ev_);
    }

    template <class Buffer>
    void async_send(Buffer& buf, CallBack callback) {
        buf.prepare(512);
        typename axon::event::SendEvent<Buffer>::Ptr ev(new axon::event::SendEvent<Buffer>(
                fd_, 
                axon::event::Event::EVENT_TYPE_WRITE,
                buf,
                callback));
        ev_service_->start_event(ev, fd_ev_);
    }

    void assign(int fd);
    void shutdown();

private:
    int fd_;
    axon::service::IOService* io_service_;
    axon::event::EventService* ev_service_;
    axon::event::EventService::fd_event::Ptr fd_ev_;
};

}
}
}
