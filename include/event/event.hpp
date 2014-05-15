#pragma once
#include <functional>
#include "util/error_code.hpp"
#include "util/noncopyable.hpp"
namespace axon {
namespace event {
    
class Event : public axon::util::Noncopyable {
public:
    enum event_type {
        EVENT_TYPE_READ = 0,
        EVENT_TYPE_WRITE = 1,
        EVENT_TYPE_EXCEPT = 2,
        EVENT_TYPE_COUNT = 3
    };
    int get_type() const {
        return type_;
    }
    // perform() returns false if the operation needs redo and complete() can not be called.
    // it will return true even IO failed
    // perform may be called multiple times 
    virtual bool perform() = 0;

    // complete() calls the callback function with error codes
    virtual void complete() = 0;

    Event(int fd, int type): fd_(fd), type_(type) { }
    virtual ~Event() {}

protected:
    int fd_;
    int type_;

};

}
}
