#include <cstring>
#include "rpc/base_rpc_client.hpp"
using namespace axon::socket;
using namespace axon::rpc;
using namespace axon::service;

int main() {
    IOService io_service;
    BaseRPCClient::Ptr client = BaseRPCClient::create<BaseRPCClient>(&io_service, "127.0.0.1", 12345);

    Context::Ptr context(new Context());
    int numbers[2] = {1, 2};
    context->request.set_size(sizeof(numbers));
    memcpy(context->request.content_ptr(), numbers, sizeof(numbers));

    client->async_request(context, [client, context](const BaseRPCClient::ClientResult& cr) {
        if (cr == BaseRPCClient::ClientResult::SUCCESS) {
            LOG_INFO("server returned %d", *((int*)context->response.content_ptr()));
        } else {
            LOG_INFO("error happened, error code %d", (int)cr);
        }
        client->shutdown();
    });


    io_service.run();

}
