#pragma once
#include <memory>
#include <unordered_map>
#include "service/io_service.hpp"
#include "socket/consistent_socket.hpp"
#include "rpc/session.hpp"

namespace axon {
namespace rpc {
class BaseRPCClient : public std::enable_shared_from_this<BaseRPCClient> {
protected:
    BaseRPCClient(axon::service::IOService* service, const std::string& addr, uint32_t port);
public:
    typedef std::shared_ptr<BaseRPCClient> Ptr;
    template <class T>
    static Ptr create(axon::service::IOService* service, const std::string& addr, uint32_t port) {
        return Ptr(new T(service, addr, port));
    }

    struct ClientResult {
        enum client_result_t {
            SUCCESS = 0,
            CANCELED = 1,
            BUFFER_FULL = 2,
            DOWN = 3,
            TIMEOUT = 4,
            UNKNOWN = 5
        };
        client_result_t result_;
        ClientResult(): result_(UNKNOWN) {}
        ClientResult(int result): result_(static_cast<client_result_t>(result)) {}
        operator int() const { return result_; }
    };
    void async_request(Context::Ptr context, std::function<void(const ClientResult&)> callback, const double timeout = 20);
    void shutdown();
private:
    void request_sent_callback(uint32_t req_no, const axon::socket::ConsistentSocket::SocketResult&);
    void timer_callback(const axon::util::ErrorCode& ec);
    struct RequestOperation {
        Context::Ptr context;
        uint32_t deadline;
        std::function<void(const ClientResult&)> callback;
    };
    void recv_loop();
    void post_result(uint32_t req_no, const axon::socket::ConsistentSocket::SocketResult&);
    axon::socket::ConsistentSocket::Ptr socket_;
    axon::util::Timer timer_;

    axon::util::Coroutine recv_coro_;
    axon::util::Strand::Ptr strand_;
    bool coro_started_;
    std::unordered_map<uint32_t, RequestOperation> request_map_;
    uint32_t request_no_;

    bool shutdown_;

    axon::service::IOService* io_service_;

};
}
}
