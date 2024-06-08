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

#ifndef NINJA_REMOTEEXECUTOR_REMOTESPAWN_H
#define NINJA_REMOTEEXECUTOR_REMOTESPAWN_H

#include <memory>
#include <string>
#include <vector>

struct BuildConfig;
struct Edge;

namespace RemoteExecutor {

struct RemoteSpawn {
  static RemoteSpawn* CreateRemoteSpawn(Edge* edge);
  static bool CanExecuteRemotelly(Edge* edge);

  std::vector<std::string> GetHeaderFiles();
  void ConvertAllPathToRelative();

  static const BuildConfig* config;

  std::string command;
  std::string origin_command;
  std::string rule;
  std::vector<std::string> arguments;
  std::vector<std::string> inputs;
  std::vector<std::string> outputs;

private:
  RemoteSpawn() = default;
};

} // namespace RemoteExecutor

#endif  // NINJA_REMOTEEXECUTOR_REMOTESPAWN_H
