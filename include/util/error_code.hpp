#pragma once

namespace axon {
namespace util {


class ErrorCode {
public:
    enum error_code_t {
        success = 0,
        timed_out = 1,
        socket_closed = 2,
        operation_canceled = 3,
        connection_aborted = 4,
        file_limit_reached = 5,
        permission_error = 6,
        already_connecting = 7,
        connection_refused = 8,
        already_connected = 9,
        network_unreachable = 10,
        invalid_socket = 11,
        unknown = 100

    };
    ErrorCode() {
        code_ = unknown;
    }

    ErrorCode(int code) {
        code_ = static_cast<error_code_t>(code);
    }

    
    operator int() const {
        return code();
    }

    int code() const {
        return code_;
    }
private:
    error_code_t code_;
};

}
}
