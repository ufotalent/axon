#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include "ip/tcp/acceptor.hpp"

using namespace axon::ip::tcp;

Acceptor::Acceptor() {
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
}

Acceptor::~Acceptor() {
    close(fd_);
}

void Acceptor::bind(std::string addr, uint32_t port) {
    memset(&addr_, 0, sizeof(addr_));
    addr_.sin_family = AF_INET;
    addr_.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr_.sin_addr);

    int ret = ::bind(fd_, (sockaddr*)&addr_, sizeof(addr_));
    if (ret < 0) {
        perror("bind failed");
        throw std::runtime_error("socket bind failed");
    }
}

void Acceptor::listen() {
    if (::listen(fd_, 128) < 0) {
        perror("listen failed");
        throw std::runtime_error("socket listen failed");
    }
}

void Acceptor::accept(Socket& sock) {

    sockaddr_in peer;
    socklen_t peer_len = sizeof(peer);
    int rfd = ::accept(fd_, (sockaddr*)&peer, &peer_len);
    if (rfd < 0) {
        perror("accept failed");
        throw std::runtime_error("accept failed");
    }
    sock.assign(rfd);
}
