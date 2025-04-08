#include <grpcpp/grpcpp.h>
#include <cstdint>

#include "proxy_service_client.h"
#include "proxy.grpc.pb.h"
#include "common.pb.h"

ProxyServiceClient::ProxyServiceClient(std::shared_ptr<grpc::Channel> channel)
    : stub_(api::ShareBuildProxy::NewStub(channel)) {}

ProxyServiceClient::ProxyServiceClient(const std::string& proxy_address) {
    std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(proxy_address, grpc::InsecureChannelCredentials());
    stub_ = api::ShareBuildProxy::NewStub(channel);
}


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

// {exit_code, result_output} 
std::pair<int, std::string> ProxyServiceClient::Execute(const std::string& ninja_host, const std::string& ninja_build_dir,
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
        std::cout << "cmd_id: " << cmd_id << ", cmd_content: " << cmd <<  ", ForwardAndExecute rpc failed, "
                  << "error code: " << status.error_code()
                  << ", msg: " << status.error_message() << std::endl;
        return {-1, "rcp failed"};
    }

    int exit_code = 0;
    if (response.status().code() != api::PROXY_OK) {
        std::cout << "[remote execute failed.]" << "cmd_id: " << cmd_id << ", cmd_content: " << cmd;
        exit_code = -1;
    }

    std::string result;
    result += "share_stdout: " + response.std_out();
    result += ", share_stderr: " + response.std_err();

    return {exit_code, result};
}

AsyncProxyClient::AsyncProxyClient(std::shared_ptr<grpc::Channel> channel, grpc::CompletionQueue* cq) 
    : stub_(api::ShareBuildProxy::NewStub(channel)), cq_(cq) {}


void AsyncProxyClient::AsyncExecute(const api::ForwardAndExecuteRequest& request, 
                      std::function<void(const api::ForwardAndExecuteResponse&, grpc::Status)> callback) {
    auto* call = new AsyncCall;
    call->response_reader = stub_->AsyncForwardAndExecute(&call->context, request, cq_);
    call->response_reader->Finish(&call->response, &call->status, (void*)call);
    call->callback = callback;
}

void AsyncProxyClient::ProcessQueue() {
    void* tag;
    bool ok = false;
    while (cq_->Next(&tag, &ok)) {
        AsyncCall* call = static_cast<AsyncCall*>(tag);
        if (ok) {
            call->callback(call->response, call->status);
        } else {
            call->callback(call->response, grpc::Status(grpc::StatusCode::INTERNAL, "Request failed"));
        }
        delete call;
    }
}