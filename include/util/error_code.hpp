#pragma once

namespace axon {
namespace service {

enum error_code_t {
    success = 0,
    timed_out = 1
};

class ErrorCode {
public:
    ErrorCode(int code) {
        code_ = static_cast<error_code_t>(code);
    }

    operator bool() const {
        return (code_ == success);
    }

    bool operator! () const {
        return (code_ != success);
    }
private:
    error_code_t code_;
};

}
}
