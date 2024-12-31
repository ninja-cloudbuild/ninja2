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

bool load_config_file(BuildConfig &config) {
  std::string config_file = "/etc/ninja2.conf";
    try {
        YAML::Node ninja2_conf = YAML::LoadFile(config_file);
        config.cloud_run = ninja2_conf["cloudbuild"].as<bool>(false);
        if(config.cloud_run && ninja2_conf["grpc_url"]){
          config.rbe_config.grpc_url = ninja2_conf["grpc_url"].as<std::string>("");
          if (config.rbe_config.grpc_url.compare(0, 7, "grpc://") != 0) {  
                Fatal("invalid grpc url in /etc/ninja2.conf");  
          }  
        }else{
          config.cloud_run = false;
          config.share_run = ninja2_conf["sharebuild"].as<bool>(false);
        }
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
        std::cerr << "Error: Unable to open devcontainer configuration file: " << devcontainer_path << std::endl;  
        return;  // 提早返回以避免进一步处理  
    }  

    // Create a JSON value to hold the parsed data  
    Json::Value root;  
    try {  
        file >> root; // 尝试读取 JSON 数据  
    } catch (const std::exception& e) {  
        std::cerr << "Error: Failed to parse JSON from devcontainer file: " << e.what() << std::endl;  
        return; // 提早返回以避免使用未解析的数据  
    }  

    // Retrieve the image key and construct the container image string  
    std::string image = root.get("image", "").asString();  
    if (!image.empty()) {  
        config.rbe_config.rbe_properties["container-image"] = DOCKER_PREFIX + image;  
        config.rbe_config.rbe_properties["workload-isolation-type"] = "docker";  
    }  
} 

const std::string COMMANDFILE ="/command_cloudbuild.yml";

void load_command_file(const std::string& project_root, BuildConfig &config){
   std::string commandFilePath=project_root+COMMANDFILE;
    std::ifstream file(commandFilePath);  
    YAML::Node  command_set;
    if (file.is_open()){
      command_set = YAML::LoadFile(commandFilePath);
        } else {
            std::cout << "YAML file not found,no filter command.\n";
            return;
        }
     if (command_set) {
        if (command_set["commands"]["local_only"]) {
            for (const auto& cmd : command_set["commands"]["local_only"]) {
                config.rbe_config.local_only_rules.insert(cmd.as<std::string>());
            }
        }
        if (command_set["commands"]["remote_no_cache"]) {
            for (const auto& cmd : command_set["commands"]["remote_no_cache"]) {
                 config.rbe_config.remote_no_cache_rules.insert(cmd.as<std::string>());
            }
        }
         if (command_set["commands"]["fuzzy_rule"]) {
            for (const auto& cmd : command_set["commands"]["fuzzy_rule"]) {
                 config.rbe_config.fuzzy_rules.insert(cmd.as<std::string>());
            }
        }

    }
}