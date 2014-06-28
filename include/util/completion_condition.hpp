#pragma once
#include <unistd.h>
#include <string>
#include "util/error_code.hpp"


namespace axon {
namespace util {

class CompletionCondition {
public:
    // last_check is an optimization parameter to indicate number of bytes checked last time
    virtual bool operator() (const ErrorCode& ec, const size_t bytes_transfered, const char* data, const size_t last_checked = 0) = 0;
};

class AtLeast : public CompletionCondition {
public:
    AtLeast(size_t bytes): bytes_(bytes) {
    
    }

    bool operator() (const ErrorCode& ec, const size_t bytes_transfered, const char* data, const size_t last_checked = 0) {
        return bytes_transfered >= bytes_;
    }
private:
    size_t bytes_;
};


class Contains : public CompletionCondition {
public:
    Contains(std::string pattern): pattern_(pattern) {
    
    }

    bool operator() (const ErrorCode& ec, const size_t bytes_transfered, const char* data, const size_t last_checked = 0) {
        int p = last_checked - pattern_.length(); 
        if (p < 0)
            p = 0;

        bool ret = (std::string(data + p, bytes_transfered - p).find(pattern_) != std::string::npos);
        return ret;
    }
private:
    std::string pattern_;

};


}
}
