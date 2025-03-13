#include <grpcpp/grpcpp.h>
#include <cstdint>

#include "proxy_service_client.h"
#include "proxy.grpc.pb.h"
#include "common.pb.h"

ProxyServiceClient::ProxyServiceClient(std::shared_ptr<grpc::Channel> channel)
    : stub_(api::ShareBuildProxy::NewStub(channel)) {}


bool ProxyServiceClient::InitializeBuildEnv(const std::string& ninja_host, const std::string& ninja_build_dir, 
                                            const std::string& root_dir, const std::string& container_image, int32_t worker_num) {
    api::InitializeBuildEnvRequest request;
    api::Project project;

    project.set_ninja_host(ninja_host);
    project.set_root_dir(root_dir);
    project.set_ninja_dir(ninja_build_dir);
    *request.mutable_project() = project;
    request.set_container_image(container_image);
    request.set_worker_num(worker_num);

    api::InitializeBuildEnvResponse response;
    grpc::ClientContext context;
    grpc::Status status = stub_->InitializeBuildEnv(&context, request, &response);
    if (!status.ok()) {
        std::cerr << "InitializeBuildEnv rpc failed. "
                  << "error code: " << status.error_code()
                  << ", msg: " << status.error_message() << std::endl;
        return false;
    }
    
    // std::cout << response.DebugString() << std::endl;
    return true;
}

bool ProxyServiceClient::ClearBuildEnv(const std::string& ninja_host, const std::string& ninja_build_dir,
                                        const std::string& root_dir) {
    api::ClearBuildEnvRequest request;
    api::Project project;
    project.set_ninja_host(ninja_host);
    project.set_root_dir(root_dir);
    project.set_ninja_dir(ninja_build_dir);
    *request.mutable_project() = project;

    api::ClearBuildEnvResponse response;
    grpc::ClientContext context;
    grpc::Status status = stub_->ClearBuildEnv(&context, request, &response);
    if (!status.ok()) {
        std::cerr << "ClearBuildEnv rpc failed."
                  << "error code: " << status.error_code()
                  << ", msg: " << status.error_message() << std::endl;
        return false;
    }

    // std::cout << response.DebugString() << std::endl;
    return true;
}

std::string ProxyServiceClient::Execute(const std::string& ninja_host, const std::string& ninja_build_dir,
                                        const std::string& root_dir, const std::string& cmd_id, const std::string& cmd) {
    api::ForwardAndExecuteRequest request;
    api::Project project;
    project.set_ninja_host(ninja_host);
    project.set_root_dir(root_dir);
    project.set_ninja_dir(ninja_build_dir);
    *request.mutable_project() = project;
    request.set_cmd_id(cmd_id);
    request.set_cmd_content(cmd);

    api::ForwardAndExecuteResponse response;
    grpc::ClientContext context;
    grpc::Status status = stub_->ForwardAndExecute(&context, request, &response);
    if (!status.ok()) {
        std::cerr << "ForwardAndExecute rpc failed." << std::endl;
        return "";
    }

    std::string result = "cmd_id: " + response.id();
    result += ", status: " + response.status().DebugString();
    result += "stdout: " + response.std_out();
    result += ", stderr: " + response.std_err();

    return result;
}