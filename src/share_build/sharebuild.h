#pragma once

#include <iostream>
#include <string>

#include "proxy_service_client.h"

bool InitShareBuildEnv(const std::string& proxy_service_address,
                       const std::string& ninja_host,
                       const std::string& ninja_build_dir,
                       const std::string& root_dir,
                       const std::string& container_image);

bool ClearShareBuildEnv(const std::string& proxy_service_address,
                        const std::string& ninja_host,
                        const std::string& ninja_build_dir,
                        const std::string& root_dir);

std::string ShareExecute(const std::string& proxy_service_address,
                         const std::string& ninja_host,
                         const std::string& ninja_build_dir,
                         const std::string& root_dir,
                         const std::string& cmd_id,
                         const std::string& cmd);