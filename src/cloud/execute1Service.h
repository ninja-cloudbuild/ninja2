#include <string>
#include <grpcpp/grpcpp.h>
#include "execute1.grpc.pb.h"

using grpc::Channel;
// using grpc::ClientContext;
// using grpc::Status;
using execute1::ExecuteRequest;
using execute1::ExecuteResult;
using execute1::ExecuteService;

class Execute1Client {
    public:
        Execute1Client(std::shared_ptr<Channel> channel);

    // 组装客户的有效载荷，发送它并呈现服务器的响应
    std::string Execute1(const std::string& cmd, const std::string& id, const std::string& ninja_host, const std::string& ninja_dir);

    private:
        std::unique_ptr<ExecuteService::Stub> stub_;
};