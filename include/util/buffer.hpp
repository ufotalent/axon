#pragma once
#include <unistd.h>
#include <vector>
#include <stdexcept>
#include "util/noncopyable.hpp"


namespace axon {
namespace util {

template <typename T>
class SequenceBuffer {
public:
    typedef T ElementType;

    virtual T* read_head() = 0; 

    virtual T* write_head() = 0; 

    virtual void consume(size_t cnt) = 0;
    
    virtual void accept(size_t cnt) = 0;

    virtual void prepare(size_t cnt) = 0;
        
    virtual size_t read_size() = 0;

    virtual size_t write_size() = 0;

};

}
}
