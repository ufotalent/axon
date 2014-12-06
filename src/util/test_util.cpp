#include "util/test_util.hpp"
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdexcept>
using namespace axon::util;

int TestUtil::available_local_port() {
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    int port = 10086;
    const int PORT_LIMIT = 60000;
    addr.sin_port = htons(port);
    while (port < PORT_LIMIT && bind(sfd, (sockaddr*)&addr, sizeof(addr)) != 0) {
        port++;
        addr.sin_port = htons(port);
    }
    close(sfd);
    
    if (port >= PORT_LIMIT) {
        throw std::runtime_error("unable to determine available port");
    }
    return port;
}
