#pragma once

namespace axon {
namespace util {

class Noncopyable {
public:
    Noncopyable(const Noncopyable&) = delete;
    Noncopyable& operator= (const Noncopyable&) = delete;
    Noncopyable() {}
};

}
}
