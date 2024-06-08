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

#ifndef NINJA_REMOTEPROCESS_H_
#define NINJA_REMOTEPROCESS_H_

#include <memory>
#include <queue>

#include "exit_status.h"
#include "thread_pool.h"

#include "remote_executor/remote_spawn.h"

constexpr auto kREAPIVersion = 2.0;

using Task = std::function<void()>;

namespace RemoteExecutor {
  class ExecutionContext;
}

struct RemoteProcess {
  ~RemoteProcess();
  ExitStatus Finish();
  bool Done() const;
  const std::string& GetOutput() const;

private:
  RemoteProcess();
  void Start(struct RemoteProcessSet* set);
  void OnPipeReady();

  static void WorkThread(RemoteExecutor::RemoteSpawn* spawn, int fd,
                         RemoteProcess* rproc);

  int fd_;
  int pipe_[2];
  int exit_code_;
  std::string buf_;
  RemoteExecutor::RemoteSpawn* spawn_;
  RemoteExecutor::ExecutionContext* context_;

  friend struct RemoteProcessSet;
};

struct SubprocessSet;

struct RemoteProcessSet {
  RemoteProcessSet(int pool_size);
  ~RemoteProcessSet();

  RemoteProcess* Add(RemoteExecutor::RemoteSpawn* spawn);
  bool DoWork(SubprocessSet* local_set);
  RemoteProcess* NextFinished();
  void Clear();
  bool ThreadPoolAlreadyFull() const;

  std::vector<RemoteProcess*> running_;
  std::queue<RemoteProcess*> finished_;

  RemoteBuildThreadPool* thread_pool_;
};

#endif // NINJA_REMOTEPROCESS_H_
