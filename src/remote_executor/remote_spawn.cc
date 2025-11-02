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

#include "remote_spawn.h"

#include "compile_command_parser.h"
#include "../build.h"
#include "../graph.h"
#include "../remote_process.h"

namespace RemoteExecutor {

const BuildConfig* RemoteSpawn::config = nullptr;

RemoteSpawn* RemoteSpawn::CreateRemoteSpawn(Edge* edge) {
  RemoteSpawn* spawn = new RemoteSpawn(edge, CanExecuteRemotelly(edge));
  std::string command = edge->EvaluateCommand();
  std::string rule = edge->rule().name();
  spawn->origin_command = command;
  spawn->command = command;
  spawn->arguments = std::move(SplitStrings(command));

  for (std::size_t i = 0; i < edge->inputs_.size(); i++) {
    auto& cur_input = edge->inputs_[i]->path();
    if (!edge->is_order_only(i))
      spawn->inputs.emplace_back(cur_input);
  }
  for (auto out_node : edge->outputs_)
    spawn->outputs.emplace_back(out_node->path());
  return spawn;
}

std::vector<std::string> RemoteSpawn::GetHeaderFiles() {
  std::vector<std::string> res;
  std::vector<std::string> cmd = SplitStrings(command);
  const auto result = CompileCommandParser::ParseCommand(cmd);
  if (!result.is_compiler_command) {
    return res;
  }
  auto deps_products = result.deps_command_products;
  for (auto& dep : deps_products) {
    outputs.emplace_back(dep);
  }
  auto headers = CompileCommandParser::ParseHeaders(result);
  for (auto& header : headers) {
    res.emplace_back(header);
  }
  if (res.empty()) {
    Warning("command [%s] get headerfiles fail", origin_command.c_str());
  }
  CleanCommand();
  return res;
}

void RemoteSpawn::CleanCommand() {
  std::string command = origin_command;
  if(command.find("\\") == std::string::npos) {
    return;
  }
  std::string cleaned;
  int len = command.size();
  cleaned.reserve(command.size());
  for (size_t i = 0; i < len; ++i) {
    if (command[i] == '\\') {
      if (i + 1 < len && (command[i + 1] == ' ')) {
        ++i;
      } else if(i + 3 < len && command[i+1] == '\\' && command[i+2] == '\\' && command[i+3] == '"') {
        i+=3;
      }
      continue;
    }
    cleaned += command[i];
  }
  command = cleaned;
  this->origin_command = command;
  this->command = command;
  this->arguments = std::move(SplitStrings(command));
}

bool RemoteSpawn::CanExecuteRemotelly(Edge* edge) {
  if (!edge)
    return false;
  std::string command = edge->EvaluateCommand();
  std::string rule = edge->rule().name();
  
  if (config->rbe_config.local_only_rules.find(rule) != config->rbe_config.local_only_rules.end()){
    return false;
  }
  for (auto &cmd : config->rbe_config.fuzzy_rules){
    if(command.find(cmd)!=std::string::npos || rule.find(cmd)!=std::string::npos){
      return false;
    }
  }
  for (auto& it : CompileCommandParser::SupportedRemoteExecuteCommands())
    if (command.find(it) != std::string::npos)
      return true;
  return false;
}

bool RemoteSpawn::CanCacheRemotelly(Edge* edge) {
  if (!edge)
    return false;
  std::string command = edge->EvaluateCommand();
  std::string rule = edge->rule().name();

  if (config->rbe_config.local_only_rules.find(rule) != config->rbe_config.local_only_rules.end()){
    return false;
  }
  for (auto &cmd:config->rbe_config.fuzzy_rules){
    if (command.find(cmd)!=std::string::npos || rule.find(cmd)!=std::string::npos){
      return false;
    }
  }
  for (auto& it : CompileCommandParser::SupportedRemoteExecuteCommands())
    if (command.find(it) != std::string::npos)
      return true;
  
  return false;
}

enum OptType { relaPath, absPath, symbol, option, toolPath, errPath = -1 };

OptType OptionType(const std::string& option) {
  if (option.empty())
    return OptType::errPath;
  if (option[0] == '/') {
    const auto& prj_root = RemoteSpawn::config->rbe_config.project_root;
    return StaticFileUtils::HasPathPrefix(option, prj_root)
        ? OptType::absPath : OptType::toolPath;
  }
  if ((option[0] >= 'A' && option[0] <= 'Z') ||
      (option[0] >= 'a' && option[0] <= 'z') || (option[0] == '_')) {
    return OptType::relaPath;
  }
  if ((option[0] == '-') &&
      (option[1] == 'I' || option[1] == 'L' || option[1] == 'l')) {
    return OptType::option;
  }
  return OptType::symbol;
}

void RemoteSpawn::ConvertAllPathToRelative() {
  for (auto& input : inputs) {
    if (OptionType(input) == OptType::absPath)
      input = StaticFileUtils::MakePathRelative(input, config->rbe_config.cwd);
  }
  for (auto& output : outputs) {
    if (OptionType(output) == OptType::absPath)
      output = StaticFileUtils::MakePathRelative(output, config->rbe_config.cwd);
  }
  for (auto& arg : arguments) {
    auto opt = OptionType(arg);
    if (opt == OptType::absPath)
      arg = StaticFileUtils::MakePathRelative(arg, config->rbe_config.cwd);
    else if (opt == OptType::option)
      arg = arg.substr(0,2) +
            StaticFileUtils::MakePathRelative(arg.substr(2), config->rbe_config.cwd);
  }
  command = MergeStrings(arguments);
}

} //namespace RemoteExecutor
