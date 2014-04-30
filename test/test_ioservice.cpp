#include <cstdio>
#include <functional>
#include <gtest/gtest.h>
#include <pthread.h>
#include "service/io_service.hpp"
#include <unistd.h>

bool result[10000000];
class IOServiceTest: public ::testing::Test {
protected:
    virtual void SetUp() {
        service = new axon::service::IOService;
        call_flag_ = false;
        memset(result, 0, sizeof(result));
    }

    virtual void TearDown() {
        delete service;
    }
public:
    axon::service::IOService *service;
    bool call_flag_;
};

void* remove_work(void* args) {
    sleep(1);
    auto work = (axon::service::IOService::Work*) args;
    delete work;
    return NULL;
}

void* run_thread(void* args) {
    IOServiceTest *test = (IOServiceTest*) args;
    test->service->run();
    return NULL;
}

void* post_thread(void* args) {
    void** arg = (void**) args;
    IOServiceTest *test = (IOServiceTest*) arg[0];
    long offset = (long)arg[1];
    long interval = (long)arg[2];
    long num = (long)arg[3];
    for (int i = 0; i < num; i++) {
        int res = offset + interval * i;
        test->service->post([res]() {
            result[res] = true;
        });
    }
    return NULL;
}

TEST_F(IOServiceTest, single_call_back) {
    service->post([this]() {
        call_flag_ = true;
    });
    service->run();
    EXPECT_EQ(call_flag_, true);
}

TEST_F(IOServiceTest, single_call_back_work_block) {
    service->post([this]() {
        call_flag_ = true;
        printf("called\n");
    });
    axon::service::IOService::Work *work = new axon::service::IOService::Work(*service);
    pthread_t thread;
    pthread_create(&thread, NULL, &remove_work, work);
    service->run();
    EXPECT_EQ(call_flag_, true);
}

TEST_F(IOServiceTest, one_producer_100_worker) {
    const int nt = 100;
    const int nn = 1000000;
    axon::service::IOService::Work *work = new axon::service::IOService::Work(*service);
    pthread_t threads[nt];
    for (int i = 0; i < nt; i++) {
        pthread_create(&threads[i], NULL, run_thread, this);
    }

    for (int j = 0; j < nn; j++) {
        service->post([j](){
            result[j] = true;
        });
    }

    printf("will close\n");
    pthread_t thread;
    pthread_create(&thread, NULL, &remove_work, work);
    for (int i = 0; i < nt; i++) {
        pthread_join(threads[i], NULL);
    }

    
    for (int i = 0; i < nn; i++) {
        EXPECT_EQ(result[i], true);
    }
}

TEST_F(IOServiceTest, 100_producer_100_worker) {
    const int nt = 100;
    const int nn = 1000000;
    axon::service::IOService::Work *work = new axon::service::IOService::Work(*service);
    pthread_t threads[nt];
    pthread_t threads_post[nt];
    void* arg[nt][4];
    for (int i = 0; i < nt; i++) {
        arg[i][0] = this;
        arg[i][1] = (void*)((long)i);
        arg[i][2] = (void*)(nt);
        arg[i][3] = (void*)(nn/nt);
        pthread_create(&threads[i], NULL, run_thread, this);
        pthread_create(&threads_post[i], NULL, post_thread, arg[i]);
    }

    for (int i = 0; i < nt; i++) {
        pthread_join(threads_post[i], NULL);
    }

    printf("will close\n");
    pthread_t thread;
    pthread_create(&thread, NULL, &remove_work, work);
    for (int i = 0; i < nt; i++) {
        pthread_join(threads[i], NULL);
    }

    
    for (int i = 0; i < nn; i++) {
        EXPECT_EQ(result[i], true);
    }
}
