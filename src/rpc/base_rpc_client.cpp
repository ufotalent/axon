#include "rpc/base_rpc_client.hpp"

using namespace axon::rpc;
using namespace axon::socket;
using namespace axon::util;

BaseRPCClient::BaseRPCClient(axon::service::IOService* service, const std::string& addr, uint32_t port): timer_(service) {
    socket_ = ConsistentSocket::create(service, addr, port);
    socket_->start_connecting();
    io_service_ = service;

    coro_started_ = false;
    shutdown_ = false;
    recv_coro_.set_function(std::bind(&BaseRPCClient::recv_loop, this));
    strand_ = Strand::create(service);
    request_no_ = 0;
}

void BaseRPCClient::async_request(Context::Ptr context, std::function<void(const ClientResult&)> callback, const double timeout) {
    Ptr pthis = shared_from_this();
    strand_->dispatch([context, callback, timeout, pthis, this]() {
        Ptr _ref __attribute__((unused)) = pthis;
        if (shutdown_) {
            io_service_->post(std::bind(callback, ClientResult::CANCELED));
        }
        RequestOperation operation;
        operation.context = context;
        operation.deadline = time(NULL) + timeout;
        operation.callback = std::move(callback);

        uint32_t current_request_no = request_no_++;
        operation.context->request.header()->token = current_request_no;
        request_map_[current_request_no] = std::move(operation);

        socket_->async_send(context->request, strand_->wrap(
            std::function<void (const axon::socket::ConsistentSocket::SocketResult&)>(
                std::bind(&BaseRPCClient::request_sent_callback, pthis, current_request_no, std::placeholders::_1)
            )
        ));

        if (!coro_started_) {
            coro_started_ = true;
            strand_->post([pthis]() {
                pthis->recv_coro_();
            });

            timer_.expires_from_now(1000);
            timer_.async_wait(strand_->wrap(
                    std::function<void(const axon::util::ErrorCode&)>(
                        std::bind(&BaseRPCClient::timer_callback, pthis, std::placeholders::_1)
                    )
            ));
        }
    });
    pthis.reset();
}

void BaseRPCClient::post_result(uint32_t req_no, const ConsistentSocket::SocketResult& sr) {
    ClientResult cr = ClientResult::UNKNOWN;
    if (sr == ConsistentSocket::SocketResult::CANCELED) { cr = ClientResult::CANCELED; }
    if (sr == ConsistentSocket::SocketResult::BUFFER_FULL) { cr = ClientResult::BUFFER_FULL; }
    if (sr == ConsistentSocket::SocketResult::DOWN) { cr = ClientResult::DOWN; }
    if (sr == ConsistentSocket::SocketResult::SUCCESS) { cr = ClientResult::SUCCESS; }
    auto it = request_map_.find(req_no);
    if (it != request_map_.end()) {
        io_service_->post(std::bind(it->second.callback, cr));
        request_map_.erase(it);
    }
}
void BaseRPCClient::request_sent_callback(uint32_t req_no, const axon::socket::ConsistentSocket::SocketResult& sr) {
    if (sr != ConsistentSocket::SocketResult::SUCCESS) {
        post_result(req_no, sr);
    }
}


void BaseRPCClient::recv_loop() {
    while (!shutdown_) {
        axon::socket::Message response;

        Ptr pthis = shared_from_this();
        ConsistentSocket::SocketResult sr;
        socket_->async_recv(response, strand_->wrap(
                std::function<void (const axon::socket::ConsistentSocket::SocketResult&)>(
                    [pthis, &sr](const axon::socket::ConsistentSocket::SocketResult& r) {
                        sr = r;
                        pthis->recv_coro_();
                    }
                )
        ));
        pthis.reset();
        recv_coro_.yield();

        if (shutdown_) {
            return;
        }
        int req_no = response.header()->token;
        if (sr == ConsistentSocket::SocketResult::SUCCESS) {
            auto it = request_map_.find(req_no);
            if (it != request_map_.end()) {
                it->second.context->response = std::move(response);
                io_service_->post(std::bind(it->second.callback, ClientResult::SUCCESS));
                request_map_.erase(it);
            }
        } else {
            post_result(req_no, sr);
        }

    }

}

void BaseRPCClient::timer_callback(const axon::util::ErrorCode&) {
    if (shutdown_) return;
    time_t current_time = time(NULL);
    for (auto it = request_map_.begin(); it != request_map_.end(); it++) {
        while (it != request_map_.end() && it->second.deadline < current_time) {
            io_service_->post(std::bind(it->second.callback, ClientResult::TIMEOUT));
            it = request_map_.erase(it);
        }
        if (it == request_map_.end()) {
            break;
        }
    }
    timer_.expires_from_now(1000);
    timer_.async_wait(strand_->wrap(
            std::function<void(const axon::util::ErrorCode&)>(
                std::bind(&BaseRPCClient::timer_callback, shared_from_this(), std::placeholders::_1)
            )
    ));
}

void BaseRPCClient::shutdown() {
    Ptr pthis = shared_from_this();
    strand_->dispatch([pthis, this]() {
        Ptr _ref __attribute__((unused)) = pthis;
        if (shutdown_) {
            return;
        }
        shutdown_ = true;
        socket_->shutdown();
        socket_.reset();

        // timers does not need to be canceled
        
        // clear all callbacks
        while (!request_map_.empty()) {
            auto it = request_map_.begin();
            io_service_->post(std::bind(it->second.callback, ClientResult::CANCELED));
            request_map_.erase(it);
        }
        
    });
    pthis.reset();

}
