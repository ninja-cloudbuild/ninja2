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

#include "thread_pool.h"

#include <assert.h>

#include <atomic>
#include <condition_variable>
#include <queue>
#include <thread>

#include "metrics.h"
#include "util.h"

struct ThreadPoolImpl : ThreadPool {
  ThreadPoolImpl(int num_threads);
  virtual ~ThreadPoolImpl();
  void RunTasks(std::vector<std::function<void()>>&& tasks) override;

private:
  void WorkerThreadLoop();

  struct Batch {
    Batch(size_t task_count, std::vector<std::function<void()>>&& tasks)
        : task_count(task_count), tasks(std::move(tasks)) {}

    const size_t task_count;
    std::atomic<size_t> next_task_idx { 0 };
    std::atomic<size_t> tasks_completed { 0 };
    std::vector<std::function<void()>> tasks;
    bool completed = false;
  };

  std::mutex mutex_;
  std::shared_ptr<Batch> current_batch_;
  bool shutting_down_ = false;

  std::condition_variable worker_cv_;
  std::condition_variable main_cv_;

  std::vector<std::thread> threads_;
};

ThreadPoolImpl::ThreadPoolImpl(int num_threads) {
  if (num_threads <= 1) {
    // On a single-core machine (or with "-d nothreads"), don't start any
    // threads.
    return;
  }

  threads_.resize(num_threads);
  for (int i = 0; i < num_threads; ++i) {
    threads_[i] = std::thread([this] {
      WorkerThreadLoop();
    });
  }
}

ThreadPoolImpl::~ThreadPoolImpl() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    shutting_down_ = true;
  }
  worker_cv_.notify_all();
  for (auto& thread : threads_) {
    thread.join();
  }
}

void ThreadPoolImpl::RunTasks(std::vector<std::function<void()>>&& tasks) {
  // Sometimes it's better to run the tasks on the caller's thread:
  //  - When parsing the manifest tree, only one file is parsed at a time, so if
  //    a manifest tree has many small files, the overhead of dispatching each
  //    file to a worker thread can be substantial.
  //  - When ninja runs on a single-core machine, or with "-d nothreads", there
  //    are no worker threads in the pool.
  const size_t task_count = tasks.size();
  if (threads_.empty() || task_count <= 1) {
    for (auto& task : tasks) {
      task();
    }
    return;
  }

  std::shared_ptr<Batch> batch = std::make_shared<Batch>(task_count,
                                                         std::move(tasks));

  {
    std::lock_guard<std::mutex> lock(mutex_);
    assert(current_batch_.get() == nullptr &&
           "the thread pool isn't intended for reentrant or concurrent use");
    current_batch_ = batch;
  }

  worker_cv_.notify_all();

  {
    std::unique_lock<std::mutex> lock(mutex_);
    main_cv_.wait(lock, [batch]() { return batch->completed; });
    current_batch_ = {};
  }

  // The client's std::function might do interesting work when it's destructed
  // (e.g. destruct a lambda's captured state), so destruct the functions before
  // returning. It's safe to modify the batch's tasks vector because there are
  // no more tasks to run. The Batch itself is freed at an unpredictable time.
  batch->tasks.clear();
}

