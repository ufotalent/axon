#pragma once
#include <string>
#include "service/io_service.hpp"
#include "ip/tcp/socket.hpp"

namespace axon {
namespace socket {

class ConsistentSocket {
public:
    ConsistentSocket(axon::service::IOService* service);

    ConsistentSocket(axon::service::IOService* service, const std::string& addr, uint32_t port);
protected:
    axon::service::IOService* io_service_;
    axon::ip::tcp::Socket base_socket_;

    bool should_connect_;
};

}
}
