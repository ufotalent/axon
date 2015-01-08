#pragma once
#include <memory>
#include "service/io_service.hpp"
#include "socket/consistent_socket.hpp"
#include "socket/message.hpp"

namespace axon {
namespace rpc {
class BaseRPCService;
class Session: public std::enable_shared_from_this<Session> {
public:
    Session(axon::service::IOService* service, BaseRPCService *rpc);
    virtual ~Session();
    typedef std::shared_ptr<Session> Ptr;
    void start_event_loop();
    void send_response(axon::socket::Message& message);
protected:
    void dispatch_request(const axon::socket::Message& message);
private:
    void shutdown();
    axon::socket::ConsistentSocket::Ptr socket_;
    axon::service::IOService *io_service_;
    BaseRPCService* rpc_service_;
    axon::util::Coroutine recv_coro_;
    pthread_mutex_t mutex_;
    bool shutdown_ = false;
    void event_loop();
    std::function<void(const axon::socket::ConsistentSocket::SocketResult&)> safe_callback(std::function<void(const axon::socket::ConsistentSocket::SocketResult&)> handler) {
        Ptr ptr = shared_from_this();
        return [this, ptr, handler](const axon::socket::ConsistentSocket::SocketResult& sr) {
            Ptr _ref __attribute__((unused)) = ptr;
            axon::util::ScopedLock lock(&mutex_);
            handler(sr);
        };
    }
    friend class axon::rpc::BaseRPCService;
};
}
}
