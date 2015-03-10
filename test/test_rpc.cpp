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
#include "rpc/base_rpc_client.hpp"
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
    void dispatch_request(Session::Ptr session, Context::Ptr context) {
        int data = *((const int*)context->request.content_ptr());
        if (data != -1) {
            context->response = context->request;
            session->send_response(context);
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
    EchoServer::Ptr server = EchoServer::create<EchoServer>(&service, "127.0.0.1", test_port);
    server->bind_and_listen();
    server.reset();
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
    EchoServer::Ptr server = EchoServer::create<EchoServer>(&service, "127.0.0.1", test_port);
    server->bind_and_listen();
    server.reset();
    Thread* run_thrs[12];
    for (int i = 0; i < 12; i++) {
        run_thrs[i] = new Thread([&service](){service.run(); LOG_DEBUG("run thread exiting");});
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
    EchoServer::Ptr server = EchoServer::create<EchoServer>(&service, "127.0.0.1", test_port);
    server->bind_and_listen();
    Thread* run_thrs[12];
    for (int i = 0; i < 12; i++) {
        run_thrs[i] = new Thread([&service](){service.run(); LOG_DEBUG("run thread exiting");});
    }

    Thread* client_thrs[30];
    for (int i = 0; i < 30; i++) {
        client_thrs[i] = new Thread(std::bind(client_thread));
    }
    sleep(1);
    server->shutdown();
    server.reset();
    for (int i = 0; i < 30; i++) {
        client_thrs[i]->join();
        delete client_thrs[i];
    }
    for (int i = 0; i < 12; i++) {
        run_thrs[i]->join();
        delete run_thrs[i];
    }
    LOG_INFO("min %d max %d", min_success, max_success);
}

TEST_F(RPCTest, server_bad_init) {
    IOService service;
    EchoServer server(&service, "127.0.0.1", test_port);
    EXPECT_THROW(server.bind_and_listen(), std::bad_weak_ptr);
}

TEST_F(RPCTest, base_rpcclient) {
    IOService service;
    EchoServer::Ptr server = EchoServer::create<EchoServer>(&service, "127.0.0.1", test_port);
    server->bind_and_listen();
    BaseRPCClient::Ptr client = BaseRPCClient::create<BaseRPCClient>(&service, "127.0.0.1", test_port);

    bool done = false;
    Context::Ptr context(new Context());
    context->request.set_size(sizeof(int));
    *(reinterpret_cast<int*>(context->request.content_ptr())) = 0x01020304;
    LOG_DEBUG("%p", context.get());
    client->async_request(context, [client, context, &done](const BaseRPCClient::ClientResult& cr) {
        EXPECT_EQ((int)cr, BaseRPCClient::ClientResult::SUCCESS);
        LOG_DEBUG("%p", context.get());
        EXPECT_EQ(context->response.content_length(), sizeof(int));
        int c = *(reinterpret_cast<const int*>(context->response.content_ptr()));
        EXPECT_EQ(c, 0x01020304);
        done = true;
        stop_server();
        client->shutdown();
    });
    service.run();
    EXPECT_EQ(done, true);
}


namespace {
    std::atomic_int success_count;
    std::atomic_int done_count;
}
void rpc_client_thread(IOService* service, bool shutdown) {
    BaseRPCClient::Ptr client = BaseRPCClient::create<BaseRPCClient>(service, "127.0.0.1", test_port);
    std::shared_ptr<std::atomic_int> local_count (new std::atomic_int());
    local_count->store(0);
    for (int i = 0; i < 100000; i++) {
        Context::Ptr context(new Context());
        context->request.set_size(sizeof(int));
        int req = rand();
        *(reinterpret_cast<int*>(context->request.content_ptr())) = req;
        client->async_request(context, [shutdown, local_count, client, context, req](const BaseRPCClient::ClientResult& cr) {
            if (cr == BaseRPCClient::ClientResult::SUCCESS) {
                EXPECT_EQ((int)cr, BaseRPCClient::ClientResult::SUCCESS);
                EXPECT_EQ(context->response.content_length(), sizeof(int));
                int c = *(reinterpret_cast<const int*>(context->response.content_ptr()));
                EXPECT_EQ(c, req);
                success_count++;
            }
            done_count++;
            (*local_count)++;
            if (local_count->load() % 1000 == 0) {
                LOG_INFO("local count %d done", local_count->load());
            }
            if (done_count == 100000 * 4) {
                stop_server();
            }
            if (local_count->load() == 100000 && !shutdown) {
                client->shutdown();
            }
        });
    }
    if (shutdown) {
        sleep(1);
        client->shutdown();
    }
}
TEST_F(RPCTest, 4thr_4rpcclient) {
    success_count = 0;
    done_count = 0;
    IOService service;
    EchoServer::Ptr server = EchoServer::create<EchoServer>(&service, "127.0.0.1", test_port);
    server->bind_and_listen();

    Thread* client_threads[4];
    for (int i = 0; i < 4; i++) {
        client_threads[i] = new Thread(std::bind(rpc_client_thread, &service, false));
    }
    Thread* run_threads[4];
    for (int i = 0; i < 4; i++) {
        run_threads[i] = new Thread(std::bind(&IOService::run, &service));
    }
    for (int i = 0; i < 4; i++) {
        run_threads[i]->join();
        delete run_threads[i];
    }
    for (int i = 0; i < 4; i++) {
        client_threads[i]->join();
        delete client_threads[i];
    }
    EXPECT_EQ(done_count, 400000);
    EXPECT_EQ(success_count, 400000);
}

TEST_F(RPCTest, 4thr_4rpcclient_shutdown) {
    success_count = 0;
    done_count = 0;
    IOService service;
    EchoServer::Ptr server = EchoServer::create<EchoServer>(&service, "127.0.0.1", test_port);
    server->bind_and_listen();

    Thread* client_threads[4];
    for (int i = 0; i < 4; i++) {
        client_threads[i] = new Thread(std::bind(rpc_client_thread, &service, true));
    }
    Thread* run_threads[4];
    for (int i = 0; i < 4; i++) {
        run_threads[i] = new Thread(std::bind(&IOService::run, &service));
    }

    for (int i = 0; i < 4; i++) {
        run_threads[i]->join();
        delete run_threads[i];
    }
    for (int i = 0; i < 4; i++) {
        client_threads[i]->join();
        delete client_threads[i];
    }
    EXPECT_EQ(done_count, 400000);
    LOG_INFO("success_count: %d", success_count.load());
}

