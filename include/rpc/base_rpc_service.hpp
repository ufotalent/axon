#pragma once
#include <pthread.h>
#include <string>
#include <unordered_set>
#include "service/io_service.hpp"
#include "ip/tcp/acceptor.hpp"
#include "util/coroutine.hpp"
#include "rpc/session.hpp"
#include "socket/message.hpp"

namespace axon {
namespace rpc {
class BaseRPCService {
private:
    axon::service::IOService *io_service_;
    axon::ip::tcp::Acceptor acceptor_;
    axon::util::Coroutine accept_coro_;
    std::string addr_;
    pthread_mutex_t mutex_;
    std::unordered_set<axon::rpc::Session::Ptr> session_set_;

    uint32_t port_;
    bool shutdown_;
    void event_loop();
public:
    BaseRPCService(axon::service::IOService* service, const std::string& addr, uint32_t port);
    virtual ~BaseRPCService();
    virtual void dispatch_request(const axon::socket::Message& message, Session::Ptr session);
    void remove_session(Session::Ptr session);
    void bind_and_listen();
    void shutdown();
    void lock_continue() {
        axon::util::ScopedLock lock(&mutex_);
        accept_coro_();
    }
};
}
}
