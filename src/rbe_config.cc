//
// Created by ubuntu on 23-5-6.
//
#include <unistd.h>
#include <memory>
#include <csignal>
#include <cstring>
#include <iostream>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <yaml-cpp/yaml.h>
#include <fstream>
#include "rbe_config.h"

#include <json/json.h>

RBEConfig g_rbe_config;


RBEConfig::RBEConfig() {
    // Users can adjust the overall options/parameters of RBE in the config.yaml file
    // TODO: support users in specifying a configuration file via command-line argument
    std::string ninja2_project_dir = "/home/chanfun/merge_work/ninja2";
    std::string config_path = ninja2_project_dir + "/config.yaml";
    if (!load_config_with_yaml(config_path)) {
        std::cout << "Waning: Failed to read RBE configuration." << std::endl;
    }

    // By reading the .devcontainer/devcontainer.json file, obtain the image address that allows the project to be successfully built.                                     
    // When remote compilation is enabled, the build node will pull up a container using this image address to perform the build task. 
    try {
        std::string devcontainer_path = ninja2_project_dir + "/.devcontainer/devcontainer.json";
        std::ifstream f(devcontainer_path);
        Json::Value root;
        f >> root;

        std::string docker_prefix = "docker://";
        rbe_properties["container-image"] = docker_prefix + root.get("image", "docker.io/chanfun/ninja2_ubuntu:1.0").asString();
        rbe_properties["workload-isolation-type"] = "docker";
    } catch (const std::exception& e) {
        std::cout << "Waning: fail to parse image key-value from devcontainer.json: " << e.what() << std::endl;
    }

}

std::ostream& operator<<(std::ostream& os, const RBEConfig& config) {
    os << "cloud_build: " << config.cloud_build << std::endl;
    os << "grpc_url: " << config.grpc_url << std::endl;
    os << "share_build: " << config.share_build << std::endl;
    os << "master_addr: " << config.master_addr << std::endl;
    os << "cwd: " << config.cwd << std::endl;
    os << "project_root: " << config.project_root << std::endl;
    os << "rbe_properties: " << std::endl;
    for (auto& it : config.rbe_properties) {
        os << "    " << it.first << ": " << it.second << std::endl;
    }
    os << "self_ipv4_address: " << config.self_ipv4_address << std::endl;
    return os;
}

std::string RBEConfig::get_cwd() {
    char buffer[1024];
    if (getcwd(buffer, sizeof(buffer)) != nullptr) {
        return std::string(buffer);
    } else {
        return std::strerror(errno);
    }
}

std::string RBEConfig::get_ipv4_address(size_t address_size) {
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
    
    return std::string(address);
}

bool RBEConfig::load_config_with_yaml(const std::string& filename) {
    try {
        YAML::Node config = YAML::LoadFile(filename);
        cloud_build = config["cloud_build"].as<bool>(false);
        grpc_url = config["grpc_url"].as<std::string>();

        share_build = config["share_build"].as<bool>(false);
        master_addr = config["master_addr"].as<std::string>();

        cwd = config["cwd"].as<std::string>(get_cwd());
        project_root = config["project_root"].as<std::string>(cwd);

        // default rbe properties (may be overwritten by user .devcontainer/devcontainer.json file)
        rbe_properties["container-image"] = "docker://docker.io/chanfun/ninja2_ubuntu:1.0";
        rbe_properties["workload-isolation-type"] = "docker";
        // read from user yaml file
        const YAML::Node& properties = config["rbe_properties"];
        for (YAML::const_iterator it = properties.begin(); it != properties.end(); ++it) {
            rbe_properties[it->first.as<std::string>()] = it->second.as<std::string>();
        }

        self_ipv4_address = config["self_ipv4_address"].as<std::string>(get_ipv4_address());

        return true;
    } catch (const std::exception& e) {
        std::cout << "Waning: exception caught when read config.yaml: " << e.what() << std::endl;
        return false;
    }
}

// bool Config::read_yaml() {
//     std::ifstream file("/home/ubuntu/.config/sharebuild/node/config.yaml");
//     if (file.is_open()) {
//         YAML::Node node = YAML::LoadFile("/home/ubuntu/.config/sharebuild/node/config.yaml");//读取文件
//         std::string master_host = node["schedulerRegisterServer"]["host"].as<std::string>();
//         std::string master_port = node["schedulerRegisterServer"]["port"].as<std::string>();
//         masterAddr = master_host + ":" + master_port;
//         std::cout << "从config.yaml中读出的master节点地址：" << masterAddr << std::endl;
//         return true;
//     }
//     return false;
// }
