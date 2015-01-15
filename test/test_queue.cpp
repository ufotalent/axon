#include <pthread.h>
#include <unistd.h>
#include <cstdio>
#include <atomic>
#include <cassert>
#include <functional>
#include <algorithm>
#include <gtest/gtest.h>
#include "util/blocking_queue.hpp"

class QueueTest: public ::testing::Test {
protected:
    virtual void SetUp() {
        res.clear();
        queue = new axon::util::BlockingQueue<int>();
    }

    virtual void TearDown() {
        delete queue;
    }

public:
    std::vector<char> res;
    axon::util::BlockingQueue<int>* queue;
};

void* read_thread(void* args) {
    QueueTest *test = (QueueTest*)args;
    int r = 0x3fffffff;

    int last = -1;
    while (true) {
        auto ret = test->queue->pop_front(r);
        if (ret == axon::util::BlockingQueue<int>::BlockingQueueClosed)
            break;
        if (ret == axon::util::BlockingQueue<int>::BlockingQueueSuccess) {
            test->res[r] = true;
            last = r;
        }
    }
    return NULL;
}

void* write_thread(void* args) {
    void** arg = (void**) args;
    QueueTest *test = (QueueTest*) arg[0];
    long offset = (long)arg[1];
    long interval = (long)arg[2];
    long num = (long)arg[3];
    for (int i = 0; i < num; i++) {
        test->queue->push_back(offset + interval * i);
    }
    return NULL;
}



TEST_F(QueueTest, one_product_one_consume) {
    int nn = 10000000;
    res.resize(nn, 0);

    pthread_t thread;
    pthread_create(&thread, NULL, &read_thread, this);
    for (int i = 0; i < nn; i++) {
        queue->push_back(i);
    }
    while (!queue->empty())
        sleep(1);
    queue->close();
    pthread_join(thread, NULL);

    for (int i = 0; i < nn; i++) {
        EXPECT_EQ(res[i], true);
    }
}




TEST_F(QueueTest, one_product_100_consume) {
    int nt = 100, nn = 1000000;
    res.resize(nn, false);
    pthread_t thread[nt];
    for (int i = 0; i < nt; i++) {
        pthread_create(&thread[i], NULL, &read_thread, this); 
    }
    for (int i = 0; i < nn; i++) {
        queue->push_back(i);
    }
    printf("waiting for consumer to be done.\n");
    while (!queue->empty()) {
        sleep(1);
    }
    queue->close();
    for (int i = 0; i < nt; i++) {
        pthread_join(thread[i], NULL);
    }

    for (int i = 0; i < nn; i++) {
        EXPECT_EQ(res[i], true);
    }
}

TEST_F(QueueTest, 100_product_100_consume) {
    const int nt = 100, nn = 10000000;
    res.resize(nn, 0);
    pthread_t thread_read[nt], thread_write[nt];
    void* arg[nt][4];
    for (int i = 0; i < nt; i++) {
        arg[i][0] = this;
        arg[i][1] = (void*)((long)i);
        arg[i][2] = (void*)(nt);
        arg[i][3] = (void*)(nn/nt);
        pthread_create(&thread_read[i], NULL, &read_thread, this); 
        pthread_create(&thread_write[i], NULL, &write_thread, arg[i]); 
    }

    for (int i = 0; i < nt; i++) {
        pthread_join(thread_write[i], NULL);
    }
    printf("waiting for consumer to be done.\n");
    while (!queue->empty()) {
        sleep(1);
    }
    queue->close();
    for (int i = 0; i < nt; i++) {
        pthread_join(thread_read[i], NULL);
    }

    for (int i = 0; i < nn; i++) {
        EXPECT_EQ(res[i], true);
    }
}



TEST_F(QueueTest, 100_product_100_consume_close_halfway) {
    const int nt = 100, nn = 100000000;
    res.resize(nn, 0);
    pthread_t thread_read[nt], thread_write[nt];
    void* arg[nt][4];
    for (int i = 0; i < nt; i++) {
        arg[i][0] = this;
        arg[i][1] = (void*)((long)i);
        arg[i][2] = (void*)(nt);
        arg[i][3] = (void*)(nn/nt);
        pthread_create(&thread_read[i], NULL, &read_thread, this); 
        pthread_create(&thread_write[i], NULL, &write_thread, arg[i]); 
    }

    sleep(1);
    queue->close();
    for (int i = 0; i < nt; i++) {
        pthread_join(thread_write[i], NULL);
    }
    printf("waiting for consumer to be done.\n");
    while (!queue->empty()) {
        sleep(1);
    }
    for (int i = 0; i < nt; i++) {
        pthread_join(thread_read[i], NULL);
    }

    bool close[nt] = {};
    int where[nt] = {};
    for (int i = 0; i < nn; i++) {
        if (!close[i%nt] && !res[i]) {
            close[i%nt] = true;
            where[i%nt] = i;
        }
        EXPECT_EQ(res[i], !close[i%nt]);
    }
}

TEST_F(QueueTest, 100_product_100_consume_interuption) {
    const int nt = 100, nn = 10000000;
    res.resize(nn, 0);
    pthread_t thread_read[nt], thread_write[nt];
    void* arg[nt][4];
    for (int i = 0; i < nt; i++) {
        arg[i][0] = this;
        arg[i][1] = (void*)((long)i);
        arg[i][2] = (void*)(nt);
        arg[i][3] = (void*)(nn/nt);
        pthread_create(&thread_read[i], NULL, &read_thread, this); 
        pthread_create(&thread_write[i], NULL, &write_thread, arg[i]); 
    }

    printf("waiting for producer to be done.\n");
    for (int i = 0; i < 1000; i++)  {
        queue->notify_all();
        usleep(1);
    }
    for (int i = 0; i < nt; i++) {
        pthread_join(thread_write[i], NULL);
    }
    printf("waiting for consumer to be done.\n");
    while (!queue->empty()) {
        printf("notify\n");
        queue->notify_all();
        usleep(1);
    }
    queue->notify_all();
    queue->close();
    for (int i = 0; i < nt; i++) {
        pthread_join(thread_read[i], NULL);
    }

    for (int i = 0; i < nn; i++) {
        EXPECT_EQ(res[i], true);
    }
}
