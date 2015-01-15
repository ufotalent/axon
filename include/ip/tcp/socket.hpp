#pragma once

#include <atomic>
#include "service/io_service.hpp"
#include "event/event_service.hpp"
#include "buffer/buffer.hpp"
#include "event/recv_event.hpp"
#include "event/recv_until_event.hpp"
#include "event/send_event.hpp"
#include "event/send_until_event.hpp"
#include "util/noncopyable.hpp"
#include "util/completion_condition.hpp"
#include "util/error_code.hpp"
#include "util/strand.hpp"

namespace axon {
namespace ip {
namespace tcp {
class Socket: public axon::util::Noncopyable {
public:
    typedef std::function<void(const axon::util::ErrorCode&, size_t)> CallBack;
    Socket(axon::service::IOService* io_service);
    virtual ~Socket();

    template <class Buffer>
    void async_recv(Buffer& buf, CallBack callback, axon::util::Strand::Ptr callback_strand = NULL) {
        if (is_down_.load()) {
            io_service_->post(std::bind(callback, axon::util::ErrorCode::invalid_socket, 0));
            return;
        }
        typename axon::event::RecvEvent<Buffer>::Ptr ev(new axon::event::RecvEvent<Buffer>(
                fd_, 
                axon::event::Event::EVENT_TYPE_READ,
                buf,
                callback));
        ev->set_callback_strand(callback_strand);
        ev_service_->start_event(ev, fd_ev_);
    }

    template <class Buffer, class CompletionCondition>
    void async_recv_until(Buffer& buf, CallBack callback, CompletionCondition condition, axon::util::Strand::Ptr callback_strand = NULL) {
        if (is_down_.load()) {
            io_service_->post(std::bind(callback, axon::util::ErrorCode::invalid_socket, 0));
            return;
        }
        typename axon::event::RecvUntilEvent<Buffer, CompletionCondition>::Ptr ev(new axon::event::RecvUntilEvent<Buffer, CompletionCondition>(
                fd_, 
                axon::event::Event::EVENT_TYPE_READ,
                buf,
                callback,
                condition));
        ev->set_callback_strand(callback_strand);
        ev_service_->start_event(ev, fd_ev_);
    }

    template <class Buffer>
    void async_recv_all(Buffer& buf, CallBack callback, axon::util::Strand::Ptr callback_strand = NULL) {
        async_recv_until(buf, callback, axon::util::AtLeast(buf.write_size()), callback_strand);
    }

    template <class Buffer>
    void async_send(Buffer& buf, CallBack callback, axon::util::Strand::Ptr callback_strand = NULL) {
        if (is_down_.load()) {
            io_service_->post(std::bind(callback, axon::util::ErrorCode::invalid_socket, 0));
            return;
        }
        typename axon::event::SendEvent<Buffer>::Ptr ev(new axon::event::SendEvent<Buffer>(
                fd_, 
                axon::event::Event::EVENT_TYPE_WRITE, buf, callback));
        ev->set_callback_strand(callback_strand);
        ev_service_->start_event(ev, fd_ev_);
    }

    template <class Buffer, class CompletionCondition>
    void async_send_until(Buffer& buf, CallBack callback, CompletionCondition condition, axon::util::Strand::Ptr callback_strand = NULL) {
        if (is_down_.load()) {
            io_service_->post(std::bind(callback, axon::util::ErrorCode::invalid_socket, 0));
            return;
        }
        typename axon::event::SendUntilEvent<Buffer, CompletionCondition>::Ptr ev(new axon::event::SendUntilEvent<Buffer, CompletionCondition>(
                fd_, 
                axon::event::Event::EVENT_TYPE_WRITE,
                buf,
                callback,
                condition));
        ev->set_callback_strand(callback_strand);
        ev_service_->start_event(ev, fd_ev_);
    }

    template <class Buffer>
    void async_send_all(Buffer& buf, CallBack callback, axon::util::Strand::Ptr callback_strand = NULL) {
        async_send_until(buf, callback, axon::util::AtLeast(buf.read_size()), callback_strand);
    }

    void connect(std::string remote_addr, uint32_t port);
    void async_connect(std::string remote_addr, uint32_t port, CallBack callback, axon::util::Strand::Ptr callback_strand = NULL);
    void assign(int fd);
    void shutdown();

    // For debugging purpose
    int get_fd() const; 

protected:
    int fd_;
    axon::service::IOService* io_service_;
    axon::event::EventService* ev_service_;
    axon::event::EventService::fd_event::Ptr fd_ev_;
    std::atomic_bool is_down_;

    int do_connect(const std::string& remote_addr, uint32_t port);
    void fail_if_down() {
    }
};

}
}
}
