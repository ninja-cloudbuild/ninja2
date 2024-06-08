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

#include "remote_process.h"

#include <unistd.h>
#if defined(USE_PPOLL)
#include <poll.h>
#else
#include <sys/select.h>
#endif

#include "build.h"
#include "subprocess.h"
#include "remote_executor/execution_context.h"

static std::atomic_bool stop_token(false);

RemoteProcess::RemoteProcess() : fd_(-1), exit_code_(-1), spawn_(nullptr) {
  context_ = new RemoteExecutor::ExecutionContext;
}

RemoteProcess::~RemoteProcess() {
  delete context_;
  if (spawn_)
    delete spawn_;
  if (fd_ >= 0)
    close(fd_);
}

void RemoteProcess::WorkThread(RemoteExecutor::RemoteSpawn* spawn, int fd,
                               RemoteProcess* rproc) {
  std::vector<std::string> headerfiles = spawn->GetHeaderFiles();
  for (std::size_t i = 0; i < headerfiles.size(); i++)
    spawn->inputs.emplace_back(headerfiles[i]);
  rproc->context_->Execute(fd, spawn, rproc->exit_code_);
}

void RemoteProcess::Start(struct RemoteProcessSet* set) {
  std::string command = spawn_->command;
  if (pipe(pipe_) < 0)
    Fatal("==> pipe: %s", strerror(errno));
  fd_ = pipe_[0];
  context_->SetStopToken(stop_token);
  spawn_->ConvertAllPathToRelative();
  Task task = std::bind(RemoteProcess::WorkThread, spawn_, pipe_[1], this);
  set->thread_pool_->AddTask(task);
}

void RemoteProcess::OnPipeReady() {
  char buf[4 << 10];
  ssize_t len = read(fd_, buf, sizeof(buf));
  if (len > 0) {
    buf_.append(buf, len);
  } else {
    if (len < 0)
      Fatal("read: %s", strerror(errno));
    close(fd_);
    fd_ = -1;
  }
}

ExitStatus RemoteProcess::Finish() {
  return exit_code_ == 0 ? ExitSuccess : ExitFailure;
}

const std::string& RemoteProcess::GetOutput() const {
  return buf_;
}

bool RemoteProcess::Done() const {
  return fd_ == -1;
}

RemoteProcessSet::RemoteProcessSet(int pool_size) {
  thread_pool_ = CreateRemoteBuildThreadPool(pool_size);
}

RemoteProcessSet::~RemoteProcessSet() {
  delete thread_pool_;
}

RemoteProcess* RemoteProcessSet::Add(RemoteExecutor::RemoteSpawn* spawn) {
  RemoteProcess* rproc = new RemoteProcess();
  rproc->spawn_ = spawn;
  rproc->Start(this);
  running_.push_back(rproc);
  return rproc;
}

RemoteProcess* RemoteProcessSet::NextFinished() {
  if (finished_.empty())
    return NULL;
  RemoteProcess* remoteproc = finished_.front();
  finished_.pop();
  return remoteproc;
}

