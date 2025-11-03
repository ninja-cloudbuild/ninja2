//
//  Copyright 2024 Mengning Software All rights reserved.
//
#include <unistd.h>
#include <memory>
#include <csignal>
#include <cstring>
#include <iostream>
#include <fstream>
#include <yaml-cpp/yaml.h>
#include <json/json.h>
#include "rbe_config.h"
#include <filesystem>


// #include <sstream>
#include <stdexcept>
#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// 获取当前主机 ipv4 地址
std::string get_ipv4_address(size_t address_size);

bool load_config_file(BuildConfig &config) {
    // 设置默认值 
    config.cloud_run = false;
    config.share_run = false;
    // zero config: `localhost:50051` as default sharebuild proxy address
    config.rbe_config.shareproxy_addr = "localhost:50051";
    config.rbe_config.self_ipv4_addr = get_ipv4_address(INET_ADDRSTRLEN);
    config.rbe_config.grpc_url = "";
    
    // 尝试加载配置文件并覆盖默认值
    std::string config_file = ".ninja2.conf";
    std::ifstream file(config_file);  
    if (file.is_open()) {
        #ifdef ENABLE_CONFIG_LOG
            std::cout << "Found " << config_file << " in working dir.\n";
        #endif
        file.close(); // 记得关闭文件
    } else {
        const char* home_dir = std::getenv("HOME");  
        if (home_dir == nullptr) {  
            std::cerr << "Error: HOME environment variable not set.\n";
            exit(-1);
            return false;  
        }  
    
        config_file = std::string(home_dir) + "/" + config_file;  
        std::ifstream file(config_file);  
    
        if (file.is_open()) {  
            std::cout << "Found " << config_file << " in home dir.\n";
            file.close(); // 记得关闭文件  
        } else {    
            return false;
        }
    }
    try {
        YAML::Node ninja2_conf = YAML::LoadFile(config_file);
        
        config.cloud_run = ninja2_conf["cloudbuild"].as<bool>(config.cloud_run);
        if (config.cloud_run && ninja2_conf["grpc_url"]) {
          config.rbe_config.grpc_url = ninja2_conf["grpc_url"].as<std::string>(config.rbe_config.grpc_url);
          if (config.rbe_config.grpc_url.compare(0, 7, "grpc://") != 0) {  
                Fatal("invalid grpc url in /etc/ninja2.conf");  
          }
        }
        
        config.share_run = ninja2_conf["sharebuild"].as<bool>(config.share_run);
        config.rbe_config.shareproxy_addr = ninja2_conf["shareproxy_addr"].as<std::string>(config.rbe_config.shareproxy_addr);
        config.rbe_config.self_ipv4_addr = ninja2_conf["self_ipv4_addr"].as<std::string>(config.rbe_config.self_ipv4_addr);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading config file: " << config_file << std::endl;  
        std::cerr << "Exception caught: " << e.what() << std::endl; 
        return false;
    }
}

// Constants for paths and prefixes  
const std::string DEV_CONTAINER_PATH = "/.devcontainer/devcontainer.json";  
const std::string DOCKER_PREFIX = "docker://";  

// Loads the devcontainer configuration from the specified project root  
void load_devcontainer_config(const std::string& project_root, BuildConfig &config) {  
    // Construct the path to the devcontainer configuration file  
    std::string devcontainer_path = project_root + DEV_CONTAINER_PATH;  

    // Use ifstream to read the configuration file  
    std::ifstream file(devcontainer_path);  
    if (!file.is_open()) {  
        return;
    }  

    // Create a JSON value to hold the parsed data  
    Json::Value root;  
    try {  
        file >> root;
    } catch (const std::exception& e) {  
        // std::cerr << "Error: Failed to parse JSON from devcontainer file: " << e.what() << std::endl;  
        return;
    }  

    // Retrieve the image key and construct the container image string  
    std::string image = root.get("image", "").asString();  
    if (!image.empty()) {  
        config.rbe_config.rbe_properties["container-image"] = DOCKER_PREFIX + image;  
        config.rbe_config.rbe_properties["workload-isolation-type"] = "docker";  
    }  
} 

const std::string CLOUDBUILD_FILE_NAME ="/.cloudbuild.yml";

void load_rules_file(const std::string& project_root, BuildConfig &config){
    std::string FilePath = project_root + CLOUDBUILD_FILE_NAME;
    std::ifstream file(FilePath);  
    YAML::Node  rule_set;
    if (file.is_open()){
        rule_set = YAML::LoadFile(FilePath);
    } else {
        std::cout << "YAML file not found, no filter command.\n";
        return;
    }
    if (rule_set) {
        if (rule_set["rules"]["local_only_rules"]) {
            for (const auto& cmd : rule_set["rules"]["local_only_rules"]) {
                config.rbe_config.local_only_rules.insert(cmd.as<std::string>());
            }
        }
        if (rule_set["rules"]["local_only_fuzzy"]) {
            for (const auto& cmd : rule_set["rules"]["local_only_fuzzy"]) {
                config.rbe_config.local_only_fuzzy.insert(cmd.as<std::string>());
            }
        }
        if (rule_set["rules"]["remote_exec_rules"]) {
            for (const auto& cmd : rule_set["rules"]["remote_exec_rules"]) {
                config.rbe_config.remote_exec_rules.insert(cmd.as<std::string>());
            }
        }
    }
}

std::string execute_command(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

std::string get_ipv4_address(size_t address_size) {
    char address[INET_ADDRSTRLEN];
    struct ifaddrs* ifaddr;
    struct ifaddrs* ifa;
    struct sockaddr_in* addr;

    // 获取本地网络接口地址信息列表
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    // 遍历网络接口地址信息列表，查找IPv4地址
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) {
            continue;
        }

        // 判断是否为IPv4地址
        if (ifa->ifa_addr->sa_family == AF_INET) {
            addr = (struct sockaddr_in*)ifa->ifa_addr;
            // 排除回环地址
            if (!strcmp(inet_ntoa(addr->sin_addr), "127.0.0.1")) {
                continue;
            }
            // 将IPv4地址拷贝到目标缓冲区
            strncpy(address, inet_ntoa(addr->sin_addr), address_size - 1);
            address[address_size - 1] = '\0';
            break;
        }
    }

    // 释放网络接口地址信息列表
    freeifaddrs(ifaddr);

    // 如果通过网络接口没有找到有效 IP 地址，尝试通过命令获取
    if (strlen(address) == 0) {
        std::cout << "Failed to obtain IP address via network interface, trying command..." << std::endl;
        try {
            std::string command_result = execute_command("hostname -I");
            std::istringstream iss(command_result);
            iss >> address;  // 获取第一个非回环 IP 地址
        } catch (const std::runtime_error& e) {
            std::cerr << "Failed to obtain IP address via command: " << e.what() << std::endl;
            return "";
        }
    }
    return std::string(address);
}