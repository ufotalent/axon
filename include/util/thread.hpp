#pragma once
#include <pthread.h>
#include <functional>

namespace axon {
namespace util {

class Thread {
public:
    Thread(std::function<void()> call);
    void join();
private:
    std::function<void()> call_;
    pthread_t thread_;
    friend void* thread_run(void* arg);
};

void* thread_run(void*);

}
}
