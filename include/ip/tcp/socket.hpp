#pragma once

#include "service/io_service.hpp"
#include "event/event_service.hpp"
#include "util/buffer.hpp"

namespace axon {
namespace ip {
namespace tcp {
class Socket {
public:
    typedef std::function<void(const axon::util::ErrorCode&, size_t)> CallBack;
    typedef axon::util::SequenceBuffer<char> Buffer;
    Socket(axon::service::IOService* io_service);
    ~Socket();
    void async_recv(Buffer& buf, CallBack callback);
    void async_send(Buffer& buf, CallBack callback);

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
