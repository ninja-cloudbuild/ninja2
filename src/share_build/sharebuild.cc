#include "sharebuild.h"
#include <memory>

ProxyServiceClient CreateProxyClient(const std::string& proxy_service_address) {
  std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(proxy_service_address, grpc::InsecureChannelCredentials());
  // grpc_connectivity_state state = channel->GetState(true);
  // if (state == GRPC_CHANNEL_READY) {
  //     std::cout << "Channel is ready" << std::endl;
  // } else {
  //     std::cerr << "Channel is not ready, state: " << state << std::endl;
  // }
  return ProxyServiceClient(channel);
}

bool InitShareBuildEnv(const std::string& proxy_service_address,
                       const std::string& ninja_host,
                       const std::string& ninja_build_dir,
                       const std::string& root_dir,
                       const std::string& container_image) {
  ProxyServiceClient proxy_client = CreateProxyClient(proxy_service_address);
  bool init_env_res = proxy_client.InitializeBuildEnv(
      ninja_host, ninja_build_dir, root_dir, container_image);
  // std::cout << "初始化环境结果 init_env_res is " << init_env_res << std::endl;
  return init_env_res;
}

bool ClearShareBuildEnv(const std::string& proxy_service_address,
                        const std::string& ninja_host,
                        const std::string& ninja_build_dir,
                        const std::string& root_dir) {
  ProxyServiceClient proxy_client = CreateProxyClient(proxy_service_address);
  bool clear_env_res = proxy_client.ClearBuildEnv(
      ninja_host, ninja_build_dir, root_dir);
  // std::cout << "清理环境结果 clear_env_res is " << clear_env_res << std::endl;
  return clear_env_res;
}

std::string ShareExecute(const std::string& proxy_service_address,
                         const std::string& ninja_host,
                         const std::string& ninja_build_dir,
                         const std::string& root_dir,
                         const std::string& cmd_id,
                         const std::string& cmd) {
  ProxyServiceClient proxy_client = CreateProxyClient(proxy_service_address);
  std::string result_output = proxy_client.Execute(
      ninja_host, ninja_build_dir, root_dir, cmd_id, cmd);
  // std::cout << result_output << std::endl;
  return result_output;
}