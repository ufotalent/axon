#pragma once
#include <unistd.h>
#include "util/error_code.hpp"


namespace axon {
namespace util {

class CompletionCondition {
public:
    virtual bool operator() (const ErrorCode& ec, const size_t bytes_transfered) = 0;
};

class AtLeast : public CompletionCondition {
public:
    AtLeast(size_t bytes):bytes_(bytes) {
    
    }

    virtual bool operator() (const ErrorCode& ec, const size_t bytes_transfered) {
        if (ec) {
            return true;
        }
        return bytes_transfered >= bytes_;
    }
private:
    size_t bytes_;
};


}
}
