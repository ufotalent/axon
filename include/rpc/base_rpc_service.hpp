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
class BaseRPCService: public std::enable_shared_from_this<BaseRPCService> {
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

protected:
    BaseRPCService(axon::service::IOService* service, const std::string& addr, uint32_t port);
public:
    typedef std::shared_ptr<BaseRPCService> Ptr;
    template <class T>
    static Ptr create(axon::service::IOService* service, const std::string& addr, uint32_t port) {
        return Ptr(new T(service, addr, port));
    }
    virtual ~BaseRPCService();
    virtual void dispatch_request(Session::Ptr session, Context::Ptr context);
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
