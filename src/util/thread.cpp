#include "util/thread.hpp"

using namespace axon::util;

Thread::Thread(std::function<void()> call): 
    call_(call) {
    pthread_create(&thread_, NULL, &thread_run, (void*)this);

}

void Thread::join() {
    pthread_join(thread_, NULL);
}

void* axon::util::thread_run(void* arg) {
    Thread* thr = (Thread*)arg;
    thr->call_();
    return NULL;
}

