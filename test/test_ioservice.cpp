#include <pthread.h>
#include <unistd.h>

#include <cstdio>
#include <functional>
#include <stdexcept>

#include <gtest/gtest.h>

#include "service/io_service.hpp"
#include "util/util.hpp"

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

void* poll_thread(void* args) {
    sleep(1);
    IOServiceTest *test = (IOServiceTest*) args;
    test->service->poll();
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

void* post_thread_with_work(void* args) {
    void** arg = (void**) args;
    IOServiceTest *test = (IOServiceTest*) arg[0];
    long offset = (long)arg[1];
    long interval = (long)arg[2];
    long num = (long)arg[3];
    for (int i = 0; i < num; i++) {
        int res = offset + interval * i;
        axon::service::IOService::Work *work =  new axon::service::IOService::Work(*test->service);
        test->service->post([res, work]() {
            result[res] = true;
            delete work;
        });
    }
    return NULL;
}

void* remove_work(void* args) {
    auto work = (axon::service::IOService::Work*) args;
    delete work;
    return NULL;
}


TEST_F(IOServiceTest, single_call_back) {
    service->post([this]() {
        call_flag_ = true;
        });
    service->run_one();
    EXPECT_EQ(call_flag_, true);
}

TEST_F(IOServiceTest, single_call_back_work_block) {
    service->post([this]() {
        call_flag_ = true;
        });
    axon::service::IOService::Work *work = new axon::service::IOService::Work(*service);
    pthread_t thread;
    ENSURE_RETURN_ZERO_PERROR(pthread_create(&thread, NULL, &remove_work, work));
    service->run();
    pthread_join(thread, NULL);
    EXPECT_EQ(call_flag_, true);
}

TEST_F(IOServiceTest, one_producer_100_worker) {
    const int nt = 100;
    const int nn = 1000000;
    pthread_t threads[nt];

    for (int j = 0; j < nn; j++) {
        service->post([j](){
            result[j] = true;
            });
    }

    for (int i = 0; i < nt; i++) {
        ENSURE_RETURN_ZERO_PERROR(pthread_create(&threads[i], NULL, poll_thread, this));
    }

    printf("will close\n");
    for (int i = 0; i < nt; i++) {
        pthread_join(threads[i], NULL);
    }


    for (int i = 0; i < nn; i++) {
        EXPECT_EQ(result[i], true);
    }
}

TEST_F(IOServiceTest, 100_producer_100_worker) {
    const int nt = 100;
    const int nn = 10000000;
    pthread_t threads[nt];
    pthread_t threads_post[nt];
    void* arg[nt][4];
    for (int i = 0; i < nt; i++) {
        arg[i][0] = this;
        arg[i][1] = (void*)((long)i);
        arg[i][2] = (void*)(nt);
        arg[i][3] = (void*)(nn/nt);
        ENSURE_RETURN_ZERO_PERROR(pthread_create(&threads_post[i], NULL, post_thread, arg[i]));
    }
    for (int i = 0; i < nt; i++) {
        ENSURE_RETURN_ZERO_PERROR(pthread_create(&threads[i], NULL, poll_thread, this));
    }

    for (int i = 0; i < nt; i++) {
        pthread_join(threads_post[i], NULL);
    }

    printf("waiting for queue to drain\n");
    for (int i = 0; i < nt; i++) {
        pthread_join(threads[i], NULL);
    }

    service->poll();
    for (int i = 0; i < nn; i++) {
        if (result[i] != true) {
            printf("XXXXXXXX %d\n", i);
        }
        EXPECT_EQ(result[i], true);
    }
}

TEST_F(IOServiceTest, 100_producer_100_worker_run) {
    const int nt = 100;
    const int nn = 10000000;
    axon::service::IOService::Work *work = new axon::service::IOService::Work(*service);
    pthread_t threads[nt];
    pthread_t threads_post[nt];
    void* arg[nt][4];
    for (int i = 0; i < nt; i++) {
        arg[i][0] = this;
        arg[i][1] = (void*)((long)i);
        arg[i][2] = (void*)(nt);
        arg[i][3] = (void*)(nn/nt);
        ENSURE_RETURN_ZERO_PERROR(pthread_create(&threads[i], NULL, run_thread, this));
        ENSURE_RETURN_ZERO_PERROR(pthread_create(&threads_post[i], NULL, post_thread, arg[i]));
    }

    for (int i = 0; i < nt; i++) {
        pthread_join(threads_post[i], NULL);
    }

    printf("will close\n");
    pthread_t thread;
    ENSURE_RETURN_ZERO_PERROR(pthread_create(&thread, NULL, &remove_work, work));
    for (int i = 0; i < nt; i++) {
        pthread_join(threads[i], NULL);
    }
    pthread_join(thread, NULL);


    for (int i = 0; i < nn; i++) {
        EXPECT_EQ(result[i], true);
    }
}

TEST_F(IOServiceTest, 2_producer_2_worker_run_with_work) {
    const int nt = 2;
    const int nn = 10000000;
    axon::service::IOService::Work *work = new axon::service::IOService::Work(*service);
    pthread_t threads[nt];
    pthread_t threads_post[nt];
    void* arg[nt][4];
    for (int i = 0; i < nt; i++) {
        arg[i][0] = this;
        arg[i][1] = (void*)((long)i);
        arg[i][2] = (void*)(nt);
        arg[i][3] = (void*)(nn/nt);
        ENSURE_RETURN_ZERO_PERROR(pthread_create(&threads[i], NULL, run_thread, this));
        ENSURE_RETURN_ZERO_PERROR(pthread_create(&threads_post[i], NULL, post_thread_with_work, arg[i]));
    }

    for (int i = 0; i < nt; i++) {
        pthread_join(threads_post[i], NULL);
    }

    printf("will close\n");
    pthread_t thread;
    ENSURE_RETURN_ZERO_PERROR(pthread_create(&thread, NULL, &remove_work, work));
    for (int i = 0; i < nt; i++) {
        pthread_join(threads[i], NULL);
    }
    pthread_join(thread, NULL);


    for (int i = 0; i < nn; i++) {
        EXPECT_EQ(result[i], true);
    }
}


TEST_F(IOServiceTest, 8_producer_8_worker_run_with_work) {
    const int nt = 8;
    const int nn = 10000000;
    ASSERT_EQ(nn%nt, 0);
    axon::service::IOService::Work *work = new axon::service::IOService::Work(*service);
    pthread_t threads[nt];
    pthread_t threads_post[nt];
    void* arg[nt][4];
    for (int i = 0; i < nt; i++) {
        arg[i][0] = this;
        arg[i][1] = (void*)((long)i);
        arg[i][2] = (void*)(nt);
        arg[i][3] = (void*)(nn/nt);
        ENSURE_RETURN_ZERO_PERROR(pthread_create(&threads[i], NULL, run_thread, this));
        ENSURE_RETURN_ZERO_PERROR(pthread_create(&threads_post[i], NULL, post_thread_with_work, arg[i]));
    }

    for (int i = 0; i < nt; i++) {
        pthread_join(threads_post[i], NULL);
    }

    printf("will close\n");
    pthread_t thread;
    ENSURE_RETURN_ZERO_PERROR(pthread_create(&thread, NULL, &remove_work, work));
    for (int i = 0; i < nt; i++) {
        pthread_join(threads[i], NULL);
    }
    pthread_join(thread, NULL);


    for (int i = 0; i < nn; i++) {
        EXPECT_EQ(result[i], true);
    }
}

TEST_F(IOServiceTest, 8_producer_8_worker_run_remove_work_at_last) {
    const int nt = 8;
    const int nn = 10000;
    ASSERT_EQ(nn%nt, 0);
    axon::service::IOService::Work *work = new axon::service::IOService::Work(*service);
    pthread_t threads[nt];
    pthread_t threads_post[nt];
    void* arg[nt][4];
    for (int i = 0; i < nt; i++) {
        arg[i][0] = this;
        arg[i][1] = (void*)((long)i);
        arg[i][2] = (void*)(nt);
        arg[i][3] = (void*)(nn/nt);
        ENSURE_RETURN_ZERO_PERROR(pthread_create(&threads[i], NULL, run_thread, this));
        ENSURE_RETURN_ZERO_PERROR(pthread_create(&threads_post[i], NULL, post_thread_with_work, arg[i]));
    }

    for (int i = 0; i < nt; i++) {
        pthread_join(threads_post[i], NULL);
    }

    sleep(10);
    printf("will close\n");
    pthread_t thread;
    ENSURE_RETURN_ZERO_PERROR(pthread_create(&thread, NULL, &remove_work, work));
    for (int i = 0; i < nt; i++) {
        pthread_join(threads[i], NULL);
    }
    pthread_join(thread, NULL);


    for (int i = 0; i < nn; i++) {
        EXPECT_EQ(result[i], true);
    }
}

