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
#include "util/nonfree_sequence_buffer.hpp"

using namespace axon::service;
using namespace axon::ip::tcp;
using namespace axon::util;
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
    axon::util::NonfreeSequenceBuffer<char> outbuf, inbuf;
    outbuf.prepare(strlen(message) + 1);
    strcpy(outbuf.write_head(), message);
    outbuf.accept(strlen(message));
    printf("will connect\n");
    socket.connect("115.239.210.27", 8080);
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
