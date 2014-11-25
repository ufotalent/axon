#pragma once
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include "event/event.hpp"
#include "service/io_service.hpp"

namespace axon {
namespace event {

template <class SocketType>
class AcceptEvent: public Event {
public:
    typedef std::function<void(const axon::util::ErrorCode&)> CallBack;

    AcceptEvent(int fd, SocketType &socket, CallBack callback): Event(fd, EVENT_TYPE_READ), socket_(socket), callback_(callback) {
    }

    bool perform() {
        sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);
        int rfd = ::accept4(fd_, (sockaddr*)&peer, &peer_len, SOCK_NONBLOCK);

        if (rfd >= 0) {
            ec_ = axon::util::ErrorCode::success;
            socket_.assign(rfd);
            return true;
        }

        // read directly returns -1 without any data
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return false;
        }

        perror("accept error");
        switch (errno) {
        case ECONNABORTED:
            ec_ = axon::util::ErrorCode::connection_aborted;
            return true;
        case EMFILE:case ENFILE:
            ec_ = axon::util::ErrorCode::file_limit_reached;
            return true;
        case EPERM:
            ec_ = axon::util::ErrorCode::permission_error;
            return true;
        default: 
            ec_ = axon::util::ErrorCode::unknown;
            return true;
        }
    }

    void complete() {
        callback_(ec_);
    }

protected:
    SocketType& socket_;
    CallBack callback_;


};

}
}
