#pragma once
#include <string>
#include <arpa/inet.h>
#include "socket.hpp"

namespace axon {
namespace ip {
namespace tcp {

class Acceptor {
public:
    void bind(std::string addr, uint32_t port);
    void listen();
    void accept(Socket &sock);
    Acceptor();
    virtual ~Acceptor();
private:
    int fd_;
    sockaddr_in addr_;
};

}
}
}
