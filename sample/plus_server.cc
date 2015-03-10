#include "rpc/base_rpc_service.hpp"

using namespace axon::rpc;
using namespace axon::socket;
using namespace axon::service;
class PlusServer: public axon::rpc::BaseRPCService {
public:
    PlusServer(IOService* service, const std::string& addr, uint32_t port) : BaseRPCService(service, addr, port) {
    
    }
    void dispatch_request(Session::Ptr session, Context::Ptr context) {
        Message &request = context->request;
        Message &response= context->response;
        int request_length = request.content_length();
        if (request_length == 2 * sizeof(int)) {
            int *ints = reinterpret_cast<int*>(request.content_ptr());
            int result = ints[0] + ints[1];
            response.set_size(sizeof(int));
            *((int*)response.content_ptr()) = result;

            session->send_response(context);
        } else {
            LOG_INFO("invalid request, shutting down server");
            this->shutdown();
        }
    }
};

int main() {
    axon::service::IOService io_service;
    PlusServer::Ptr server = PlusServer::create<PlusServer>(&io_service, "127.0.0.1", 12345);
    server->bind_and_listen();

    io_service.run();
}
