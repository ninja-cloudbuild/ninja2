#pragma once

#include <iostream>
#include <string>

#include "proxy_service_client.h"
#include "../build.h"

bool InitShareBuildEnv(const ProjectConfig& rbe_config);

bool ClearShareBuildEnv(const ProjectConfig& rbe_config);

std::string ShareExecute(const ProjectConfig& rbe_config, 
                         const std::string& cmd_id,
                         const std::string& cmd_content);