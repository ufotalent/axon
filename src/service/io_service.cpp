#include "service/io_service.hpp"

using namespace axon::service;

void IOService::post(const IOService::CallBack& handler) {
    handler();
}

