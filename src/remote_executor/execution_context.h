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

#ifndef NINJA_REMOTEEXECUTOR_EXECUTIONCONTEXT_H
#define NINJA_REMOTEEXECUTOR_EXECUTIONCONTEXT_H

#include "cas_client.h"

namespace RemoteExecutor {

using DigestStringMap = std::unordered_map<Digest, std::string>;
using FileDigestFunction = std::function<Digest(int fd)>;

struct File {
  Digest digest;
  bool executable { false };
  bool mtime_set { false };
  std::chrono::system_clock::time_point mtime;

  File(){};
  File(const char *path,
       const std::vector<std::string>& capture_properties = {});
  File(const char* path, const FileDigestFunction& digest_func,
       const std::vector<std::string>& capture_properties = {});
  File(int dirfd, const char *path, const FileDigestFunction& digest_func,
       const std::vector<std::string>& capture_properties = {});

  FileNode ToFileNode(const std::string &name) const;
  OutputFile ToOutputFile(const std::string &name) const;

private:
  void Init(int fd, const FileDigestFunction& digest_func,
            const std::vector<std::string>& capture_properties = {});
};

struct NestedDirectory {
  // Use sorted maps to keep subdirectories & files ordered by name
  using SubdirMap = std::map<std::string, NestedDirectory>;
  std::unique_ptr<SubdirMap> subdirs;
  std::map<std::string, File> files;
  std::map<std::string, std::string> symlinks;

  NestedDirectory() : subdirs(new SubdirMap){};

  void Add(const File& file, const char* relative_path);
  void AddDirectory(const char* directory);
  Digest ToDigest(DigestStringMap* digest_map = nullptr) const;
};

struct RemoteSpawn;

class ExecutionContext {
public:
  void Execute(int fd, RemoteSpawn* spawn, int& exit_code);
  void UploadResources(CASClient* client, const DigestStringMap& blobs,
                       const DigestStringMap& digest_to_filepaths);
  void SetStopToken(const std::atomic_bool& stop_requested) {
    stop_requested_ = &stop_requested;
  }

private:
  const std::atomic_bool* stop_requested_ = nullptr;
};

}  // namespace RemoteExecutor

#endif  // NINJA_REMOTEEXECUTOR_EXECUTIONCONTEXT_H
