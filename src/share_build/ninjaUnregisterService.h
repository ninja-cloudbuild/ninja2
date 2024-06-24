#include <string>
#include <grpcpp/grpcpp.h>
#include "ninjaUnregister.grpc.pb.h"

using grpc::Channel;
// using grpc::ClientContext;
// using grpc::Status;
using ninjaUnregister::UnregisterRequest;
using ninjaUnregister::UnregisterResponse;
using ninjaUnregister::UnregisterService;

class UnregisterClient {
    public:
        UnregisterClient(std::shared_ptr<Channel> channel);

        // 组装客户的有效载荷，发送它并呈现服务器的响应
        bool Unregister(const std::string& ninja_host, const std::string& ninja_dir, const std::string& root_dir);

    private:
        std::unique_ptr<UnregisterService::Stub> stub_;
};