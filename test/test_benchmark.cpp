#include <pthread.h>
#include <unistd.h>
#include <cstdio>
#include <atomic>
#include <cassert>
#include <functional>
#include <algorithm>
#include <gtest/gtest.h>
#include <sys/time.h>
#include "util/blocking_queue.hpp"
#include "ip/tcp/socket.hpp"
#include "service/io_service.hpp"
#include "buffer/nonfree_sequence_buffer.hpp"
#include "util/coroutine.hpp"
#include "util/timer.hpp"
#include "util/thread.hpp"
#include "rpc/base_rpc_service.hpp"
#include "util/test_util.hpp"

using namespace axon::service;
using namespace axon::ip::tcp;
using namespace axon::util;
using namespace axon::buffer;
using namespace axon::rpc;
using namespace axon::socket;
namespace {
    int test_port;
    int test_count;
    std::atomic_int response_count;
}
class BenchmarkTest: public ::testing::Test {
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
    void dispatch_request(Session::Ptr session, Session::Context::Ptr context) {
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
void client_read_thread(int fd) {
    for (int i = 0; i < test_count; i++) {
        int sz = sizeof(Message::MessageHeader) + sizeof(int);
        int buf[100];
        int ret = read(fd, buf, sz);
        if (ret < 0) {
            perror("read failed");
            return;
        }
        if (ret == 0) {
            LOG_INFO("remote closed");
            return;
        }
        response_count++;
    }
}
void client_thread() {
    int write_fd = connect();
    Thread thr(std::bind(&client_read_thread, write_fd));
    int i;
    for (i = 0; i < test_count; i++) {
        int data = rand();
        Message message(sizeof(int));
        *((int*)message.content_ptr()) = data;
        int done = 0;
        while (done != message.length()) {
            int sz = send(write_fd, message.data() + done, message.length() - done, MSG_NOSIGNAL);
            if (sz < 0) {
                perror("send failed");
                LOG_INFO("client stoped at %d", i);
                goto quit;
                
            }
            done += sz;
        }
    }
quit:
    if (i >= test_count) {
        i--;
    }
    thr.join();
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
int create_count = 0;
int copy_count = 0;
int copy_cons_count = 0;
int move_count = 0;
int move_cons_count = 0;
class Monitor {
public:
    Monitor() {
        create_count++;
    }
    Monitor(const Monitor& rhs) {
        copy_cons_count++;
    }
    Monitor& operator=(const Monitor& rhs) {
        copy_count++;
        return *this;
    }
    Monitor(Monitor&& rhs) {
        move_cons_count++;
    }
    Monitor& operator=(const Monitor&& rhs) {
        move_count++;
        return *this;
    }
};
void binder(Monitor monitor) {

}
}

TEST_F(BenchmarkTest, ioservice) {
    Monitor monitor;
    IOService service;
    LOG_INFO("%d %d %d %d %d", create_count, copy_cons_count, copy_count, move_cons_count, move_count);
    service.post(std::bind(&binder, monitor));
    LOG_INFO("%d %d %d %d %d", create_count, copy_cons_count, copy_count, move_cons_count, move_count);
    service.run();
    LOG_INFO("%d %d %d %d %d", create_count, copy_cons_count, copy_count, move_cons_count, move_count);
}
TEST_F(BenchmarkTest, rpc_1_client) {
    test_count = 1000000;
    IOService service;
    EchoServer::Ptr server = EchoServer::create<EchoServer>(&service, "127.0.0.1", test_port);
    server->bind_and_listen();
    Thread run_thr(std::bind(&IOService::run, &service));
    Thread client_thr(client_thread);
    client_thr.join();
    stop_server();
    run_thr.join();
    EXPECT_EQ(response_count, test_count);
}

TEST_F(BenchmarkTest, rpc_4_client) {
    test_count = 1000000;
    response_count = 0;
    const int nt = 4;
    IOService service;
    EchoServer::Ptr server = EchoServer::create<EchoServer>(&service, "127.0.0.1", test_port);
    server->bind_and_listen();
    Thread run_thr(std::bind(&IOService::run, &service));
    Thread *client_thrs[nt];
    for (int i = 0; i < nt; i++) {
        client_thrs[i] = new Thread(client_thread);
    }
    for (int i = 0; i < nt; i++) {
        client_thrs[i]->join();
        delete client_thrs[i];
    }
    stop_server();
    run_thr.join();
    EXPECT_EQ(response_count, test_count * nt);
}

TEST_F(BenchmarkTest, rpc_4_client_server_4thr) {
    test_count = 1000000;
    response_count = 0;
    const int nt = 4;
    const int rt = 4;
    IOService service;
    EchoServer::Ptr server = EchoServer::create<EchoServer>(&service, "127.0.0.1", test_port);
    server->bind_and_listen();
    Thread* run_thrs[rt];
    for (int i = 0; i < rt; i++) {
        run_thrs[i] = new Thread(std::bind(&IOService::run, &service));
    }
    Thread *client_thrs[nt];
    for (int i = 0; i < nt; i++) {
        client_thrs[i] = new Thread(client_thread);
    }
    for (int i = 0; i < nt; i++) {
        client_thrs[i]->join();
        delete client_thrs[i];
    }
    stop_server();
    for (int i = 0; i < rt; i++) {
        run_thrs[i]->join();
        delete run_thrs[i];
    }
    EXPECT_EQ(response_count, test_count * nt);
}