#ifdef USE_PPOLL
bool RemoteProcessSet::DoWork(SubprocessSet* local_set) {
  std::vector<pollfd> fds;
  nfds_t nfds = 0;
  for (std::vector<Subprocess*>::iterator i = local_set->running_.begin();
       i != local_set->running_.end(); ++i) {
    int fd = (*i)->fd_;
    if (fd < 0)
      continue;
    pollfd pfd = { fd, POLLIN | POLLPRI, 0 };
    fds.push_back(pfd);
    ++nfds;
  }
  for (std::vector<RemoteProcess*>::iterator i = running_.begin();
       i != running_.end(); ++i) {
    int fd = (*i)->fd_;
    if (fd < 0)
      continue;
    pollfd pfd = { fd, POLLIN | POLLPRI, 0 };
    fds.push_back(pfd);
    ++nfds;
  }

  local_set->interrupted_ = 0;
  int ret = ppoll(&fds.front(), nfds, NULL, &local_set->old_mask_);
  if (ret == -1) {
    if (errno != EINTR) {
      perror("ninja: ppoll");
      return false;
    }
    if (local_set->IsInterrupted()) {
      stop_token = true;
      return true;
    }
    return false;
  }

  local_set->HandlePendingInterruption();
  if (local_set->IsInterrupted()) {
    stop_token = true;
    return true;
  }

  nfds_t cur_nfd = 0;
  for (std::vector<Subprocess*>::iterator i = local_set->running_.begin();
       i != local_set->running_.end(); ) {
    int fd = (*i)->fd_;
    if (fd < 0)
      continue;
    assert(fd == fds[cur_nfd].fd);
    if (fds[cur_nfd++].revents) {
      (*i)->OnPipeReady();
      if ((*i)->Done()) {
        local_set->finished_.push(*i);
        i = local_set->running_.erase(i);
        continue;
      }
    }
    ++i;
  }
  for (std::vector<RemoteProcess*>::iterator i = running_.begin();
       i != running_.end();) {
    int fd = (*i)->fd_;
    if (fd < 0)
      continue;
    assert(fd == fds[cur_nfd].fd);
    if (fds[cur_nfd++].revents) {
      (*i)->OnPipeReady();
      if ((*i)->Done()) {
        finished_.push(*i);
        i = running_.erase(i);
        continue;
      }
    }
    ++i;
  }
  if (local_set->IsInterrupted()) {
    stop_token = true;
    return true;
  }
  return false;
}
#else // !defined(USE_PPOLL)
bool RemoteProcessSet::DoWork(SubprocessSet* local_set) {
  fd_set set;
  int nfds = 0;
  FD_ZERO(&set);

  for (std::vector<Subprocess*>::iterator i = local_set->running_.begin();
       i != local_set->running_.end(); ++i) {
    int fd = (*i)->fd_;
    if (fd >= 0) {
      FD_SET(fd, &set);
      if (nfds < fd + 1)
        nfds = fd + 1;
    }
  }
  for (std::vector<RemoteProcess*>::iterator i = running_.begin();
       i != running_.end(); ++i) {
    int fd = (*i)->fd_;
    if (fd >= 0) {
      FD_SET(fd, &set);
      if (nfds < fd + 1)
        nfds = fd + 1;
    }
  }

  local_set->interrupted_ = 0;
  int ret = pselect(nfds, &set, 0, 0, 0, &local_set->old_mask_);
  if (ret == -1) {
    if (errno != EINTR) {
      perror("ninja: pselect");
      return false;
    }
    if (local_set->IsInterrupted()) {
      stop_token = true;
      return true;
    }
    return false;
  }

  local_set->HandlePendingInterruption();
  if (local_set->IsInterrupted()) {
    stop_token = true;
    return true;
  }

  for (std::vector<Subprocess*>::iterator i = local_set->running_.begin();
       i != local_set->running_.end(); ) {
    int fd = (*i)->fd_;
    if (fd >= 0 && FD_ISSET(fd, &set)) {
      (*i)->OnPipeReady();
      if ((*i)->Done()) {
        local_set->finished_.push(*i);
        i = local_set->running_.erase(i);
        continue;
      }
    }
    ++i;
  }
  for (std::vector<RemoteProcess*>::iterator i = running_.begin();
       i != running_.end();) {
    int fd = (*i)->fd_;
    if (fd >= 0 && FD_ISSET(fd, &set)) {
      (*i)->OnPipeReady();
      if ((*i)->Done()) {
        finished_.push(*i);
        i = running_.erase(i);
        continue;
      }
    }
    ++i;
  }

  if (local_set->IsInterrupted()) {
    stop_token = true;
    return true;
  }
  return false;
}
#endif // !defined(USE_PPOLL)

void RemoteProcessSet::Clear() {
  for (std::vector<RemoteProcess*>::iterator i = running_.begin();
       i != running_.end(); ++i)
    delete *i;
  running_.clear();
}

bool RemoteProcessSet::ThreadPoolAlreadyFull() const {
  return thread_pool_->HasWaitingTask();
}
