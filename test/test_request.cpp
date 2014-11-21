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
#include "socket/MessageSocket.hpp"

using namespace axon::service;
using namespace axon::ip::tcp;
using namespace axon::util;
using namespace axon::buffer;
using namespace axon::socket;
class RequestTest: public ::testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

public:
};

namespace {
bool connected;
bool send_success;
void* connect_and_write_thread(void *args) {
    sleep(1);
    axon::service::IOService* service = static_cast<IOService*>(args);
    axon::socket::MessageSocket socket(service);
    MessageSocket::Message message(12);
    strcpy(message.content_ptr(), "socket data");

    socket.async_connect("127.0.0.1", 10087, [&socket, &message](const ErrorCode &ec, size_t) {
        connected = true;
        socket.async_send(message, [](const MessageSocket::MessageResult mr) {
            if (mr == MessageSocket::MessageResult::SUCCESS) {
                send_success = true;
            } else {
                printf("send failed %d\n", int(mr));
            }
        });
    });
    service->run();
    return NULL;
}

axon::util::Coroutine write_coro;
int send_count;
int recv_count;
int make_data(int id, char *dst) {
    return sprintf(dst, "socket data %d", id) + 1;
}
void do_write_multiple(void *args) {
    sleep(1);
    axon::service::IOService* service = static_cast<IOService*>(args);
    axon::socket::MessageSocket socket(service);

    socket.async_connect("127.0.0.1", 10087, [&socket](const ErrorCode &ec, size_t) {
        connected = true;
        write_coro();
    });
    write_coro.yield();
    for (int i = 0; i < 1000; i++) {
        char buf[20];
        int len = make_data(i, buf);
        MessageSocket::Message message(len);
        strcpy(message.content_ptr(), buf);

        socket.async_send(message, [](const MessageSocket::MessageResult mr) {
            if (mr == MessageSocket::MessageResult::SUCCESS) {
                send_success = true;
                send_count ++;
                write_coro();
            } else {
                printf("send failed %d\n", int(mr));
                return;
            }
        });
        write_coro.yield();
    }
}

axon::util::Coroutine read_coro;
void do_recv_multiple(IOService* service, MessageSocket* socket) {
    for (int i = 0; i < 1000; i++) {
        MessageSocket::Message message;
        socket->async_recv(message, [&i, &message](const MessageSocket::MessageResult mr) {
            if (mr == MessageSocket::MessageResult::SUCCESS) {
                EXPECT_EQ((int)mr, MessageSocket::MessageResult::SUCCESS);
                char buf[20];
                int len = make_data(i, buf);
                EXPECT_EQ(len, message.content_length());
                EXPECT_EQ(memcmp(buf, message.content_ptr(), len), 0);
                recv_count ++;
                read_coro();
            }
        });
        read_coro.yield();
    }
}
void* write_multiple_thread(void* args) {
    write_coro.set_function(std::bind(&do_write_multiple, args));
    write_coro();
    return NULL;
}

}
TEST_F(RequestTest, recv_one_msg) {
    connected = false;
    send_success = false;

    axon::service::IOService service;
    pthread_t thread;
    pthread_create(&thread, NULL, &connect_and_write_thread, (void*)&service);

    axon::socket::MessageSocket socket(&service);
    axon::ip::tcp::Acceptor acceptor(&service);
    acceptor.bind("127.0.0.1", 10087);
    acceptor.listen();
    acceptor.accept(socket);

    MessageSocket::Message message;
    socket.async_recv(message, [&message](const MessageSocket::MessageResult mr) {
        printf("message:%s\n", message.content_ptr());
        EXPECT_EQ((int)mr, MessageSocket::MessageResult::SUCCESS);
        EXPECT_EQ(memcmp("socket data", message.content_ptr(), 12), 0);
    });
    service.run();
    pthread_join(thread, NULL);
    EXPECT_EQ(connected, true);
    EXPECT_EQ(send_success, true);
}

TEST_F(RequestTest, recv_1000_msg) {
    connected = false;
    send_success = false;
    send_count = recv_count = 0;

    axon::service::IOService service;
    pthread_t thread;
    pthread_create(&thread, NULL, &write_multiple_thread, (void*)&service);

    axon::socket::MessageSocket socket(&service);
    axon::ip::tcp::Acceptor acceptor(&service);
    acceptor.bind("127.0.0.1", 10087);
    acceptor.listen();
    acceptor.accept(socket);

    read_coro.set_function(std::bind(&do_recv_multiple, &service, &socket));
    service.post([](){read_coro();});

    service.run();
    pthread_join(thread, NULL);
    EXPECT_EQ(connected, true);
    EXPECT_EQ(send_success, true);
    EXPECT_EQ(send_count, 1000);
    EXPECT_EQ(recv_count, 1000);
}


