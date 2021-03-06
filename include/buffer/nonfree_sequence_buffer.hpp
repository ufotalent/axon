#pragma once
#include <unistd.h>
#include <vector>
#include <stdexcept>
#include "util/noncopyable.hpp"
#include "buffer.hpp"


namespace axon {
namespace buffer {

template <typename T>
class NonfreeSequenceBuffer : public SequenceBuffer<T>, public axon::util::Noncopyable {
public:
    NonfreeSequenceBuffer() {
        reset();
    }

    T* read_head() {
        return head_ + read_head_;
    }

    T* write_head() { 
        return head_ + write_head_;
    }

    void consume(size_t cnt) {
        check_forward(read_head_, cnt, write_head_);
    }
    
    void accept(size_t cnt) {
        check_forward(write_head_, cnt, tail_);
    }

    void prepare(size_t cnt) {
        tail_ += cnt;
        data_.resize(tail_);

        // head may be moved as vector will duplicate
        head_ = &data_[0];
    }

    void reset() {
        data_.resize(1);
        head_ = &data_[0];
        read_head_ = 0;
        write_head_ = 0;
        tail_ = 0;
    }
        
    size_t read_size() {
        return write_head_ - read_head_;
    }

    size_t write_size() {
        return tail_ - write_head_;
    }

private:
    typedef std::vector<T> DataType;
    DataType data_;
    T* head_;
    size_t read_head_;
    size_t write_head_;
    size_t tail_;

    void check_forward(size_t & from, size_t cnt, size_t barrier) {
        if (from + cnt > barrier) {
            throw std::runtime_error("no more available buffers");
        }
        from += cnt;
    }
};

}
}
