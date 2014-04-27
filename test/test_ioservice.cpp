#include <cstdio>
#include <functional>
#include <gtest/gtest.h>
#include "service/io_service.hpp"

class IOServiceTest: public ::testing::Test {
protected:
    axon::service::IOService *service;
    virtual void SetUp() {
        service = new axon::service::IOService;
        call_flag_ = false;
    }

    virtual void TearDown() {
        delete service;
    }

    bool call_flag_;
};

TEST_F(IOServiceTest, call_back) {
    service->post([this]() {
        call_flag_ = true;
    });
    EXPECT_EQ(call_flag_, true);

}
