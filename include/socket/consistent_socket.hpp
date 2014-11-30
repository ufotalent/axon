#pragma once
#include <string>
#include <memory>
#include <queue>
#include "service/io_service.hpp"
#include "socket/message_socket.hpp"
#include "util/coroutine.hpp"
#include "util/timer.hpp"

namespace axon {
namespace socket {

class ConsistentSocket: public std::enable_shared_from_this<ConsistentSocket> {
private:
    ConsistentSocket(axon::service::IOService* service);
    ConsistentSocket(axon::service::IOService* service, const std::string& addr, uint32_t port);
public:
    struct SocketResult {
        enum socket_result_t {
            SUCCESS = 0,
            CANCELED = 1,
            BUFFER_FULL = 2,
            DOWN = 3
        };
        socket_result_t result_;
        SocketResult(int result): result_(static_cast<socket_result_t>(result)) {}
        operator int() const { return result_; }
    };
    typedef std::function<void(const SocketResult&)> CallBack;
    typedef axon::socket::MessageSocket BaseSocket;
    typedef std::shared_ptr<ConsistentSocket> Ptr;
    void async_recv(axon::socket::Message& message, CallBack callback);
    void shutdown();
    enum SocketStatus {
        SOCKET_CONNECTING = 1,
        SOCKET_READY = 2,
        SOCKET_WRITING = 4,
        SOCKET_READING = 8,
        SOCKET_DOWN = 16
    };
    static Ptr create(axon::service::IOService* service) {
        return Ptr(new ConsistentSocket(service));
    }
    static Ptr create(axon::service::IOService* service, const std::string& addr, uint32_t port) {
        return Ptr(new ConsistentSocket(service, addr, port));
    }
protected:

    axon::service::IOService* io_service_;
    BaseSocket base_socket_;
    axon::util::Timer reconnect_timer_;
    std::string addr_;
    uint32_t port_;
    bool should_connect_;
    uint32_t status_;
    pthread_mutex_t mutex_;

    struct Operation {
        Message& message;
        CallBack callback;
        Operation(Message& message, CallBack callback):message(message), callback(callback) { }
    };
    std::queue<Operation> read_queue_;
    std::queue<Operation> write_queue_;
    bool queue_full(const std::queue<Operation>& q) { return q.size() >= 1000; }
private:
    void connect_loop();
    void read_loop();
    void write_loop();
    axon::util::Coroutine connect_coro_, read_coro_, write_coro_;

    std::function<void()> wrap(axon::util::Coroutine& coro) {
        return [&coro, this]() {
            axon::util::ScopedLock lock(&this->mutex_);
            coro();
        };
    }
    void init_coros();
};

}
}
