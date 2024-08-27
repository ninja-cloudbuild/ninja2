//
// Created by ubuntu on 23-5-6.
//
#pragma once
#include <string>
#include <map>
#include <netinet/in.h>
#include "util.h"

struct RBEConfig {
    // RBE server config
    bool cloud_build;                                       // enable remote api cloud build
    std::string grpc_url;                                   // remote api master addr 

    bool share_build;                                       // enable p2p share build
    std::string master_addr;                                // p2p master addr
    
    std::string self_ipv4_address;                          // self ipv4 address


    // project config
    std::string cwd;                                        // current working directory(i.e. ninjaDir). eg: ~/proj/build
    std::string project_root;                               // project root directory. eg: ~/proj
    std::map<std::string, std::string> rbe_properties;      // remote build execution properties

    RBEConfig();

    bool load_server_config(const std::string& filename);
    void init_proj_config(const std::string& project_root_path);

    std::string get_cwd();
    std::string get_ipv4_address(size_t address_size = INET_ADDRSTRLEN);
    friend std::ostream& operator<<(std::ostream& os, const RBEConfig& config);
};

extern RBEConfig g_rbe_config;