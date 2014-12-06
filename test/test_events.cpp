#include <cstdio>
#include <functional>
#include <gtest/gtest.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "service/io_service.hpp"
#include "event/recv_event.hpp"
#include "buffer/nonfree_sequence_buffer.hpp"
#include "event/event_service.hpp"
#include "util/test_util.hpp"

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
int test_port;
}
using namespace axon::util;
using namespace axon::event;
using namespace axon::service;
using namespace axon::buffer;
class EventTest: public ::testing::Test {
protected:
    virtual void SetUp() {
        test_port = TestUtil::available_local_port();
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
    void Listen() {
        sfd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(test_port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        ASSERT_EQ(bind(sfd, (sockaddr*)&addr, sizeof(addr)), 0);
        ASSERT_EQ(listen(sfd, 128), 0);
    }

    int Accept() {
        sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);
        int rfd = accept(sfd, (sockaddr*)&peer, &peer_len);
        if (rfd < 0)
            throw std::runtime_error("accept failed");
        return rfd;
    }

public:
};


void* event_write_thread(void* ) {
    write_fd = socket(AF_INET, SOCK_STREAM, 0);
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

void* event_multiple_write_thread(void* args) {
    long offset = (long) args;
    write_fds[offset] = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port =  htons(test_port);
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


TEST_F(EventTest, recv_ev) {
    Listen();

    pthread_t thread;
    pthread_create(&thread, NULL, &event_write_thread, NULL);

    read_fd = Accept();

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
    Listen();

    pthread_t thread;
    pthread_create(&thread, NULL, &event_write_thread, NULL);

    read_fd = Accept();

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


IOService *io_service;
EventService *ev_service;


TEST_F(EventTest, recv_with_service) {
    Listen();

    pthread_t thread;
    pthread_create(&thread, NULL, &event_write_thread, NULL);

    read_fd = Accept();

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

    io_service = new IOService();
    ev_service = &EventService::get_instance();

    {
        EventService::fd_event::Ptr fd_ev = std::make_shared<EventService::fd_event>(read_fd, io_service);
        ev_service->register_fd(read_fd, fd_ev);
        ev_service->start_event(event, fd_ev);
        io_service->run();
        ev_service->unregister_fd(read_fd, fd_ev);
    }
    pthread_join(thread, NULL);
    delete io_service;

}


void call_back(NonfreeSequenceBuffer<char>* buf, EventService::fd_event::Ptr fd_ev, const ErrorCode& ec, size_t sz) {
    if (!ec) {
        buf->prepare(100);
        Event::Ptr ev(new RecvEvent<NonfreeSequenceBuffer<char> >(
            fd_ev->fd,
            axon::event::Event::EVENT_TYPE_READ,
            *buf,
            std::bind(&call_back, buf, fd_ev, std::placeholders::_1, std::placeholders::_2)
            )
        );
        ev_service->start_event(ev, fd_ev);
    }
}

TEST_F(EventTest, sequential_recv_with_service) {
    Listen();

    pthread_t thread;
    pthread_create(&thread, NULL, &event_multiple_write_thread, NULL);

    read_fd = Accept();

    NonfreeSequenceBuffer<char> buf;
    buf.prepare(100);
    // First read should return false because write delays 1s

    io_service = new IOService();
    ev_service = &EventService::get_instance();


    {
        EventService::fd_event::Ptr fd_ev = std::make_shared<EventService::fd_event>(read_fd, io_service);
        ev_service->register_fd(read_fd, fd_ev);
        Event::Ptr ev(new RecvEvent<NonfreeSequenceBuffer<char> >(
                read_fd,
                axon::event::Event::EVENT_TYPE_READ,
                buf,
                std::bind(&call_back, &buf, fd_ev, std::placeholders::_1, std::placeholders::_2)
                )
            );
        ev_service->start_event(ev, fd_ev);
        io_service->run();
        ev_service->unregister_fd(read_fd, fd_ev);
    }
    char *p = buf.read_head();
    for (int i = 0; i < max_write_cnt; i++) {
        for (int j = 0; j < write_size; j++) {
            EXPECT_EQ(*p, i % 128);
            p++;
        }
    }
    pthread_join(thread, NULL);
    delete io_service;

}
void* run_thread_ev(void* args) {
    IOService *service = (IOService*) args;
    service->run();
    return NULL;
}
TEST_F(EventTest, multiple_socket_sequential_recv_with_service) {
    Listen();

    read_fd = -1;
    pthread_t thread[socket_cnt], threadr[socket_cnt];

    NonfreeSequenceBuffer<char> buf[socket_cnt];
    for (int i = 0; i < socket_cnt; i++) {
        pthread_create(&thread[i], NULL, &event_multiple_write_thread, (void*)(long)i);
        read_fds[i] = Accept();
        buf[i].prepare(100);
    }


    // First read should return false because write delays 1s

    io_service = new IOService();
    ev_service = &EventService::get_instance();


    EventService::fd_event::Ptr fd_evs[socket_cnt];
    for (int i = 0; i <socket_cnt; i++) {
        fd_evs[i].reset(new EventService::fd_event(read_fds[i], io_service));
        ev_service->register_fd(read_fds[i],fd_evs[i]);
        Event::Ptr ev(new RecvEvent<NonfreeSequenceBuffer<char> >(
                read_fds[i],
                axon::event::Event::EVENT_TYPE_READ,
                buf[i],
                std::bind(&call_back, &buf[i], fd_evs[i], std::placeholders::_1, std::placeholders::_2)
                )
            );
        ev_service->start_event(ev, fd_evs[i]);
    }
    for (int i = 0; i < socket_cnt; i++) {
        pthread_create(&threadr[i], NULL, &run_thread_ev, (void*)(io_service));
    }

    for (int i = 0; i < socket_cnt; i++)
        pthread_join(threadr[i], NULL);
    for (int s = 0; s < socket_cnt; s++) {
        char *p = buf[s].read_head();
        for (int i = 0; i < max_write_cnt; i++) {
            for (int j = 0; j < write_size; j++) {
                EXPECT_EQ(*p, (i+s) % 128);
                p++;
            }
        }
    }
    for (int i = 0; i < socket_cnt; i++) {
        ev_service->unregister_fd(read_fds[i], fd_evs[i]);
        pthread_join(thread[i], NULL);
    }
    delete io_service;

}


TEST_F(EventTest, multiple_socket_sequential_recv_with_service_unregister_halfway) {
    Listen();

    read_fd = -1;
    pthread_t thread[socket_cnt];
    pthread_t threadr[socket_cnt];

    NonfreeSequenceBuffer<char> buf[socket_cnt];
    for (int i = 0; i < socket_cnt; i++) {
        pthread_create(&thread[i], NULL, &event_multiple_write_thread, (void*)(long)i);
        read_fds[i] = Accept();
        buf[i].prepare(100);
    }


    // First read should return false because write delays 1s

    io_service = new IOService();
    ev_service = &EventService::get_instance();


    EventService::fd_event::Ptr fd_evs[socket_cnt];
    for (int i = 0; i <socket_cnt; i++) {
        fd_evs[i].reset(new EventService::fd_event(read_fds[i], io_service));
        ev_service->register_fd(read_fds[i],fd_evs[i]);
        Event::Ptr ev(new RecvEvent<NonfreeSequenceBuffer<char> >(
                read_fds[i],
                axon::event::Event::EVENT_TYPE_READ,
                buf[i],
                std::bind(&call_back, &buf[i], fd_evs[i], std::placeholders::_1, std::placeholders::_2)
                )
            );
        ev_service->start_event(ev, fd_evs[i]);
    }
    for (int i = 0; i < socket_cnt; i++) {
        pthread_create(&threadr[i], NULL, &run_thread_ev, (void*)(io_service));
    }
    
    sleep(3);
    for (int i = 0; i < socket_cnt; i++) {
        ev_service->unregister_fd(read_fds[i], fd_evs[i]);
    }
    
    for (int i = 0; i < socket_cnt; i++)
        pthread_join(threadr[i], NULL);

    for (int s = 0; s < socket_cnt; s++) {
        char *p = buf[s].read_head();
        for (int i = 0; i < max_write_cnt; i++) {
            bool fail = false;
            for (int j = 0; j < write_size; j++) {
                
                if ( p >= buf[s].write_head() || ( *p == 0 && ((i+s) % 128 != 0)) ) {
                    printf("stop at %ld\n", p-buf[s].read_head());
                    goto bexit;
                }

                if (*p != (i+s)%128) {
                    printf("is %d %d\n", i, s);
                    fail = true;
                }
                EXPECT_EQ(*p, (i+s) % 128);
                p++;
            }
            if (fail) {
                printf("socket %d write %d misalign\n", s, i);
            }
        }
bexit:
        continue;
    }
    for (int i = 0; i < socket_cnt; i++) {
        close(read_fds[i]);
        pthread_join(thread[i], NULL);
    }
    delete io_service;
}

