/****************************************************************************
 * Copyright (c) CloudBuild Team. 2023. All rights reserved.
 * Licensed under GNU Affero General Public License v3 (AGPL-3.0) .
 * You can use this software according to the terms and conditions of the
 * AGPL-3.0.
 * You may obtain a copy of AGPL-3.0 at:
 *     https://www.gnu.org/licenses/agpl-3.0.txt
 * 
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the AGPL-3.0 for more details.
 ****************************************************************************/

#ifndef NINJA_REMOTEEXECUTOR_COMPILECOMMANDPARSER_H
#define NINJA_REMOTEEXECUTOR_COMPILECOMMANDPARSER_H

#include <list>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "static_file_utils.h"

namespace RemoteExecutor {

struct CompileCommandParser {
  struct ParseResult {
    ParseResult() = default;
    ~ParseResult() {
      if (aix_deps_file.first >= 0)
        StaticFileUtils::DeleteTempFile(aix_deps_file);
    }

    bool is_compiler_command { false };
    bool is_md_options { false };
    bool produces_sun_make_rules { false };
    bool contains_unsupported_options { false };
    std::string compiler;
    std::list<std::string> original_command;
    std::vector<std::string> default_deps_command;
    std::vector<std::string> pre_processor_options;
    std::vector<std::string> deps_command;
    std::set<std::string> command_products;
    std::set<std::string> deps_command_products;
    std::pair<int, std::string> aix_deps_file { -1, "" };
  };

  static ParseResult ParseCommand(const std::vector<std::string>& command);
  static std::set<std::string> ParseHeaders(const ParseResult& result);
  static const std::set<std::string>& SupportedRemoteExecuteCommands();
  static const std::set<std::string>& UnSupportedRemoteExecuteRules();
  static const std::set<std::string>& UnSupportedRemoteExecuteCommands();
};

} //namespace RemoteExecutor

#endif // NINJA_REMOTEEXECUTOR_COMPILECOMMANDPARSER_H
