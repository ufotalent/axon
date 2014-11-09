#include <pthread.h>
#include <unistd.h>
#include <cstdio>
#include <atomic>
#include <cassert>
#include <functional>
#include <algorithm>
#include <gtest/gtest.h>
#include "util/blocking_queue.hpp"
#include "ip/tcp/socket.hpp"
#include "service/io_service.hpp"
#include "buffer/nonfree_sequence_buffer.hpp"
#include "util/coroutine.hpp"

using namespace axon::service;
using namespace axon::ip::tcp;
using namespace axon::util;
using namespace axon::buffer;
class MiscTest: public ::testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

public:
};

TEST_F(MiscTest, get_baidu) {
    IOService service;
    Socket socket(&service);

    char message[] = "GET /index.html HTTP/1.1\r\nHost: www.baidu.com\r\n\r\n";
    axon::buffer::NonfreeSequenceBuffer<char> outbuf, inbuf;
    outbuf.prepare(strlen(message) + 1);
    strcpy(outbuf.write_head(), message);
    outbuf.accept(strlen(message));
    printf("will connect\n");
    socket.connect("115.239.210.27", 80);
    printf("connected\n");
    socket.async_send(outbuf, [](const ErrorCode &ec, size_t sz) {
        printf("send %lu %d\n", sz, ec.code());
    });

    socket.async_recv(inbuf, [&inbuf](const ErrorCode &erc, size_t sz) {
        char expected_response[] = "HTTP/1.1 200 OK";
        std::string response(inbuf.read_head(), inbuf.read_head() + inbuf.read_size());
        printf("Response: \n");
        printf("%s\n", response.c_str());
        EXPECT_EQ(response.substr(0, strlen(expected_response)), expected_response);
    });

    service.run();
    

}

TEST_F(MiscTest, corotine) {
    int val = 0;
    Coroutine coro;
    std::function<void()> func = [&val, &coro] {
        val++;
        coro.yield();
        val++;
        coro.yield();
        val++;
        coro.yield();
        val++;
    };
    coro.set_function(func);

    for (int i = 0; i < 4; i++) {
        coro();
        EXPECT_EQ(val, i+1);
    }
    coro();
    coro();
    EXPECT_EQ(val, 4);
}

TEST_F(MiscTest, corotine_exception) {
    int val = 0;
    Coroutine coro;
    std::function<void()> func = [&val, &coro] {
        val++;
        coro.yield();
        throw std::runtime_error("exception");
        coro.yield();
    };
    coro.set_function(func);

    coro();
    EXPECT_EQ(val, 1);
    EXPECT_THROW(coro(), std::runtime_error);
    EXPECT_EQ(val, 1);
    coro();
    coro();
    EXPECT_EQ(val, 1);
}
