#pragma once
#include <functional>
#include <memory>
#include "util/error_code.hpp"
#include "util/noncopyable.hpp"
#include "util/strand.hpp"
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

    Event(int fd, int type): fd_(fd), type_(type), ec_(axon::util::ErrorCode::operation_canceled), callback_strand_(NULL) { }
    virtual ~Event() {}
    
    // whether to try performing before registering in EventService
    virtual bool should_pre_try() { return true; }

    typedef std::shared_ptr<Event> Ptr;

    axon::util::Strand::Ptr callback_strand() {
        return callback_strand_;
    }
    void set_callback_strand(axon::util::Strand::Ptr callback_strand) {
        callback_strand_ = callback_strand;
    }

protected:
    int fd_;
    int type_;
    axon::util::ErrorCode ec_;
    axon::util::Strand::Ptr callback_strand_;

};

}
}
