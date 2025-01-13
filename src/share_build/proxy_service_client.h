#pragma once

#include <string>
#include <grpcpp/grpcpp.h>

#include "proxy.grpc.pb.h"
#include "common.pb.h"

class ProxyServiceClient {
 public:
  ProxyServiceClient(std::shared_ptr<grpc::Channel> channel);

  
  bool InitializeBuildEnv(const std::string& ninja_host, const std::string& ninja_build_dir, const std::string& root_dir, const std::string& container_image);
  bool ClearBuildEnv(const std::string& ninja_host, const std::string& ninja_dir, const std::string& root_dir);

  std::string Execute(const std::string& ninja_host, const std::string& ninja_build_dir, const std::string& root_dir, const std::string& cmd_id, const std::string& cmd);


 private:
  std::unique_ptr<api::ShareBuildProxy::Stub> stub_;
};