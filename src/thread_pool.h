// Copyright 2018 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef NINJA_THREAD_POOL_H_
#define NINJA_THREAD_POOL_H_

#include <functional>
#include <memory>
#include <vector>

#include "util.h"

/// This class maintains a pool of threads and distributes tasks across them.
/// It only executes one batch at a time. It is mostly lock-free, and threads
/// retrieve tasks using atomic size_t fields.
struct ThreadPool {
  virtual ~ThreadPool() {}
  virtual void RunTasks(std::vector<std::function<void()>>&& tasks) = 0;
};

/// Configure the number of worker threads a thread pool creates. A value of 1
/// or lower disables thread creation.
void SetThreadPoolThreadCount(int num_threads);

/// Get the optimal number of jobs to split work into, given the size of the
/// thread pool.
int GetOptimalThreadPoolJobCount();

/// Create a new thread pool. Destructing the thread pool joins the threads,
/// which is important for ensuring that no worker threads are running when
/// ninja forks child processes or handles signals.
std::unique_ptr<ThreadPool> CreateThreadPool();

/// Another thread pool for running remote build tasks. It can add new tasks
/// while executing existing ones.
struct RemoteBuildThreadPool {
  virtual ~RemoteBuildThreadPool() {}
  virtual void AddTask(std::function<void()> task) = 0;
  virtual bool HasWaitingTask() = 0;
};

/// Create or get a singleton thread pool for remote build, it should be freed
/// manually when all tasks finished.
RemoteBuildThreadPool* CreateRemoteBuildThreadPool(int num_threads);

#endif  // NINJA_THREAD_POOL_H_
