#pragma once
#include <string>
#include <memory>
#include <queue>
#include "service/io_service.hpp"
#include "util/coroutine.hpp"
#include "util/timer.hpp"
#include "buffer/nonfree_sequence_buffer.hpp"
#include "ip/tcp/socket.hpp"
#include "socket/message.hpp"
#include "util/log.hpp"
#include "util/strand.hpp"

namespace axon {
namespace socket {

class ConsistentSocket: public std::enable_shared_from_this<ConsistentSocket> {
private:
    ConsistentSocket(axon::service::IOService* service);
    ConsistentSocket(axon::service::IOService* service, const std::string& addr, uint32_t port);
public:
    virtual ~ConsistentSocket();
    struct SocketResult {
        enum socket_result_t {
            SUCCESS = 0,
            CANCELED = 1,
            BUFFER_FULL = 2,
            DOWN = 3,
            UNKNOWN = 4
        };
        socket_result_t result_;
        SocketResult():SocketResult(UNKNOWN) {}
        SocketResult(int result): result_(static_cast<socket_result_t>(result)) {}
        operator int() const { return result_; }
    };
    typedef std::function<void(const SocketResult&)> CallBack;
    typedef axon::ip::tcp::Socket BaseSocket;
    typedef std::shared_ptr<ConsistentSocket> Ptr;
    void async_recv(axon::socket::Message& message, CallBack callback);
    void async_recv_impl(axon::socket::Message& message, CallBack callback);
    void async_send(axon::socket::Message message, CallBack callback);
    void async_send_impl(axon::socket::Message message, CallBack callback);
    void shutdown();
    void shutdown_impl();
    void start_connecting();

    // following two methods are used to set an accepted (not connecting to anyware) socket ready
    BaseSocket& base_socket() { return base_socket_; };
    void set_ready() { status_ |= SOCKET_READY; }

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
    axon::util::Timer reconnect_timer_, wait_timer_;
    axon::util::Strand::Ptr strand_;
    std::string addr_;
    uint32_t port_;
    bool should_connect_;
    uint32_t status_;
    axon::buffer::NonfreeSequenceBuffer<char> send_buffer_;

    struct ReadOperation {
        Message& message;
        CallBack callback;
        ReadOperation(Message& message, CallBack callback):message(message), callback(callback) { }
    };
    struct WriteOperation {
        Message message;
        CallBack callback;
        WriteOperation(Message& message, CallBack callback):message(std::move(message)), callback(callback) { }
    };
    std::queue<ReadOperation> read_queue_;
    std::queue<WriteOperation> write_queue_;
    template <typename T>
    bool queue_full(const T& q) { return q.size() >= 100000; }
private:
    void connect_loop();
    void read_loop();
    void write_loop();
    axon::util::Coroutine connect_coro_, read_coro_, write_coro_;

    std::function<void()> wrap(axon::util::Coroutine& coro, int flag) {
        Ptr ptr = shared_from_this();
        return [&coro, flag, ptr]() {
            if (!(ptr->status_ & flag)) {
                coro();
            }
        };
    }

    void safe_callback_quick(Ptr ptr, axon::util::Coroutine *coro, axon::util::ErrorCode& set_ec, const axon::util::ErrorCode& ec, size_t bt) {
        Ptr _ref __attribute__((unused)) = ptr;
        set_ec = ec;
        (*coro)();
    }

    std::function<void(const axon::util::ErrorCode&, size_t)> safe_callback(std::function<void(const axon::util::ErrorCode&, size_t)> handler) {
        Ptr ptr = shared_from_this();
        return [this, ptr, handler](const axon::util::ErrorCode& ec, size_t bt) {
            Ptr _ref __attribute__((unused)) = ptr;
            handler(ec, bt);
        };
    
    }

    void init_coros();
    void handle_error() {
        status_ &= ~SOCKET_READY;
        // if this socket is not connecting to anyone, just shut it down
        if (!should_connect_ && !(status_ & SOCKET_DOWN)) {
            // LOG_INFO("recv/write failed, shutting down connection");
            strand_->post(std::bind(&ConsistentSocket::shutdown_impl, shared_from_this()));
        }
        // otherwise, try reconnection
        if (should_connect_ && !(status_ & SOCKET_CONNECTING)) {
            connect_coro_();
        } else {
        }

    }
};

}
}
