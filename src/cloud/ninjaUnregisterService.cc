#include <string>
#include <grpcpp/grpcpp.h>
#include "ninjaUnregister.grpc.pb.h"
#include "ninjaUnregisterService.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using ninjaUnregister::UnregisterRequest;
using ninjaUnregister::UnregisterResponse;
using ninjaUnregister::UnregisterService;

UnregisterClient::UnregisterClient(std::shared_ptr<Channel> channel) : stub_(UnregisterService::NewStub(channel)) {}

bool UnregisterClient::Unregister(const std::string& ninja_host, const std::string& ninja_dir, const std::string& root_dir) {
    // Data we are sending to the server.
    UnregisterRequest request;
    request.set_ninja_host(ninja_host);
    request.set_ninja_dir(ninja_dir);
    request.set_root_dir(root_dir);

    // Container for the data we expect from the server.
    UnregisterResponse reply;

    // 客户端的上下文。它可以用来向服务器传达额外的信息或调整某些RPC行为。
    ClientContext context;

    // The actual RPC.
    Status status = stub_->ninjaUnregister(&context, request, &reply);

    // Act upon its status.
    if (status.ok()) {
        char buf[1024];
        sprintf(buf, "success:%s", reply.success().c_str());
        return true;
    } else {
        std::cout << status.error_code() << ": " << status.error_message() << std::endl;
        return false;
    }
}