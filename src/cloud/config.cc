//
// Created by ubuntu on 23-5-6.
//

#include <memory>
#include <csignal>
#include <cstring>
#include <iostream>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <yaml-cpp/yaml.h>
#include <fstream>
#include "config.h"

Config sharebuild_config;

Config::Config() {
    ninjaDir = getCurrentWorkingDirectory();
    rootDir = ninjaDir;
    ipv4_address = get_ipv4_address();
    read_yaml();
}

std::string Config::getCurrentWorkingDirectory() {
    char buffer[1024];
    if (getcwd(buffer, sizeof(buffer)) != nullptr) {
        return std::string(buffer);
    } else {
        return std::strerror(errno);
    }
}

std::string Config::get_ipv4_address(size_t address_size) {
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
    ipv4_address = std::string(address);
    return ipv4_address;
}

bool Config::read_yaml() {
    std::ifstream file("/home/ubuntu/.config/sharebuild/node/config.yaml");
    if (file.is_open()) {
        YAML::Node node = YAML::LoadFile("/home/ubuntu/.config/sharebuild/node/config.yaml");//读取文件
        std::string master_host = node["schedulerRegisterServer"]["host"].as<std::string>();
        std::string master_port = node["schedulerRegisterServer"]["port"].as<std::string>();
        masterAddr = master_host + ":" + master_port;
        std::cout << "从config.yaml中读出的master节点地址：" << masterAddr << std::endl;
        return true;
    }
    return false;
}
