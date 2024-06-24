#include <string>
#include <grpcpp/grpcpp.h>
#include "ninjaRegister.grpc.pb.h"

using grpc::Channel;
// using grpc::ClientContext;
// using grpc::Status;
using ninjaRegister::RegisterRequest;
using ninjaRegister::RegisterResponse;
using ninjaRegister::RegisterService;

class RegisterClient {
    public:
        RegisterClient(std::shared_ptr<Channel> channel);

        // 组装客户的有效载荷，发送它并呈现服务器的响应
        bool Register(const std::string& ninja_host, const std::string& ninja_dir, const std::string& root_dir);

    private:
        std::unique_ptr<RegisterService::Stub> stub_;
};