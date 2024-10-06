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
    // Users can adjust the overall options/parameters of RBE in the ninja2.conf file
    // TODO: support users in specifying a configuration file via command-line argument
    std::string config_path = "/etc/ninja2.conf";
    if (!load_server_config(config_path)) {
        Warning("Fail to read RBE server configuration.");
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

bool RBEConfig::load_server_config(const std::string& filename) {
    try {
        YAML::Node config = YAML::LoadFile(filename);
        cloud_build = config["cloud_build"].as<bool>(false);
        grpc_url = config["grpc_url"].as<std::string>();

        share_build = config["share_build"].as<bool>(false);
        master_addr = config["master_addr"].as<std::string>();

        self_ipv4_address = config["self_ipv4_address"].as<std::string>(get_ipv4_address());
        return true;
    } catch (const std::exception& e) {
        Warning("exception caught when read conf: %s", e.what());
        return false;
    }
}

void RBEConfig::init_proj_config(const std::string& project_root_path) {
    cwd = get_cwd();
    project_root = project_root_path;
    
    try {
        // By reading the .devcontainer/devcontainer.json file, obtain the image address that allows the project to be successfully built.                                     
        // When remote compilation is enabled, the build node will pull up a container using this image address to perform the build task. 
        std::string devcontainer_path = project_root_path + "/.devcontainer/devcontainer.json";
        std::ifstream f(devcontainer_path);
        Json::Value root;
        f >> root;

        std::string docker_prefix = "docker://";
        rbe_properties["container-image"] = docker_prefix + root.get("image", "").asString();
        if (!rbe_properties["container-image"].empty()) {
            rbe_properties["workload-isolation-type"] = "docker";
        }
    } catch (const std::exception& e) {
        Warning("fail to parse image key-value from devcontainer.json: %s", e.what());
    }
}