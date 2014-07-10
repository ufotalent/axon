#include <cstdio>
#include <functional>
#include <gtest/gtest.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "ip/tcp/socket.hpp"
#include "ip/tcp/acceptor.hpp"
#include "util/nonfree_sequence_buffer.hpp"
#include "util/completion_condition.hpp"


namespace {
std::string data = "test data";
int sfd;
int read_fd;
int write_fd;
const int write_size = 1004;
const int max_write_cnt = 5087;
const int socket_cnt = 110;
int read_fds[socket_cnt];
int write_fds[socket_cnt];

bool read_success;
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
    char tmp_buf[255];
};

namespace {
void* socket_write_thread(void* args) {
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

    if ((size_t)args == 1) {
        close(write_fd);
        printf("directly closing socket\n");
        return NULL;
    }

    usleep(10000);
    // write at 1s
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

void* socket_write_alot_thread(void* args) {
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

    usleep(10000);
    int cnt = 0;
    while (true) {
        cnt++;
        char buf[256];
        memset(buf, cnt % 127, sizeof(buf));
        int sz = send(write_fd, buf, 256, ::MSG_NOSIGNAL);
        if (sz <= 0) {
            return NULL;
        }
        while (sz != 256) {
            int ret = send(write_fd, buf+sz, 256-sz, ::MSG_NOSIGNAL);
            if (ret < 0)
                return NULL;
            sz += ret;
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
        while (sz != write_size) {
            int ret = send(write_fds[offset], buf+sz, write_size-sz, ::MSG_NOSIGNAL);
            if (ret < 0)
                return NULL;
            sz += ret;
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

    sleep(2);
    while (true) {
        if (connect(read_fd, (sockaddr*)&addr, sizeof(addr))==0) {
            break;
        }
    };
    while (rand() % 32!=0);
    for (int cnt = 0; cnt < 1; cnt++) {
        char buf[255];
        int sz = recv(read_fd, buf, 100, ::MSG_NOSIGNAL);
        assert(sz == data.size());
        buf[sz] = 0;
        assert(data == buf);
        read_success = true;
        if (sz <= 0) {
            printf("recv error: %d en:%d\n", sz, errno);
            return NULL;
        }
        printf("read %d bytes\n", sz);
    }
    close(read_fd);
    return NULL;
}

void* socket_shutdown_thread(void* args) {
    Socket *sock = (Socket*)args;
    usleep(5000);
    sock->shutdown();
    return NULL;
}

void* socket_accept_thread(void*) {
    IOService service;
    Socket sock(&service);
    Acceptor acceptor(&service);
    acceptor.bind("127.0.0.1", 10086);
    acceptor.listen();
    printf("listened\n");
    acceptor.accept(sock);

    char buf[200];
    int sz = recv(sock.get_fd(), buf, 200, MSG_NOSIGNAL);
    buf[sz] = 0;
    printf("recv sz %d\n", sz);
    assert(sz == data.length());
    assert(buf == data);
    read_success = true;
    sleep(1);
    return NULL;
}

}


TEST_F(SocketTest, test_connect) {
    pthread_t thread;
    pthread_create(&thread, NULL, &socket_accept_thread, NULL);

    sleep(1);
    IOService service;
    Socket sock(&service);
    sock.connect("127.0.0.1", 10086);
    printf("connected\n");


    read_success = false;
    NonfreeSequenceBuffer<char> buf;
    buf.prepare(100);
    strcpy(buf.write_head(), data.c_str());  
    buf.accept(data.length()); 

    sock.async_send(buf, [](const ErrorCode &ec, size_t sz) {
            EXPECT_EQ(ec.code(), 0);
        });
    service.run();

    sock.shutdown();
    pthread_join(thread, NULL);
    EXPECT_EQ(read_success, true);
}

TEST_F(SocketTest, test_async_connect) {
    pthread_t thread;
    pthread_create(&thread, NULL, &socket_accept_thread, NULL);

    sleep(1);
    IOService service;
    Socket sock(&service);
    sock.async_connect("127.0.0.1", 10086, [&sock](const ErrorCode& ec, size_t) {

        printf("connected\n");


        read_success = false;
        NonfreeSequenceBuffer<char> buf;
        buf.prepare(100);
        strcpy(buf.write_head(), data.c_str());  
        buf.accept(data.length()); 

        sock.async_send(buf, [](const ErrorCode &ec, size_t sz) {
            EXPECT_EQ(ec.code(), 0);
        });

    });
    service.run();
    printf("run finished\n");

    sock.shutdown();
    pthread_join(thread, NULL);
    EXPECT_EQ(read_success, true);
}

TEST_F(SocketTest, test_async_connect_refused) {
    sleep(1);
    IOService service;
    Socket sock(&service);
    bool run = false;
    sock.async_connect("127.0.0.1", 51234, [&sock, &run](const ErrorCode& ec, size_t) {
        run = true;
        EXPECT_EQ(ec.code(), ErrorCode::connection_refused);

    });
    service.run();
    EXPECT_EQ(run, true);

    sock.shutdown();
}

TEST_F(SocketTest, test_async_connect_timeout) {
    sleep(1);
    IOService service;
    Socket sock(&service);
    bool run = false;
    sock.async_connect("8.8.8.8", 51234, [&sock, &run](const ErrorCode& ec, size_t) {
        run = true;
        EXPECT_EQ(ec.code(), ErrorCode::timed_out);
    });
    service.run();
    EXPECT_EQ(run, true);

    sock.shutdown();
}
TEST_F(SocketTest, recv) {
    pthread_t thread;
    pthread_create(&thread, NULL, &socket_write_thread, NULL);

    IOService service;
    Socket sock(&service);
    Acceptor acceptor(&service);
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

TEST_F(SocketTest, recv_until) {
    pthread_t thread;
    pthread_create(&thread, NULL, &socket_write_thread, NULL);

    IOService service;
    Socket sock(&service);
    Acceptor acceptor(&service);
    acceptor.bind("127.0.0.1", 10086);
    acceptor.listen();
    acceptor.accept(sock);

    while (rand() % 32 !=0);
    bool run = false;
    NonfreeSequenceBuffer<char> buf;
    axon::util::AtLeast cond(1000);
    sock.async_recv_until(buf, [&buf, &run](const ErrorCode &ec, size_t sz) {
        run = true;
        printf("recv %lu bytes ec %d\n", buf.read_size(), ec.code());
        // the socket should be closed
        EXPECT_EQ(ec, ErrorCode::socket_closed);
        EXPECT_EQ(buf.read_size(), data.length());
        EXPECT_EQ(strncmp(buf.read_head(), data.c_str(), buf.read_size()), 0);
        }, cond);
    service.run();
    EXPECT_EQ(run , true);
    pthread_join(thread, NULL);
}

TEST_F(SocketTest, recv_until_10000_bytes) {
    pthread_t thread;
    pthread_create(&thread, NULL, &socket_write_alot_thread, NULL);

    IOService service;
    Socket sock(&service);
    Acceptor acceptor(&service);
    acceptor.bind("127.0.0.1", 10086);
    acceptor.listen();
    acceptor.accept(sock);

    while (rand() % 32 !=0);
    bool run = false;
    NonfreeSequenceBuffer<char> buf;
    axon::util::AtLeast cond(10000);
    sock.async_recv_until(buf, [&sock, &buf, &run](const ErrorCode &ec, size_t sz) {
        run = true;
        printf("recv %lu bytes ec %d\n", buf.read_size(), ec.code());
        // the socket should be closed
        EXPECT_EQ(ec, ErrorCode::success);
        EXPECT_GE(buf.read_size(), 10000);
        for (size_t i = 0; i < buf.read_size(); i++) {
        EXPECT_EQ(*(buf.read_head() + i), (i /256 + 1) % 127);
        }
        sock.shutdown();
        }, cond);
    service.run();
    EXPECT_EQ(run , true);
    pthread_join(thread, NULL);
}

TEST_F(SocketTest, recv_until_contains) {
    pthread_t thread;
    pthread_create(&thread, NULL, &socket_write_alot_thread, NULL);

    IOService service;
    Socket sock(&service);
    Acceptor acceptor(&service);
    acceptor.bind("127.0.0.1", 10086);
    acceptor.listen();
    acceptor.accept(sock);

    while (rand() % 32 !=0);
    bool run = false;
    NonfreeSequenceBuffer<char> buf;
    axon::util::Contains cond("\14\15");
    sock.async_recv_until(buf, [&sock, &buf, &run](const ErrorCode &ec, size_t sz) {
        run = true;
        printf("recv %lu bytes ec %d\n", buf.read_size(), ec.code());
        // the socket should be closed
        EXPECT_EQ(ec, ErrorCode::success);
        EXPECT_GE(buf.read_size(), 014 * 256);
        for (size_t i = 0; i < buf.read_size(); i++) {
        EXPECT_EQ(*(buf.read_head() + i), (i /256 + 1) % 127);
        }
        sock.shutdown();
        }, cond);
    service.run();
    EXPECT_EQ(run , true);
    pthread_join(thread, NULL);
}


TEST_F(SocketTest, recv_remote_close) {
    pthread_t thread;
    pthread_create(&thread, NULL, &socket_write_thread, (void*)(size_t)1);

    IOService service;
    Socket sock(&service);
    Acceptor acceptor(&service);
    acceptor.bind("127.0.0.1", 10086);
    acceptor.listen();
    acceptor.accept(sock);

    // wait for remote to close
    sleep(1);
    while (rand() % 32 !=0);
    bool run = false;
    NonfreeSequenceBuffer<char> buf;
    sock.async_recv(buf, [&buf, &run](const ErrorCode &ec, size_t sz) {
        run = true;
        EXPECT_EQ(ec, ErrorCode::socket_closed);
        printf("recv %lu bytes ec %d\n", buf.read_size(), ec.code());
        });
    service.run();
    EXPECT_EQ(run , true);
    pthread_join(thread, NULL);
}

TEST_F(SocketTest, recv_cancel) {
    pthread_t thread;
    pthread_create(&thread, NULL, &socket_write_thread, NULL);

    IOService service;
    Socket sock(&service);
    Acceptor acceptor(&service);
    acceptor.bind("127.0.0.1", 10086);
    acceptor.listen();
    acceptor.accept(sock);

    pthread_t thread_shutdown;
    pthread_create(&thread_shutdown, NULL, &socket_shutdown_thread, &sock);

    while (rand() % 32 !=0);
    bool run = false;
    NonfreeSequenceBuffer<char> buf;
    sock.async_recv(buf, [&buf, &run](const ErrorCode &ec, size_t sz) {
        run = true;
        EXPECT_EQ(ec, ErrorCode::operation_canceled);
        printf("recv %lu bytes ec %d\n", buf.read_size(), ec.code());
        });
    service.run();
    EXPECT_EQ(run , true);
    pthread_join(thread, NULL);
    pthread_join(thread_shutdown, NULL);
}

void start_recv(Socket& socket, NonfreeSequenceBuffer<char>& buf, const ErrorCode& ec, size_t sz) {
    if (!ec) {
        socket.async_recv(buf, [&socket, &buf](const ErrorCode& ec, size_t sz) {
            start_recv(socket, buf, ec, sz)    ;
            });
    }
}
namespace {
    void* run_thread(void * args) {
        IOService* service = (IOService*)args;
        service->run();
        return 0;
    }
}

TEST_F(SocketTest, multiple_socket_recv) {
    IOService service;
    Acceptor acceptor(&service);
    acceptor.bind("127.0.0.1", 10086);
    acceptor.listen();

    pthread_t threads[socket_cnt], run_threads[socket_cnt];
    NonfreeSequenceBuffer<char> buffers[socket_cnt];
    std::vector<std::shared_ptr<Socket> > sockets(socket_cnt);
    for (int i = 0; i < socket_cnt; i++) {
        sockets[i].reset(new Socket(&service));
        pthread_create(&threads[i], NULL, &socket_multiple_write_thread, (void*)(size_t)(i));
        acceptor.accept(*sockets[i]);
    }

    for (int i = 0; i < socket_cnt; i++) {
        start_recv(*sockets[i], buffers[i], ErrorCode::success, 0);
    }
    for (int i = 0; i < socket_cnt; i++) {
        pthread_create(&run_threads[i], NULL, &run_thread, (void*)(&service));
    }
    for (int i = 0; i < socket_cnt; i++) {
        pthread_join(run_threads[i], NULL);
    }

    for (int s = 0; s < socket_cnt; s++) {
        EXPECT_EQ(buffers[s].read_size(), max_write_cnt * write_size);
        char *p = buffers[s].read_head();
        for (int i = 0; i < max_write_cnt; i++) {
            for (int j = 0; j < write_size; j++) {
                EXPECT_EQ(*p, (i+s) % 128);
                p++;
            }
        }
    }
    for (int i = 0; i < socket_cnt; i++) {
        pthread_join(threads[i], NULL);
    }
}



TEST_F(SocketTest, send) {

    pthread_t thread;
    pthread_create(&thread, NULL, &socket_read_thread, NULL);

    IOService service;
    Socket sock(&service);
    Acceptor acceptor(&service);
    acceptor.bind("127.0.0.1", 10086);
    acceptor.listen();
    acceptor.accept(sock);

    while (rand() % 32 !=0);
    bool run = false;
    NonfreeSequenceBuffer<char> buf;
    buf.prepare(100);
    strcpy(buf.write_head(), data.c_str());  
    buf.accept(data.size()); 
    printf("send %lu bytes\n", buf.read_size());
    sock.async_send(buf, [&buf, &run](const ErrorCode &ec, size_t sz) {
        run = true;
        printf("send ec: %d\n", ec.code());
        EXPECT_EQ(ec.code(), ErrorCode::success);
        });
    service.run();
    EXPECT_EQ(run , true);
    pthread_join(thread, NULL);


}


TEST_F(SocketTest, async_accept) {
    pthread_t thread;
    pthread_create(&thread, NULL, &socket_read_thread, NULL);

    IOService service;
    Socket sock(&service);
    Acceptor acceptor(&service);

    acceptor.bind("127.0.0.1", 10086);
    acceptor.listen();

    bool run = false;
    read_success = false;
    acceptor.async_accept(sock, [&sock, &run](const ErrorCode& ec) {
        printf("Accepted!\n");
        NonfreeSequenceBuffer<char> buf;
        buf.prepare(100);
        strcpy(buf.write_head(), data.c_str());  
        buf.accept(data.size()); 
        run = true;
        sock.async_send(buf, [](const ErrorCode& ec, size_t sz){
            printf("send ec: %d\n", ec.code());
            EXPECT_EQ(ec.code(), ErrorCode::success);
            });
        });
    service.run();
    EXPECT_EQ(run, true);
    pthread_join(thread, NULL);
    EXPECT_EQ(read_success, true);
}
