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

bool InitShareBuildEnv(const ProjectConfig &rbe_config) {
  ProxyServiceClient proxy_client = CreateProxyClient(rbe_config.shareproxy_addr);
  bool init_env_res = proxy_client.InitializeBuildEnv(rbe_config.self_ipv4_addr, 
        rbe_config.cwd, rbe_config.project_root, rbe_config.rbe_properties.at("container-image"), 
        rbe_config.worker_num);
  return init_env_res;
}

bool ClearShareBuildEnv(const ProjectConfig &rbe_config) {
  ProxyServiceClient proxy_client = CreateProxyClient(rbe_config.shareproxy_addr);
  bool clear_env_res = proxy_client.ClearBuildEnv(rbe_config.self_ipv4_addr, 
          rbe_config.cwd, rbe_config.project_root);
  return clear_env_res;
}

std::string ShareExecute(const ProjectConfig& rbe_config, 
                         const std::string& cmd_id,
                         const std::string& cmd_content) {
  ProxyServiceClient proxy_client = CreateProxyClient(rbe_config.shareproxy_addr);
  std::string result_output = proxy_client.Execute(rbe_config.self_ipv4_addr,
                rbe_config.cwd, rbe_config.project_root, cmd_id, cmd_content);
  return result_output;
}