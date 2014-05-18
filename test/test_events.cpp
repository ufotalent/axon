#include <cstdio>
#include <functional>
#include <gtest/gtest.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "service/io_service.hpp"
#include "event/recv_event.hpp"
#include "util/nonfree_sequence_buffer.hpp"
#include "event/event_service.hpp"

std::string data = "test data";
int sfd;
int read_fd;
int write_fd;
using namespace axon::util;
using namespace axon::event;
using namespace axon::service;
class EventTest: public ::testing::Test {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
        close(write_fd);
        close(read_fd);
        close(sfd);
    }
public:
};


void* socket_write_thread(void* ) {
    write_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port =  htons(20086);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    while (true) {
        if (connect(write_fd, (sockaddr*)&addr, sizeof(addr))==0) {
            break;
        }
    };
    usleep(100*100);
    for (int cnt = 0; cnt < 1; cnt++) {
        char buf[255];
        strcpy(buf, data.c_str());
        int sz = send(write_fd, buf, strlen(buf), ::MSG_NOSIGNAL);
        if (sz <= 0) {
            return NULL;
        }
        // write at 0.1s
        printf("write %d bytes\n", sz);
        usleep(100*100);
    }
    usleep(100*100);
    // close at 0.3s
    close(write_fd);
    return NULL;
}


TEST_F(EventTest, recv_ev) {
    sfd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(20086);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    ASSERT_EQ(bind(sfd, (sockaddr*)&addr, sizeof(addr)), 0);
    ASSERT_EQ(listen(sfd, 128), 0);

    pthread_t thread;
    pthread_create(&thread, NULL, &socket_write_thread, NULL);

    sockaddr_in peer;
    socklen_t peer_len = sizeof(peer);
    read_fd = accept4(sfd, (sockaddr*)&peer, &peer_len, SOCK_NONBLOCK);
    ASSERT_NE(read_fd, -1);

    while (true) {
        NonfreeSequenceBuffer<char> buf;
        buf.prepare(100);
        axon::event::RecvEvent<NonfreeSequenceBuffer<char> > ev(read_fd, axon::event::Event::EVENT_TYPE_READ, buf, [&buf, &ev](const ErrorCode &ec, size_t sz) {
            printf("errorcode %d\n", (int)ec.code());
            EXPECT_EQ(buf.read_size(), data.length());
            for (size_t i = 0; i < data.length(); i++)
            EXPECT_EQ(data[i], *(buf.read_head() + i));
            });
        if (ev.perform()) {
            ev.complete();
            break;
        }
    }
    pthread_join(thread, NULL);

}


TEST_F(EventTest, recv_empty_close) {
    sfd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(20086);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    ASSERT_EQ(bind(sfd, (sockaddr*)&addr, sizeof(addr)), 0);
    ASSERT_EQ(listen(sfd, 128), 0);

    pthread_t thread;
    pthread_create(&thread, NULL, &socket_write_thread, NULL);

    sockaddr_in peer;
    socklen_t peer_len = sizeof(peer);
    read_fd = accept4(sfd, (sockaddr*)&peer, &peer_len, SOCK_NONBLOCK);
    ASSERT_NE(read_fd, -1);

    NonfreeSequenceBuffer<char> buf;
    buf.prepare(100);
    ErrorCode expected_ec = ErrorCode::success;
    // First read should return false because write delays 1s
    axon::event::RecvEvent<NonfreeSequenceBuffer<char> > ev(read_fd, axon::event::Event::EVENT_TYPE_READ, buf, [&buf, &ev, &expected_ec](const ErrorCode &ec, size_t sz) {
        EXPECT_EQ(ec, expected_ec);
        if (!ec) {
            EXPECT_EQ(buf.read_size(), data.length());
            for (size_t i = 0; i < data.length(); i++)
                EXPECT_EQ(data[i], *(buf.read_head() + i));
        } else {
            printf("errorcode %d\n", (int)ec.code());
        }
    });
    EXPECT_EQ(ev.perform(), false);

    // wait for write to be done
    usleep(200*100);

    EXPECT_EQ(ev.perform(), true);
    ev.complete();
    EXPECT_EQ(ev.perform(), false);

    expected_ec = ErrorCode::socket_closed;
    usleep(200*100);
    EXPECT_EQ(ev.perform(), true);
    ev.complete();

    pthread_join(thread, NULL);
}


TEST_F(EventTest, recv_with_service) {
    sfd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(20086);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    ASSERT_EQ(bind(sfd, (sockaddr*)&addr, sizeof(addr)), 0);
    ASSERT_EQ(listen(sfd, 128), 0);

    pthread_t thread;
    pthread_create(&thread, NULL, &socket_write_thread, NULL);

    sockaddr_in peer;
    socklen_t peer_len = sizeof(peer);
    read_fd = accept4(sfd, (sockaddr*)&peer, &peer_len, SOCK_NONBLOCK);
    ASSERT_NE(read_fd, -1);

    NonfreeSequenceBuffer<char> buf;
    buf.prepare(100);
    ErrorCode expected_ec = ErrorCode::success;
    // First read should return false because write delays 1s

    Event::Ptr event(new axon::event::RecvEvent<NonfreeSequenceBuffer<char> >(
            read_fd,
            axon::event::Event::EVENT_TYPE_READ,
            buf,
            [&buf, &expected_ec](const ErrorCode &ec, size_t sz) {
                EXPECT_EQ(ec, expected_ec);
                if (!ec) {
                    EXPECT_EQ(buf.read_size(), data.length());
                    for (size_t i = 0; i < data.length(); i++)
                        EXPECT_EQ(data[i], *(buf.read_head() + i));
                    printf("gaoed\n");
                } else {
                    printf("errorcode %d\n", (int)ec.code());
                }
            }));

    IOService io_service;
    EventService ev_service;
    ev_service.start();

    {
        EventService::fd_event fd_ev(read_fd, &io_service);
        ev_service.register_fd(read_fd, &fd_ev);
        ev_service.start_event(event, &fd_ev);
        io_service.run();
    }
    pthread_join(thread, NULL);

}

