#include "rpc/base_rpc_service.hpp"
#include "util/log.hpp"

using namespace axon::rpc;
using namespace axon::service;
using namespace axon::ip::tcp;

BaseRPCService::BaseRPCService(IOService* service, const std::string& addr, uint32_t port):
    io_service_(service),
    acceptor_(service),
    addr_(addr),
    port_(port) {
    accept_coro_.set_function(std::bind(&BaseRPCService::event_loop, this));
    shutdown_ = false;
    pthread_mutex_init(&mutex_, NULL);
}

void BaseRPCService::bind_and_listen() {
    acceptor_.bind(addr_, port_);
    acceptor_.listen();
    io_service_->post(std::bind(&BaseRPCService::lock_continue, this));
}

void BaseRPCService::event_loop() {
    while (!shutdown_) {
        Session::Ptr new_session = Session::Ptr(new Session(io_service_, this));
        axon::util::ErrorCode accept_ec;
        acceptor_.async_accept(new_session->socket_->base_socket(), [this, &accept_ec](const axon::util::ErrorCode& ec) {
            accept_ec = ec;
            lock_continue();
        });
        accept_coro_.yield();

        if (shutdown_) {
            break;
        }
        if (accept_ec == axon::util::ErrorCode::success) {
            session_set_.insert(new_session);
            new_session->socket_->set_ready();
            new_session->start_event_loop();
        } else {
            LOG_INFO("accept failed with error %s", accept_ec.str());
        }

    }
}

void BaseRPCService::shutdown() {
    axon::util::ScopedLock lock(&mutex_);
    if (shutdown_) {
        return;
    }
    shutdown_ = true;
    acceptor_.shutdown();
    while (!session_set_.empty()) {
        auto it = *session_set_.begin();
        it->shutdown();
        session_set_.erase(it);
    }
}

BaseRPCService::~BaseRPCService() {
    pthread_mutex_destroy(&mutex_);
}

void BaseRPCService::remove_session(Session::Ptr session) {
    axon::util::ScopedLock lock(&mutex_);
    session_set_.erase(session);
}

void BaseRPCService::dispatch_request(const axon::socket::Message& message, Session::Ptr session) {
    LOG_INFO("request message content length %d", message.content_length());
}
