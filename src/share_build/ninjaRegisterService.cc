#include <string>
#include <grpcpp/grpcpp.h>
#include "ninjaRegister.grpc.pb.h"
#include "ninjaRegisterService.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using ninjaRegister::RegisterRequest;
using ninjaRegister::RegisterResponse;
using ninjaRegister::RegisterService;

RegisterClient::RegisterClient(std::shared_ptr<Channel> channel) : stub_(RegisterService::NewStub(channel)) {}

bool RegisterClient::Register(const std::string& ninja_host,
                              const std::string& ninja_dir,
                              const std::string& root_dir,
                              const std::string& container_image) {
  // Data we are sending to the server.
  RegisterRequest request;
  request.set_ninja_host(ninja_host);
  request.set_ninja_dir(ninja_dir);
  request.set_root_dir(root_dir);
  request.set_container_image(container_image);

  // Container for the data we expect from the server.
  RegisterResponse reply;

  // 客户端的上下文。它可以用来向服务器传达额外的信息或调整某些RPC行为。
  ClientContext context;

  // The actual RPC.
  Status status = stub_->ninjaRegister(&context, request, &reply);

  // Act upon its status.
  if (status.ok()) {
    char buf[1024];
    sprintf(buf, "success:%s", reply.success().c_str());
    return true;
  } else {
    std::cout << status.error_code() << ": " << status.error_message()
              << std::endl;
    return false;
  }
}