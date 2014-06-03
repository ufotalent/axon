#include <cstdio>
#include <functional>
#include <gtest/gtest.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "ip/tcp/socket.hpp"
#include "ip/tcp/acceptor.hpp"
#include "util/nonfree_sequence_buffer.hpp"


namespace {
std::string data = "test data";
int sfd;
int read_fd;
int write_fd;
const int write_size = 1004;
const int max_write_cnt = 5087;
const int socket_cnt = 100;
int read_fds[socket_cnt];
int write_fds[socket_cnt];
}
using namespace axon::ip::tcp;
using namespace axon::service;
using namespace axon::util;
class SocketTest: public ::testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
        close(write_fd);
        close(read_fd);
        close(sfd);
        for (int i = 0; i < socket_cnt; i++) {
            close(read_fds[i]);
            close(write_fds[i]);
        }
    }
public:
};


void* socket_write_thread(void* ) {
    write_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port =  htons(10086);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    while (true) {
        if (connect(write_fd, (sockaddr*)&addr, sizeof(addr))==0) {
            break;
        }
    };
    while (rand() % 32!=0);
    for (int cnt = 0; cnt < 1; cnt++) {
        char buf[255];
        strcpy(buf, data.c_str());
        int sz = send(write_fd, buf, strlen(buf), ::MSG_NOSIGNAL);
        if (sz <= 0) {
            return NULL;
        }
        printf("write %d bytes\n", sz);
    }
    close(write_fd);
    return NULL;
}

void* socket_multiple_write_thread(void* args) {
    long offset = (long) args;
    write_fds[offset] = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port =  htons(10086);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    while (true) {
        if (connect(write_fds[offset], (sockaddr*)&addr, sizeof(addr))==0) {
            break;
        }
    };
    usleep(100*100);
    for (int cnt = 0; cnt < max_write_cnt; cnt++) {
        char buf[write_size];
        for (int i = 0; i < write_size; i++) {
            buf[i] = (cnt + offset) % 128;
        }
        int sz = send(write_fds[offset], buf, write_size, ::MSG_NOSIGNAL);
        if (sz <= 0) {
            return NULL;
        }
    }
    usleep(100*100);
    // close at 0.3s
    close(write_fds[offset]);
    return NULL;
}

void* socket_read_thread(void*) {
    read_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port =  htons(10086);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    while (true) {
        if (connect(read_fd, (sockaddr*)&addr, sizeof(addr))==0) {
            break;
        }
    };
    while (rand() % 32!=0);
    for (int cnt = 0; cnt < 1; cnt++) {
        char buf[255];
        int sz = recv(read_fd, buf, 100, ::MSG_NOSIGNAL);
        if (sz <= 0) {
            printf("recv error: %d en:%d\n", sz, errno);
            return NULL;
        }
        printf("read %d bytes\n", sz);
    }
    close(read_fd);
    return NULL;
}


TEST_F(SocketTest, recv) {

    pthread_t thread;
    pthread_create(&thread, NULL, &socket_write_thread, NULL);

    IOService service;
    Socket sock(&service);
    Acceptor acceptor;
    acceptor.bind("127.0.0.1", 10086);
    acceptor.listen();
    acceptor.accept(sock);

    while (rand() % 32 !=0);
    bool run = false;
    NonfreeSequenceBuffer<char> buf;
    sock.async_recv(buf, [&buf, &run](const ErrorCode &ec, size_t sz) {
            run = true;
            printf("recv %lu bytes ec %d\n", buf.read_size(), ec.code());
        });
    service.run();
    EXPECT_EQ(run , true);
    pthread_join(thread, NULL);

}

TEST_F(SocketTest, send) {

    pthread_t thread;
    pthread_create(&thread, NULL, &socket_read_thread, NULL);

    IOService service;
    Socket sock(&service);
    Acceptor acceptor;
    acceptor.bind("127.0.0.1", 10086);
    acceptor.listen();
    acceptor.accept(sock);

    while (rand() % 32 !=0);
    bool run = false;
    NonfreeSequenceBuffer<char> buf;
    buf.prepare(100);
    strcpy(buf.write_head(), data.c_str());  
    buf.accept(data.size()); 
    printf("send %d bytes\n", buf.read_size());
    sock.async_send(buf, [&buf, &run](const ErrorCode &ec, size_t sz) {
            run = true;
            printf("send ec: %d\n", ec.code());
        });
    service.run();
    EXPECT_EQ(run , true);
    pthread_join(thread, NULL);


}
