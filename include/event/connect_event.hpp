#pragma once
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include "event/event.hpp"
#include "service/io_service.hpp"
#include "util/util.hpp"

namespace axon {
namespace event {

class ConnectEvent: public Event {
public:
    typedef std::function<void(const axon::util::ErrorCode&, size_t)> CallBack;

    ConnectEvent(int fd, CallBack callback): Event(fd, EVENT_TYPE_WRITE), callback_(callback) {
    }

    bool perform() {
        /*
        // BUG: if peer disconnects directly after connection, get_peer_name will fail but SO_ERROR is 0, resulting unfinished opreation
        //
        // If getpeername succeeded, the operation is surely completed, return success
        sockaddr_in peer;
        socklen_t peer_len;
        int code = getpeername(fd_, (sockaddr*)&peer, &peer_len);
        if (code == 0) {
            ec_ = axon::util::ErrorCode::success;
            return true;
        }
        */

        int opt;
        socklen_t opt_len = sizeof(opt);
        ENSURE_RETURN_ZERO(getsockopt(fd_, SOL_SOCKET, SO_ERROR, &opt, &opt_len));
        if (opt == 0) {
            ec_ = axon::util::ErrorCode::success;
            return true;
        }

        printf("%s\n", strerror(opt));
        switch (opt) {
        case EALREADY:
            ec_ = axon::util::ErrorCode::already_connecting;
            return true;
        case ECONNREFUSED:
            ec_ = axon::util::ErrorCode::connection_refused;
            return true;
        case EISCONN:
            ec_ = axon::util::ErrorCode::already_connected;
            return true;
        case ENETUNREACH:
            ec_ = axon::util::ErrorCode::network_unreachable;
            return true;
        case ETIMEDOUT:
            ec_ = axon::util::ErrorCode::timed_out;
            return true;
        default: 
            ec_ = axon::util::ErrorCode::unknown;
            return true;
        }
    }

    void complete() {
        callback_(ec_, 0);
    }

    bool should_pre_try() {
        return false;
    }

protected:
    CallBack callback_;


};

}
}
