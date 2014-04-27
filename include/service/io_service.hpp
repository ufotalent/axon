#pragma once
#include "service.hpp"

namespace axon {
namespace service {

class IOService {
public:
    typedef std::function<void()> CallBack;
public:
    void poll();
    void run();
    void post(const CallBack& hander);
};

}
}
