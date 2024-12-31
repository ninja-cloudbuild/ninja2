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

#include "static_file_utils.h"

#include <cstring>
#include <system_error>
#include <fcntl.h>

#include "../util.h"

#if __APPLE__
#define st_mtim st_mtimespec
#endif

namespace RemoteExecutor {

DirentWrapper::DirentWrapper(const int dirfd, const std::string& path)
    : dir_(nullptr), entry_(nullptr), path_(path), fd_(-1), pfd_(-1) {
  fd_ = openat(dirfd, path.c_str(), O_RDONLY | O_DIRECTORY);
  if (fd_ < 0) {
    Error("Error opening directory at path \"%s\" (dirfd=%d)",
          path.c_str(), dirfd);
  }
  dir_ = fdopendir(fd_);
  if (dir_ == nullptr) {
    close(fd_);
    Error("Error opening directory from fd at path \"%s\"", path_.c_str());
  }
  Next();
}

DirentWrapper::DirentWrapper(const int fd, const int p_fd,
                             const std::string& path)
    : entry_(nullptr), path_(path), fd_(fd), pfd_(p_fd) {
  dir_ = fdopendir(fd);
  if (dir_ == nullptr) {
    close(fd);
    Error("Error opening directory from fd at path \"%s\"", path_.c_str());
  }
  Next();
}

DirentWrapper::~DirentWrapper() {
  if (dir_ != nullptr) {
    const int ret_val = closedir(dir_);
    if (ret_val != 0) {
      const int errsv = errno;
      Warning("Error closing directory [%s]: %s",
              path_.c_str(), strerror(errsv));
    }
  }
}

bool DirentWrapper::CurrentEntryIsDirectory() const {
  struct stat statResult;
  bool ret_val = false;
  if (entry_ == nullptr)
    return ret_val;
  if (fstatat(fd_, entry_->d_name, &statResult, AT_SYMLINK_NOFOLLOW) == 0)
    ret_val = S_ISDIR(statResult.st_mode);
  else
    Error("Unable to stat entity \"%s\"", entry_->d_name);
  return ret_val;
}

DirentWrapper DirentWrapper::NextDir() const {
  int next_fd = OpenEntry(O_DIRECTORY);
  if (next_fd == -1)
    Error("Error getting dir from non-directory");
  return DirentWrapper(next_fd, fd(), CurrentEntryPath());
}

int DirentWrapper::OpenEntry(int flag) const {
  if (entry_ == nullptr)
    return -1;
  int fd = openat(dirfd(dir_), entry_->d_name, flag);
  if (fd == -1) {
    Warning("Warning when trying to open fd representing path with openat:"
      " [%s/%s] %s", path_.c_str(), entry_->d_name, strerror(errno));
  }
  return fd;
}

const std::string DirentWrapper::CurrentEntryPath() const {
  if (entry_ == nullptr)
    return "";
  else {
    auto current_path = path_ + "/" + std::string(entry_->d_name);
    return current_path;
  }
}

void DirentWrapper::Next() {
  do {
    errno = 0;
    entry_ = readdir(dir_);
    if (errno != 0 && entry_ == nullptr)
      Error("Error reading from directory \"%s\"", path_.c_str());
  } while (entry_ != nullptr && (strcmp(entry_->d_name, ".") == 0 ||
                                 strcmp(entry_->d_name, "..") == 0));
}

void StaticFileUtils::CreateDirectory(const char* path, mode_t mode) {
  CreateDirectory(AT_FDCWD, path, mode);
}

void StaticFileUtils::CreateDirectory(int dirfd, const char* path,
                                      mode_t mode) {
  // Normalize path first as the parent directory creation logic in
  // `CreateDirectoriesInPath()` can't handle paths with '..' components.
  const std::string normalized_path = NormalizePath(path);
  CreateDirectoriesInPath(dirfd, normalized_path, mode);
}

void StaticFileUtils::CreateDirectoriesInPath(int dirfd,
    const std::string& path, const mode_t mode) {
  // Attempt to create the directory:
  const auto mkdir_status = mkdirat(dirfd, path.c_str(), mode);
  const auto mkdir_error = errno;
  if (mkdir_status == 0 || mkdir_error == EEXIST) {
    return; // Directory was successfully created or already exists, done.
  }
  if (mkdir_error != ENOENT) { // Something went wrong, aborting.
    Fatal("Could not create directory [%s]: %s", path.c_str(),
          strerror(mkdir_error));
  }
  // `mkdir_error == ENOENT` => Some portion of the path does not exist yet.
  // We'll recursively create the parent directory and try again:
  const std::string parent_path = path.substr(0, path.rfind('/'));
  CreateDirectoriesInPath(dirfd, parent_path.c_str(), mode);
  // Now that all the parent directories exist, we create the last directory:
  if (mkdirat(dirfd, path.c_str(), mode) != 0) {
    Fatal("Could not create directory [%s]: %s", path.c_str(),
          strerror(errno));
  }
}

void StaticFileUtils::DeleteDirectory(const char* path) {
  DeleteDirectory(AT_FDCWD, path);
}

void StaticFileUtils::DeleteDirectory(int dirfd, const char* path) {
  DeleteRecursively(dirfd, path, true);
}

bool StaticFileUtils::IsExecutable(const char* path) {
  struct stat statResult;
  if (stat(path, &statResult) == 0)
    return statResult.st_mode & S_IXUSR;
  return false;
}

bool StaticFileUtils::IsExecutable(int fd) {
  struct stat statResult;
  if (fstat(fd, &statResult) == 0)
    return statResult.st_mode & S_IXUSR;
  return false;
}

bool StaticFileUtils::IsSymlink(const char* path){
  struct stat statResult;
  lstat(path,&statResult);
  return S_ISLNK(statResult.st_mode); 
} 

struct stat StaticFileUtils::GetFileStat(int dirfd, const char* path) {
  struct stat statResult;
  if (fstatat(dirfd, path, &statResult, 0) != 0)
    Error("Failed to get file stats at \"%s\"", path);
  return statResult;
}

struct stat StaticFileUtils::GetFileStat(const char *path) {
  struct stat statResult;
  if (stat(path, &statResult) != 0)
    Error("Failed to get file stats at \"%s\"", path);
  return statResult;
}

struct stat StaticFileUtils::GetFileStat(const int fd) {
  struct stat statResult;
  if (fstat(fd, &statResult) != 0)
    Error("Failed to get file stats for file descriptor %d", fd);
  return statResult;
}

std::chrono::system_clock::time_point StaticFileUtils::GetFileMtime(
    const char* path) {
  struct stat statResult = GetFileStat(path);
  return StaticFileUtils::GetMtimeTimepoint(statResult);
}

std::chrono::system_clock::time_point StaticFileUtils::GetFileMtime(
    const int fd) {
  struct stat statResult = GetFileStat(fd);
  return StaticFileUtils::GetMtimeTimepoint(statResult);
}

std::chrono::system_clock::time_point StaticFileUtils::GetMtimeTimepoint(
    struct stat& result) {
  const std::chrono::system_clock::time_point timepoint =
      std::chrono::system_clock::from_time_t(result.st_mtim.tv_sec) +
      std::chrono::microseconds{ result.st_mtim.tv_nsec / 1000 };
  return timepoint;
}

std::string StaticFileUtils::GetFileContents(const char* path) {
  return GetFileContents(AT_FDCWD, path);
}

std::string StaticFileUtils::GetFileContents(int dirfd, const char* path) {
  FileDescriptor fd(openat(dirfd, path, O_RDONLY));
  if (fd.Get() < 0)
    Fatal("Failed to open file \"%s\"", path);
  return GetFileContents(fd.Get());
}

std::string StaticFileUtils::GetFileContents(int fd) {
  struct stat statResult = GetFileStat(fd);
  if (!S_ISREG(statResult.st_mode))
    Fatal("GetFileContents() called on a directory or special file");
  std::string buffer(statResult.st_size, '\0');
  off_t pos = 0;
  while (pos < statResult.st_size) {
    ssize_t n = pread(fd, &buffer[pos], statResult.st_size - pos, pos);
    if (n < 0)
      Fatal("Failed to read file in GetFileContents()");
    else if (n == 0)
      Fatal("Unexpected end of file in GetFileContents()");
    else
      pos += n;
  }
  return buffer;
}

void StaticFileUtils::CopyFile(const char* src_path, const char* dst_path) {
  CopyFile(AT_FDCWD, src_path, AT_FDCWD, dst_path);
}

void StaticFileUtils::CopyFile(int src_dirfd, const char* src_path,
                               int dst_dirfd, const char* dst_path) {
  ssize_t rdsize, wrsize;
  int err = 0;
  const auto mode = StaticFileUtils::GetFileStat(src_dirfd, src_path).st_mode;

  FileDescriptor src(openat(src_dirfd, src_path, O_RDONLY, mode));
  if (src.Get() == -1) {
    err = errno;
    Error("Failed to open file at %s", src_path);
  }
  FileDescriptor dst(openat(dst_dirfd, dst_path,
                            O_WRONLY | O_CREAT | O_TRUNC, 0644));
  if (dst.Get() == -1) {
    err = errno;
    Error("Failed to open file at %s", dst_path);
  }
  if (err == 0) {
    const size_t bufsize = 65536;
    char *buf = new char[bufsize];
    while ((rdsize = read(src.Get(), buf, bufsize)) > 0) {
      wrsize = write(dst.Get(), buf, static_cast<unsigned long>(rdsize));
      if (wrsize != rdsize) {
        err = EIO;
        Error("Failed to write to file at %s", dst_path);
        break;
      }
    }
    delete[] buf;
    if (rdsize == -1) {
      err = errno;
      Error("Failed to read file at %s", src_path);
    }
    if (fchmod(dst.Get(), mode) != 0) {
      err = errno;
      Error("Failed to set mode of file at %s", dst_path);
    }
  }
  if (err != 0) {
    if (unlink(dst_path) != 0) {
      err = errno;
      Error("Failed to remove file at %s", dst_path);
    }
    Fatal("CopyFile failed");
  }
}

void StaticFileUtils::DeleteRecursively(int dirfd, const char* path,
                                        bool delete_root_directory) {
  DirentWrapper root(dirfd, path);
  DirTraversalFnPtr rmdir_func = [dirfd](const char* dir_path, int fd) {
    std::string dir_basename(dir_path);
    if (fd != -1) {
      dir_basename = StaticFileUtils::PathBasename(dir_path);
      if (dir_basename.empty())
        return;
    } else {
      // The root directory has no parent directory file descriptor,
      // but it may still be relative to the specified dirfd.
      fd = dirfd;
    }
    // unlinkat will disregard the file descriptor and call
    // rmdir/unlink on the path depending on the entity
    // type(file/directory).
    //
    // For deletion using the file descriptor, the path must be
    // relative to the directory the file descriptor points to.
    if (unlinkat(fd, dir_basename.c_str(), AT_REMOVEDIR) == -1) {
      Error("Error removing directory \"%s\"", dir_path);
    }
  };
  DirTraversalFnPtr unlink_func = [](const char* path = nullptr, int fd = 0) {
    if (unlinkat(fd, path, 0) == -1)
      Error("Error removing file \"%s\"", path);
  };
  FileDescriptorTraverseAndApply(&root, rmdir_func, unlink_func,
                                 delete_root_directory, true);
}

void StaticFileUtils::FileDescriptorTraverseAndApply(
    DirentWrapper* dir, DirTraversalFnPtr dir_func,
    DirTraversalFnPtr file_func, bool apply_to_root, bool pass_parent_fd) {
  while (dir->Entry() != nullptr) {
    if (dir->CurrentEntryIsDirectory()) {
      auto NextDir = dir->NextDir();
      FileDescriptorTraverseAndApply(&NextDir, dir_func, file_func, true,
                                     pass_parent_fd);
    } else {
      if (file_func != nullptr)
        file_func(dir->Entry()->d_name, dir->fd());
    }
    dir->Next();
  }
  if (apply_to_root && dir_func != nullptr) {
    if (pass_parent_fd)
      dir_func(dir->Path().c_str(), dir->pfd());
    else
      dir_func(dir->Path().c_str(), dir->fd());
  }
}

std::string StaticFileUtils::NormalizePath(const char* path) {
  std::vector<std::string> segments;
  const bool global = path[0] == '/';
  while (path[0] != '\0') {
    const char* slash = strchr(path, '/');
    std::string segment;
    if (slash == nullptr) {
      segment = std::string(path);
    } else {
      segment = std::string(path, size_t(slash - path));
    }
    if (segment == ".." && !segments.empty() && segments.back() != "..") {
      segments.pop_back();
    } else if (global && segment == ".." && segments.empty()) {
      // dot-dot in the root directory refers to the root directory
      // itself and can thus be dropped.
    } else if (segment != "." && segment != "") {
      segments.push_back(segment);
    }
    if (slash == nullptr) {
      break;
    } else {
      path = slash + 1;
    }
  }
  std::string result(global ? "/" : "");
  if (segments.size() > 0) {
    for (const auto &segment : segments)
      result += segment + "/";
    result.pop_back();
  } else if (!global) {
    // The normalized path for the current directory is `.`,
    // not an empty string.
    result = ".";
  }
  return result;
}

std::string StaticFileUtils::PathBasename(const char* path) {
  std::string basename(path);
  if (basename.empty() || basename.size() == 1)
    return "";
  if (basename[basename.size() - 1] == '/')
    basename.pop_back();
  auto pos = basename.rfind('/');
  return (pos == std::string::npos) ? "" : basename.substr(pos + 1);
}

#ifndef _WIN32
constexpr auto kDefaultTempDir = "/tmp";
constexpr auto kDefaultPrefix = "ninja_tmp_";

std::pair<int, std::string> StaticFileUtils::CreateTempFile(
    const char* prefix, const char* dir, mode_t mode) {
  std::string path = dir ? dir : getenv("TMPDIR");
  if (path.empty())
    path = kDefaultTempDir;
  std::string name = prefix ? prefix : kDefaultPrefix;
  std::string full_name = path + "/" + prefix + "XXXXXX";
  int fd = mkstemp(&full_name[0]);
  if (fd >= 0 && mode != 0600)
    fchmod(fd, mode);
  return make_pair(fd, full_name);
}

void StaticFileUtils::DeleteTempFile(std::pair<int, std::string> temp_file) {
  if (temp_file.first >= 0)
    close(temp_file.first);
  unlink(temp_file.second.c_str());
}

int StaticFileUtils::ParentDirectoryLevel(const std::string& path) {
  int currentLevel = 0, lowestLevel = 0;
  auto path_p = path.c_str();
  while (*path_p != 0) {
    auto slash = strchr(path_p, '/');
    if (!slash)
      break;
    const auto segmentLength = slash - path_p;
    if (segmentLength == 0 || (segmentLength == 1 && path_p[0] == '.')) {
      // Empty or dot segments don't change the level.
    } else if (segmentLength == 2 && path_p[0] == '.' && path_p[1] == '.') {
      lowestLevel = std::min(lowestLevel, --currentLevel);
    } else {
      currentLevel++;
    }
    path_p = slash + 1;
  }
  if (strcmp(path_p, "..") == 0) {
    currentLevel--;
    lowestLevel = std::min(lowestLevel, currentLevel);
  }
  return -lowestLevel;
}

std::string StaticFileUtils::LastNSegments(const std::string& path, int n) {
  if (n == 0)
    return {};
  const auto len = path.length();
  auto path_p = path.c_str();
  const char* substr_start = path_p + len - 1;
  unsigned int substr_len = (path_p[len - 1] == '/') ? 0 : 1;
  int slashesSeen = 0;
  while (substr_start != path_p) {
    if (*(substr_start - 1) == '/') {
      slashesSeen++;
      if (slashesSeen == n)
        return std::string(substr_start, substr_len);
    }
    substr_start--;
    substr_len++;
  }
  // The path might only be one segment (no slashes)
  if (slashesSeen == 0 && n == 1)
    return std::string(path_p, len);
  Fatal("Not enough segments in path");
}

bool StaticFileUtils::HasPathPrefix(const std::string& path,
                                    const std::string& prefix) {
  // A path can never have the empty path as a prefix
  if (prefix.empty())
    return false;
  if (path == prefix)
    return true;
  std::string tmpPrefix(prefix);
  if (tmpPrefix.back() != '/')
    tmpPrefix.push_back('/');
  std::string tmpPath(path);
  if (tmpPath.back() != '/')
    tmpPath.push_back('/');
  return tmpPath.substr(0, tmpPrefix.length()) == tmpPrefix;
}

std::string StaticFileUtils::MakePathRelative(const std::string& path,
                                              const std::string& base) {
  if (base.empty() || path.empty() || path.front() != '/')
    return path;
  if (base.front() != '/')
    Fatal("base must be an absolute path or empty: '%s' ]", base.c_str());

  unsigned pos_c = 0;
  unsigned pos_n = pos_c + 1;
  unsigned last_seg_matched = 0;
  while (pos_c < path.length() && path[pos_c] == base[pos_c]) {
    if (pos_n == base.length()) {
      // base is prefix of path, so if the last segment matches, we're done
      if (path.length() == pos_n)
        return path[pos_c] == '/' ? "./" : ".";
      if (path.length() == pos_c + 2 && path[pos_n] == '/')
        return "./";
      if (path[pos_c] == '/')
        return path.substr(pos_n);
      if (path[pos_n] == '/')
        return path.substr(pos_c + 2);
    } else if (path[pos_c] == '/') {
      last_seg_matched = pos_c;
    }
    ++pos_c;
    ++pos_n;
  }

  if (pos_c == path.length() && base[pos_c] == '/') {
    // Path is prefix of base
    if (pos_n == base.length())
      return ".";
    last_seg_matched = pos_c;
    ++pos_c;
    ++pos_n;
  }

  unsigned dotdots_needed = 1;
  while (pos_c < base.length()) {
    if (base[pos_c] == '/' && base[pos_n] != 0)
      ++dotdots_needed;
    ++pos_c;
    ++pos_n;
  }

  std::string result = path;
  result.replace(0, last_seg_matched, dotdots_needed * 3 - 1, '.');
  for (unsigned j = 0; j < dotdots_needed - 1; ++j)
    result[j * 3 + 2] = '/';
  return result;
}
#endif

}  // namespace RemoteExecutor
