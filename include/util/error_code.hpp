#pragma once

namespace axon {
namespace util {


class ErrorCode {
public:
    enum error_code_t {
        success = 0,
        timed_out = 1,
        socket_closed = 2,
        unknown = 100

    };
    ErrorCode() {
        code_ = unknown;
    }

    ErrorCode(int code) {
        code_ = static_cast<error_code_t>(code);
    }

    operator bool() const {
        return (code_ == success);
    }

    bool operator! () const {
        return (code_ == success);
    }

    int code() const {
        return code_;
    }
private:
    error_code_t code_;
};

}
}
