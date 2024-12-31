//
//  Copyright 2024 Mengning Software All rights reserved.
//
#pragma once
#include <string>
#include <map>
#include <netinet/in.h>
#include "util.h"
#include "build.h"

// load /etc/ninja2.conf
bool load_config_file(BuildConfig &config);
// Loads the devcontainer configuration from the specified project root  
void load_devcontainer_config(const std::string& project_root, BuildConfig &config);

void load_command_file(const std::string& project_root, BuildConfig &config);
