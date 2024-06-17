//
// Created by ubuntu on 23-5-6.
//
#include <string>
#include <netinet/in.h>

class Config {
public:
    std::string rootDir;  // 项目目录，如果特殊设置则为cwd

    std::string getCurrentWorkingDirectory();
    std::string ninjaDir;     // 当前工作目录

    std::string get_ipv4_address(size_t address_size = INET_ADDRSTRLEN);
    std::string ipv4_address;

    bool read_yaml();
    std::string masterAddr;

    Config();
};

extern Config sharebuild_config;