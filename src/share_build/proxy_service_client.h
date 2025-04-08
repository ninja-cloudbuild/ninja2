#pragma once

#include <cstdint>
#include <string>
#include <thread>
#include <grpcpp/grpcpp.h>

#include "proxy.grpc.pb.h"
#include "common.pb.h"
#include "../build.h"
#include "share_worker.h"

class ProxyServiceClient {
 public:
  ProxyServiceClient(std::shared_ptr<grpc::Channel> channel);
  ProxyServiceClient(const std::string& proxy_address);

  bool InitializeBuildEnv(const std::string& ninja_host, const std::string& ninja_build_dir, const std::string& root_dir, const std::string& container_image, int32_t worker_num);

  bool ClearBuildEnv(const std::string& ninja_host, const std::string& ninja_build_dir, const std::string& root_dir);

  std::pair<int, std::string> Execute(const std::string& ninja_host, const std::string& ninja_build_dir, const std::string& root_dir, const std::string& cmd_id, const std::string& cmd);


 private:
  std::unique_ptr<api::ShareBuildProxy::Stub> stub_;
};

class ProxyServiceClientPool {
public:
    ProxyServiceClientPool(const std::string& proxy_address, size_t pool_size) {
        for (size_t i = 0; i < pool_size; ++i) {
            std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(proxy_address, grpc::InsecureChannelCredentials());
            clients_.push_back(std::make_unique<ProxyServiceClient>(channel));
        }
    }

    ProxyServiceClient* GetClient() {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t index = current_index_++ % clients_.size(); // 简单的轮询分配
        return clients_[index].get();
    }

private:
    std::vector<std::unique_ptr<ProxyServiceClient>> clients_;
    std::mutex mutex_;
    std::atomic<size_t> current_index_{0};
};



class AsyncProxyClient {
public:
    AsyncProxyClient(std::shared_ptr<grpc::Channel> channel, grpc::CompletionQueue* cq);

    // 异步发送请求
    void AsyncExecute(const api::ForwardAndExecuteRequest& request, 
                      std::function<void(const api::ForwardAndExecuteResponse&, grpc::Status)> callback);

    // 处理 CompletionQueue 中的响应
    void ProcessQueue();

private:
    struct AsyncCall {
        api::ForwardAndExecuteResponse response;
        grpc::ClientContext context;
        grpc::Status status;
        std::unique_ptr<grpc::ClientAsyncResponseReader<api::ForwardAndExecuteResponse>> response_reader;
        std::function<void(const api::ForwardAndExecuteResponse&, grpc::Status)> callback;
    };

    std::unique_ptr<api::ShareBuildProxy::Stub> stub_;
    grpc::CompletionQueue* cq_;
};