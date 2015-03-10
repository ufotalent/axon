#include "util/coroutine.hpp"
#include <sys/mman.h>
#include <cstdlib>
#include <cstdio>
#include <boost/context/all.hpp>
#include <stdexcept>
#include "util/lock.hpp"
#include "util/util.hpp"

using namespace axon::util;
Coroutine::Coroutine(): 
    context_callee_(NULL), 
    call_(std::function<void()>()) {
    int pagesize = sysconf(_SC_PAGE_SIZE);
    stack_size_ = 10 * 1024 * 1024;
    usable_size_ = stack_size_ - pagesize;

    ENSURE_RETURN_ZERO_PERROR(posix_memalign((void**)&stack_, pagesize, stack_size_));
    //ENSURE_RETURN_ZERO_PERROR(mprotect(stack_, pagesize, PROT_NONE));
    pthread_mutex_init(&mutex_, NULL);
}
Coroutine::~Coroutine() {
    free(stack_);
}

void Coroutine::set_function(std::function<void()>&& f) {
    call_ = std::move(f);
    context_callee_ = boost::context::make_fcontext(stack_ + stack_size_, usable_size_, dispatch);
}

void Coroutine::operator()() {
    axon::util::ScopedLock lock(&mutex_);
    boost::context::jump_fcontext(&context_caller_, context_callee_, (intptr_t)this);
    if (exception_ != std::exception_ptr()) {
        std::rethrow_exception(exception_);
    }
}
void Coroutine::yield() { 
    boost::context::jump_fcontext(context_callee_, &context_caller_, 0);
}

void Coroutine::dispatch(intptr_t arg) {
    Coroutine *co = (Coroutine*) arg;
    try {
        co->call_();
    } catch (...) {
        co->exception_ = std::current_exception();
    }
    co->call_ = std::function<void()>();
    co->yield();
    // guard 
    co->exception_ = std::exception_ptr();
    while (true) {
        co->yield();
    }
}
