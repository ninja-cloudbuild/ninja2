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

#ifndef NINJA_REMOTEEXECUTOR_STATICFILEUTILS_H
#define NINJA_REMOTEEXECUTOR_STATICFILEUTILS_H

#include <chrono>
#include <functional>
#include <string>

#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>

namespace RemoteExecutor {

class DirentWrapper final {
public:
  explicit DirentWrapper(int dirfd, const std::string& path);
  ~DirentWrapper();

  bool CurrentEntryIsDirectory() const;
  int OpenEntry(int flag) const;
  DirentWrapper NextDir() const;
  void Next();

  const dirent* Entry() const { return entry_; }
  int fd() const { return fd_; }
  int pfd() const { return pfd_; }
  const std::string Path() const { return path_; }
  const std::string CurrentEntryPath() const;

private:
  explicit DirentWrapper(const int fd, const int p_fd,
                         const std::string& path);
  DIR* dir_;
  dirent* entry_;
  std::string path_;
  int fd_;
  int pfd_;
};

struct StaticFileUtils {
  static void CreateDirectory(int dirfd, const char* path, mode_t mode = 0777);
  static void CreateDirectory(const char* path, mode_t mode = 0777);

  static void DeleteDirectory(const char* path);
  static void DeleteDirectory(int dirfd, const char* path);

  static bool IsExecutable(const char* path);
  static bool IsExecutable(int fd);

  static bool IsSymlink(const char* path);

  static std::chrono::system_clock::time_point GetFileMtime(const char* path);
  static std::chrono::system_clock::time_point GetFileMtime(const int fd);

  static std::string GetFileContents(const char* path);
  static std::string GetFileContents(int dirfd, const char* path);
  static std::string GetFileContents(int fd);

  static std::string NormalizePath(const char* path);

  static std::string PathBasename(const char* path);

  static void CopyFile(const char* src_path, const char* dst_path);
  static void CopyFile(int src_dirfd, const char* src_path, int dst_dirfd,
                       const char* dst_path);

  using DirTraversalFnPtr = std::function<void(const char* path, int fd)>;
  static void FileDescriptorTraverseAndApply(
      DirentWrapper* dir, DirTraversalFnPtr dir_func = nullptr,
      DirTraversalFnPtr file_func = nullptr, bool apply_to_root = false,
      bool pass_parent_fd = false);

#ifndef _WIN32
  static std::pair<int, std::string> CreateTempFile(
      const char* prefix = nullptr, const char* dir = nullptr,
      mode_t mode = 0600);
  static void DeleteTempFile(std::pair<int, std::string> temp_file);

  /// Return the number of levels of parent directory needed to follow the
  /// given path. For example, "a/b/c.txt" has zero parent directory levels,
  /// "a/../../b.txt" has one, and "../.." has two.
  static int ParentDirectoryLevel(const std::string& path);

  /// Return a string containing the last N segments of the given path,
  /// without a trailing slash.
  static std::string LastNSegments(const std::string& path, int n);

  static bool HasPathPrefix(const std::string& path, const std::string& prefix);
  static std::string MakePathRelative(const std::string& path,
                                      const std::string& base);
#endif

 private:
  static struct stat GetFileStat(const int fd);
  static struct stat GetFileStat(const char* path);
  static struct stat GetFileStat(int dirfd, const char* path);

  static std::chrono::system_clock::time_point GetMtimeTimepoint(
      struct stat& result);

  static void DeleteRecursively(int dirfd, const char* path,
                                bool delete_parent_directory);

  static void CreateDirectoriesInPath(int dirfd, const std::string& path,
                                      const mode_t mode);
};

class FileDescriptor final {
 public:
  FileDescriptor() : fd_(-1), close_(false) {}
  explicit FileDescriptor(int fd, bool close = true) : fd_(fd), close_(close) {}
  ~FileDescriptor() {
    if (fd_ >= 0 && close_)
      close(fd_);
  }

  FileDescriptor(FileDescriptor&& other)
      : fd_(other.fd_), close_(other.close_) {
    other.fd_ = -1;
  }
  FileDescriptor& operator=(FileDescriptor&& other) {
    if (this != &other) {
      this->~FileDescriptor();
      fd_ = other.fd_;
      close_ = other.close_;
      other.fd_ = -1;
    }
    return *this;
  }

  int Get() const { return fd_; }

 private:
  int fd_;
  bool close_;
};

}  // namespace RemoteExecutor

#endif  // NINJA_REMOTEEXECUTOR_STATICFILEUTILS_H
