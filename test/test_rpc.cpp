#include <pthread.h>
#include <unistd.h>
#include <cstdio>
#include <atomic>
#include <cassert>
#include <cstring>
#include <functional>
#include <algorithm>
#include <gtest/gtest.h>
#include "util/blocking_queue.hpp"
#include "ip/tcp/acceptor.hpp"
#include "service/io_service.hpp"
#include "buffer/nonfree_sequence_buffer.hpp"
#include "util/coroutine.hpp"
#include "socket/message_socket.hpp"
#include "socket/consistent_socket.hpp"
#include "util/thread.hpp"
#include "util/test_util.hpp"
#include "rpc/base_rpc_service.hpp"
#include <stdexcept>

using namespace axon::service;
using namespace axon::ip::tcp;
using namespace axon::util;
using namespace axon::buffer;
using namespace axon::socket;
using namespace axon::rpc;
namespace {
int test_port;
int test_count;
int min_success;
int max_success;
}
class RPCTest: public ::testing::Test {
protected:
    virtual void SetUp() {
        test_port = TestUtil::available_local_port();
    }

    virtual void TearDown() {
    }

public:
};


namespace {
class EchoServer: public axon::rpc::BaseRPCService {
public:
    EchoServer(IOService* service, std::string addr, uint32_t port): BaseRPCService(service, addr, port) {
    }
    void dispatch_request(const Message& message, Session::Ptr session) {
        Message response = message;
        int data = *((const int*)message.content_ptr());
        if (data != -1) {
            session->send_response(response);
        } else {
            this->shutdown();
        }
    }
};
int connect() {
    int write_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port =  htons(test_port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    while (true) {
        if (connect(write_fd, (sockaddr*)&addr, sizeof(addr))==0) {
            break;
        }
    };
    return write_fd;
}
void client_thread() {
    int write_fd = connect();
    int i;
    for (i = 0; i < test_count; i++) {
        int data = rand();
        Message message(sizeof(int));
        *((int*)message.content_ptr()) = data;
        if (write(write_fd, message.data(), message.length()) <= 0) {
            LOG_INFO("client stoped at %d", i);
            break;
        }
        char response[100];
        int sz = read(write_fd, response, message.length());
        if (sz <= 0) {
            LOG_INFO("client stoped at %d", i);
            break;
        }
        assert(sz == message.length());
        Message response_message(response, sz);
        EXPECT_EQ(response_message.valid(), true);
        EXPECT_EQ(*((int *)(response_message.content_ptr())), data);
        max_success = std::max(max_success, i);
    }
    if (i >= test_count) {
        i--;
    }
    min_success = std::min(min_success, i);
    close(write_fd);
    return;
}
void stop_server() {
    int write_fd = connect();
    int data = -1;
    Message message(sizeof(int));
    *((int*)message.content_ptr()) = data;
    assert(write(write_fd, message.data(), message.length()) > 0);
}

}
TEST_F(RPCTest, test_single) {
    IOService service;
    printf("%d\n", test_port);
    test_count = 1;
    max_success = -1;
    EchoServer server(&service, "127.0.0.1", test_port);
    server.bind_and_listen();
    Thread run_thr(std::bind(&IOService::run, &service));
    Thread client_thr(std::bind(client_thread));
    client_thr.join();
    stop_server();
    run_thr.join();
    EXPECT_EQ(max_success, 0);
}

TEST_F(RPCTest, test_30_client) {
    IOService service;
    printf("%d\n", test_port);
    test_count = 10000;
    min_success = std::numeric_limits<int>::max();
    max_success = -1;
    EchoServer server(&service, "127.0.0.1", test_port);
    service.add_work();
    server.bind_and_listen();
    Thread* run_thrs[12];
    for (int i = 0; i < 12; i++) {
        run_thrs[i] = new Thread(std::bind(&IOService::run, &service));
    }

    Thread* client_thrs[30];
    for (int i = 0; i < 30; i++) {
        client_thrs[i] = new Thread(std::bind(client_thread));
    }
    for (int i = 0; i < 30; i++) {
        client_thrs[i]->join();
        delete client_thrs[i];
    }
    stop_server();
    service.remove_work();
    for (int i = 0; i < 12; i++) {
        run_thrs[i]->join();
        delete run_thrs[i];
    }
    EXPECT_EQ(max_success, test_count - 1);
    EXPECT_EQ(min_success, test_count - 1);
}

TEST_F(RPCTest, test_30_client_shutdown_halfway) {
    IOService service;
    printf("%d\n", test_port);
    test_count = 10000;
    min_success = std::numeric_limits<int>::max();
    max_success = -1;
    EchoServer server(&service, "127.0.0.1", test_port);
    service.add_work();
    server.bind_and_listen();
    Thread* run_thrs[12];
    for (int i = 0; i < 12; i++) {
        run_thrs[i] = new Thread(std::bind(&IOService::run, &service));
    }

    Thread* client_thrs[30];
    for (int i = 0; i < 30; i++) {
        client_thrs[i] = new Thread(std::bind(client_thread));
    }
    sleep(1);
    stop_server();
    for (int i = 0; i < 30; i++) {
        client_thrs[i]->join();
        delete client_thrs[i];
    }
    service.remove_work();
    for (int i = 0; i < 12; i++) {
        run_thrs[i]->join();
        delete run_thrs[i];
    }
    LOG_INFO("min %d max %d", min_success, max_success);
}
