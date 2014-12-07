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
#include <stdexcept>

using namespace axon::service;
using namespace axon::ip::tcp;
using namespace axon::util;
using namespace axon::buffer;
using namespace axon::socket;
namespace {
int test_port;
}
class RequestTest: public ::testing::Test {
protected:
    virtual void SetUp() {
        test_port = TestUtil::available_local_port();
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
    Message message(12);
    strcpy(message.content_ptr(), "socket data");

    socket.async_connect("127.0.0.1", test_port, [&socket, &message](const ErrorCode &ec, size_t) {
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

    socket.async_connect("127.0.0.1", test_port, [&socket](const ErrorCode &ec, size_t) {
        connected = true;
        write_coro();
    });
    write_coro.yield();
    for (int i = 0; i < 1000; i++) {
        char buf[20];
        int len = make_data(i, buf);
        Message message(len);
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
        Message message;
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
    try {
        acceptor.bind("127.0.0.1", test_port);
        acceptor.listen();
        acceptor.accept(socket);
    } catch (std::runtime_error e) {
        assert(false);
    }

    Message message;
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
    acceptor.bind("127.0.0.1", test_port);
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

TEST_F(RequestTest, consistent_recv) {
    IOService service;

    Coroutine coro; 
    coro.set_function([&service, &coro]() {
        Timer timer(&service);
        timer.expires_from_now(5000);
        timer.async_wait([&coro](const ErrorCode& ec) { coro(); });
        coro.yield();

        Acceptor acceptor(&service);
        acceptor.bind("127.0.0.1", test_port);
        acceptor.listen();
        for (int i = 0; i < 8; i++) {
            axon::socket::MessageSocket socket(&service);
            acceptor.async_accept(socket, [&coro](const ErrorCode& ec) {
                EXPECT_EQ(ec.code(), ErrorCode::success);
                coro();
            });
            coro.yield();
            char buf[20];
            int len = make_data(i, buf);
            Message message(len);
            strcpy(message.content_ptr(), buf);
            socket.async_send(message, [&coro](const MessageSocket::MessageResult& mr) {
                EXPECT_EQ((int)mr, MessageSocket::MessageResult::SUCCESS) ;
                coro();
            });
            coro.yield();
            // disconnect socket;
            socket.shutdown();
        }
    });
    service.post([&coro]() {coro();});

    Coroutine recv_coro;
    recv_coro.set_function([&service, &recv_coro] {
        axon::socket::ConsistentSocket::Ptr socket = axon::socket::ConsistentSocket::create(&service, "127.0.0.1", test_port);
        socket->start_connecting();
        Message message[10];
        int cnt = 0;
        for (int i = 0; i < 8; i++) {
            socket->async_recv(message[i], [&recv_coro, &message, i, socket, &cnt](const ConsistentSocket::SocketResult& sr){
                cnt++;
                char buf[20];
                make_data(i, buf);
                EXPECT_EQ((int)sr, ConsistentSocket::SocketResult::SUCCESS);
                EXPECT_EQ(strcmp(buf, message[i].content_ptr()), 0);
                if (!sr) {
                    printf("%d socket data: %s\n", i, message[i].content_ptr());
                }
                recv_coro();
            });
            recv_coro.yield();
        }
        socket->shutdown();
    });
    service.post([&recv_coro](){recv_coro();});
    Thread thr([&service](){service.run();});
    service.run();
    thr.join();
    printf("run finished\n");
}

TEST_F(RequestTest, consistent_send) {
    IOService service;

    Coroutine coro; 
    coro.set_function([&service, &coro]() {
        Timer timer(&service);
        timer.expires_from_now(5000);
        timer.async_wait([&coro](const ErrorCode& ec) { coro(); });
        coro.yield();

        Acceptor acceptor(&service);
        acceptor.bind("127.0.0.1", test_port);
        acceptor.listen();
        for (int i = 0; i < 8; i++) {
            axon::socket::MessageSocket socket(&service);
            acceptor.async_accept(socket, [&coro](const ErrorCode& ec) {
                EXPECT_EQ(ec.code(), ErrorCode::success);
                coro();
            });
            coro.yield();
            Message message;
            socket.async_recv(message, [&coro, i, &message](const MessageSocket::MessageResult& mr) {
                EXPECT_EQ((int)mr, MessageSocket::MessageResult::SUCCESS) ;
                char buf[20];
                make_data(i, buf);
                EXPECT_EQ(strcmp(buf, message.content_ptr()), 0);
                if (!mr) {
                    printf("%d socket data: %s\n", i, message.content_ptr());
                }
                coro();
            });
            coro.yield();
            // disconnect socket;
            socket.shutdown();
        }
    });
    service.post([&coro]() {coro();});

    Coroutine send_coro;
    send_coro.set_function([&service, &send_coro] {
        Message message[10];
        for (int i = 0; i < 8; i++) {
            char buf[20];
            int len = make_data(i, buf);
            message[i].set_size(len);
            strcpy(message[i].content_ptr(), buf);
        }
        int cnt = 0;
        for (int i = 0; i < 8; i++) {
            axon::socket::ConsistentSocket::Ptr socket = axon::socket::ConsistentSocket::create(&service, "127.0.0.1", test_port);
            socket->start_connecting();
            socket->async_send(message[i], [&send_coro, &message, i, socket, &cnt](const ConsistentSocket::SocketResult& sr){
                EXPECT_EQ((int)sr, ConsistentSocket::SocketResult::SUCCESS);
                send_coro();
            });
            socket.reset();
            send_coro.yield();
        }
    });
    service.post([&send_coro](){send_coro();});
    Thread thr([&service](){service.run();});
    service.run();
    thr.join();
    printf("run finished\n");
}


TEST_F(RequestTest, consistent_recv_shutdown) {
    IOService service;

    for (int t = 0; t < 100; t++) {
        printf("start %d ------------------------------------\n", t);
        test_port = TestUtil::available_local_port();
        Acceptor acceptor(&service);
        acceptor.bind("127.0.0.1", test_port);
        acceptor.listen();

        Coroutine coro; 
        bool send_shutdown = false;
        coro.set_function([&send_shutdown, &service, &coro, &acceptor]() {
            for (int i = 0; i < 8; i++) {
                axon::socket::MessageSocket socket(&service);
                printf("start async accept\n");
                acceptor.async_accept(socket, [&coro](const ErrorCode& ec) {
                    EXPECT_EQ(ec.code(), ErrorCode::success);
                    coro();
                });
                coro.yield();
                char buf[20];
                int len = make_data(i, buf);
                Message message(len);
                strcpy(message.content_ptr(), buf);
                printf("peer will send\n");
                socket.async_send(message, [&coro](const MessageSocket::MessageResult& mr) {
                    EXPECT_EQ((int)mr, MessageSocket::MessageResult::SUCCESS) ;
                    coro();
                });
                coro.yield();
                printf("peer shutdown\n");
                // disconnect socket;
                socket.shutdown();
            }
            send_shutdown = true;
        });
    
        service.post([&coro](){coro();});
        Coroutine recv_coro;
        printf("recv_coro is %p\n", &recv_coro);
        recv_coro.set_function([&service, &recv_coro] {
            axon::socket::ConsistentSocket::Ptr socket = axon::socket::ConsistentSocket::create(&service, "127.0.0.1", test_port);
            socket->start_connecting();
            Message message[10];
            std::atomic_int cnt;
            cnt.store(0);
            for (int i = 0; i < 10; i++) {
                socket->async_recv(message[i], [&recv_coro, &message, i, socket, &cnt](const ConsistentSocket::SocketResult& sr){
                    cnt++;
                    if (cnt <= 8) {
                        EXPECT_EQ((int)sr, ConsistentSocket::SocketResult::SUCCESS);
                    } else {
                        EXPECT_EQ((sr == ConsistentSocket::SocketResult::SUCCESS || sr == ConsistentSocket::SocketResult::CANCELED || sr == ConsistentSocket::SocketResult::DOWN), true);
                    }
                    if (!sr) {
                        printf("%d socket data: %s\n", i, message[i].content_ptr());
                    }
                    if (cnt == 8) {
                        printf("shuting down consist socket\n");
                        socket->shutdown();
                    }
                    recv_coro();
                });
            }
            for (int i = 0; i < 10; i++) {
                recv_coro.yield();
            }
            printf("will exit\n");
        });
        service.post([&recv_coro](){recv_coro();});
        Thread thr([&service](){service.run();});
        service.run();
        printf("local run finished\n");
        thr.join();
        ASSERT_EQ(send_shutdown, true);
        printf("run finished ------------------------------------\n");
    }
}

namespace {
const int TEST_DATA_COUNT = 100000;
const int TEST_THREAD_COUNT = 8;
bool socket_test_data[TEST_DATA_COUNT];

bool socket_request_succeed[TEST_DATA_COUNT];
std::atomic_int success_count;
}
TEST_F(RequestTest, consistent_recv_send) {
    IOService service;
    test_port = TestUtil::available_local_port();
    Acceptor acceptor(&service);
    acceptor.bind("127.0.0.1", test_port);
    acceptor.listen();

    std::fill(socket_test_data, socket_test_data + TEST_DATA_COUNT, false);
    std::fill(socket_request_succeed, socket_request_succeed + TEST_DATA_COUNT, false);
    success_count = 0;

    bool server_exit = false;
    Coroutine coro;
    coro.set_function([&service, &coro, &acceptor, &server_exit]() {
        while (true) {
            ConsistentSocket::Ptr socket = ConsistentSocket::create(&service);
            printf("will accept\n");
            acceptor.async_accept(socket->base_socket(), [&coro](const ErrorCode& ec) {
                EXPECT_EQ(ec.code(), ErrorCode::success);
                coro();
            });
            coro.yield();
            socket->set_ready();
            printf("accepted\n");
            // this may make remote write fail
            if (rand() % 100 == 0) {
                socket->shutdown();
                continue;
            }
            while (true) {
                Message message;
                socket->async_recv(message, [&coro](const ConsistentSocket::SocketResult& sr) {
                    EXPECT_EQ((int)sr, ConsistentSocket::SocketResult::SUCCESS);
                    coro();
                });
                coro.yield();
                EXPECT_EQ(message.content_length(), sizeof(int));
                int id = *reinterpret_cast<int*>(message.content_ptr());
                if (id >= 0)
                    socket_test_data[id] = true;
                // this may make remote read fail
                if (rand() % 100 == 0) {
                    socket->shutdown();
                    break;
                }
                socket->async_send(message, [&coro](const ConsistentSocket::SocketResult& sr) {
                    EXPECT_EQ((int)sr, ConsistentSocket::SocketResult::SUCCESS);
                    coro();
                });
                coro.yield();
                if (id == -1) {
                    printf("received termination signal\n");
                    socket->shutdown();
                    server_exit = true;
                    return 0;
                }
            }

        }
    });

    Coroutine control_coro;

    ConsistentSocket::Ptr request_socket = ConsistentSocket::create(&service, "127.0.0.1", test_port);
    Coroutine request_coro[TEST_THREAD_COUNT];
    for (int i = 0; i < TEST_THREAD_COUNT; i++) {
        request_coro[i].set_function([i, &request_coro, request_socket, &service, &control_coro]() {
            while (true) {
                Coroutine& this_coro = request_coro[i];
                for (int s = 0; s < TEST_DATA_COUNT; s++) {
                    if (s % TEST_THREAD_COUNT != i)
                        continue;
                    if (!socket_request_succeed[s]) {
                        Message message(sizeof(int));
                        *(reinterpret_cast<int*>(message.content_ptr())) = s;
    
                        ConsistentSocket::SocketResult result;
                        request_socket->async_send(message, [&this_coro, i, &result](const ConsistentSocket::SocketResult& sr) {
                            result = sr;
                            this_coro();
                        });
                        this_coro.yield();
                        if (result == ConsistentSocket::SocketResult::BUFFER_FULL) {
                            printf("buffer full\n");
                            break;
                        }
                    }
                }
                service.post([&control_coro](){control_coro();});
                this_coro.yield();
            }
        });
    }

    control_coro.set_function([&request_coro, &service, &control_coro, &request_socket, &server_exit]() {
        while (success_count != TEST_DATA_COUNT) {
            for (int i = 0; i < TEST_THREAD_COUNT; i++) {
                service.post([&request_coro, i](){request_coro[i]();});
            }
            for (int i = 0; i < TEST_THREAD_COUNT; i++) {
                control_coro.yield();
            }
            printf("success_count: %d\n", success_count.load());
        }
        while (true) {
            Message message(sizeof(int));
            *(reinterpret_cast<int*>(message.content_ptr())) = -1;
            ConsistentSocket::SocketResult result;
            request_socket->async_send(message, [&result, &control_coro](const ConsistentSocket::SocketResult& sr) {
                result = sr;
                control_coro();
            });
            control_coro.yield();
            printf("sent termination signal\n");
            if (server_exit) {
                request_socket->shutdown();
                return;
            }
            Timer timer(&service);
            timer.expires_from_now(1000);
            timer.async_wait([&control_coro](const ErrorCode& ec){
                control_coro();
            });
            control_coro.yield();
        }
    });

    Coroutine check_coro;
    check_coro.set_function([request_socket, &check_coro]() {
        while (success_count != TEST_DATA_COUNT) {
            Message message;
            ConsistentSocket::SocketResult result;
            request_socket->async_recv(message,[&check_coro, &result](const ConsistentSocket::SocketResult& sr) {
                result = sr;
                check_coro();
            });
            check_coro.yield();
            if (result == ConsistentSocket::SocketResult::SUCCESS) {
                EXPECT_EQ(message.content_length(), sizeof(int));
                int id = *(reinterpret_cast<int*>(message.content_ptr()));
                if (!socket_request_succeed[id]) {
                    printf("set %d done\n", id);
                    socket_request_succeed[id] = true;
                    success_count ++;
                }
            }
        }
    });
    request_socket->start_connecting();
    service.post([&coro](){coro();});
    service.post([&control_coro](){control_coro();});
    service.post([&check_coro](){check_coro();});

    service.run();
    EXPECT_EQ(success_count.load(), TEST_DATA_COUNT);
    for (int i = 0; i < TEST_DATA_COUNT; i++) {
        EXPECT_EQ(socket_test_data[i], true);
    }
}
