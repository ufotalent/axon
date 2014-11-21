#pragma once
#include <pthread.h>
#include <vector>
#include <functional>
#include <boost/context/all.hpp>
#include "noncopyable.hpp"

namespace axon {
namespace util {
class Coroutine : public axon::util::Noncopyable {
public:
    Coroutine();
    void yield(); // this must be called inside the coroutine
    void operator() ();
    void set_function(std::function<void()>&& f);


private:
    std::vector<char> stack_;
    boost::context::fcontext_t context_caller_;
    boost::context::fcontext_t *context_callee_;
    std::function<void()> call_;
    pthread_mutex_t mutex_;
    std::exception_ptr exception_;

    static void dispatch(intptr_t arg);

};

}
}