void ThreadPoolImpl::WorkerThreadLoop() {
  while (true) {
    // A shared_ptr object isn't thread-safe itself, but its control block is
    // thread-safe. This worker thread must lock the mutex before checking the
    // active batch.
    std::shared_ptr<Batch> batch;

    {
      // Wait until either:
      //  - There is an active batch with at least one task remaining.
      //  - The thread pool is shutting down.
      std::unique_lock<std::mutex> lock(mutex_);
      worker_cv_.wait(lock, [this, &batch]() {
        auto b = current_batch_;
        if (b && b->next_task_idx.load() < b->task_count) {
          batch = b;
          return true;
        }
        return shutting_down_;
      });
      if (shutting_down_) {
        return;
      }
    }

    while (true) {
      // Try to start another task in this batch. Atomically load and increment
      // the next task index.
      size_t idx = batch->next_task_idx++;
      if (idx >= batch->task_count) {
        // The fetched next-task-index is expected to exceed the total number of
        // tasks as the threads finish the batch. Return to the main loop. Some
        // other thread will finish the batch, if it hasn't already.
        break;
      }

      batch->tasks[idx]();

      // Atomically increment the number of completed tasks. Exactly one worker
      // thread should notice that the completed task count equals the total
      // number of tasks, and that worker thread signals the main thread.
      if (++batch->tasks_completed >= batch->task_count) {
        assert(batch->tasks_completed.load() == batch->task_count &&
               "BatchThreadPool::Batch completion count exceeded task count");

        // Every task is completed, so mark the batch done and wake up the main
        // thread.
        {
          std::lock_guard<std::mutex> lock(mutex_);

          batch->completed = true;
        }
        main_cv_.notify_one();
        break;
      }
    }
  }
}

static int g_num_threads = 1;

void SetThreadPoolThreadCount(int num_threads) {
  g_num_threads = std::max(num_threads, 1);
}

int GetOptimalThreadPoolJobCount() {
  if (g_num_threads > 1) {
    // Magic constant: when splitting work into tasks for the thread pool, try
    // to create a fixed number of tasks per thread in the pool.
    return g_num_threads * 2;
  } else {
    // If there are no worker threads, then multiple tasks aren't useful.
    // Returning 1 will disable manifest and log file splitting.
    return 1;
  }
}

std::unique_ptr<ThreadPool> CreateThreadPool() {
  return std::unique_ptr<ThreadPool>(new ThreadPoolImpl(g_num_threads));
}

struct RBThreadPoolImpl : RemoteBuildThreadPool {
  RBThreadPoolImpl(int num_threads);
  virtual ~RBThreadPoolImpl();
  void AddTask(std::function<void()> task) override;
  bool HasWaitingTask() override;

private:
  void WorkerThreadLoop();

  std::queue<std::function<void()>> waiting_tasks_;
  bool shutting_down_ = false;

  std::mutex mutex_;
  std::condition_variable worker_cv_;
  std::vector<std::thread> threads_;
};

RBThreadPoolImpl::RBThreadPoolImpl(int num_threads) {
  threads_.resize(num_threads);
  for (int i = 0; i < num_threads; ++i) {
    threads_[i] = std::thread([this] {
      WorkerThreadLoop();
    });
  }
}

RBThreadPoolImpl::~RBThreadPoolImpl() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    shutting_down_ = true;
  }
  worker_cv_.notify_all();
  for (auto& thread : threads_)
    thread.join();
}

void RBThreadPoolImpl::AddTask(std::function<void()> task) {
  bool need_notify = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    need_notify = waiting_tasks_.empty();
    waiting_tasks_.push(task);
  }
  if (need_notify)
    worker_cv_.notify_all();
}

bool RBThreadPoolImpl::HasWaitingTask() {
  return !waiting_tasks_.empty();
}

void RBThreadPoolImpl::WorkerThreadLoop() {
  while (true) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      worker_cv_.wait(lock, [this]() {
        return shutting_down_ || !waiting_tasks_.empty();
      });
      if (shutting_down_)
        return;
    }
    while (true) {
      std::function<void()> task;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (waiting_tasks_.empty())
          break;
        task = waiting_tasks_.front();
        waiting_tasks_.pop();
      }
      task();
    }
  }
}

static int g_num_remote_threads = 0;
static RemoteBuildThreadPool* g_remote_thread_pool = nullptr;

RemoteBuildThreadPool* CreateRemoteBuildThreadPool(int num_threads) {
  if (g_remote_thread_pool)
    return g_remote_thread_pool;
  g_num_remote_threads = std::max(num_threads, 1);
  g_remote_thread_pool = new RBThreadPoolImpl(g_num_remote_threads);
  return g_remote_thread_pool;
}
