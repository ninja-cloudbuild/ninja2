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

#include "execution_context.h"

#include <fstream>
#include <sstream>

#include "google/protobuf/util/time_util.h"

#include "remote_execution_client.h"
#include "remote_spawn.h"
#include "static_file_utils.h"
#include "../build.h"
#include "../remote_process.h"
#include "../util.h"
#include "../subprocess.h"

namespace RemoteExecutor {

Digest HashFile(int fd) { return CASHash::Hash(fd); }

File::File(const char* path,
           const std::vector<std::string>& capture_properties)
    : File(path, HashFile, capture_properties) {}

File::File(const char* path, const FileDigestFunction& digest_func,
           const std::vector<std::string>& capture_properties)
    : File(AT_FDCWD, path, digest_func, capture_properties) {}

File::File(int dirfd, const char* path,
           const FileDigestFunction& digest_func,
           const std::vector<std::string>& capture_properties) {
  const FileDescriptor fd(openat(dirfd, path, O_RDONLY));
  if (fd.Get() < 0)
    Fatal("Failed to open path \"%s\"", path);
  Init(fd.Get(), digest_func, capture_properties);
}

void File::Init(int fd, const FileDigestFunction& digest_func,
                const std::vector<std::string>& capture_properties) {
  executable = StaticFileUtils::IsExecutable(fd);
  digest = digest_func(fd);
  for (const auto &property : capture_properties) {
    if (property == "mtime") {
      mtime = StaticFileUtils::GetFileMtime(fd);
      mtime_set = true;
    }
  }
}

const google::protobuf::Timestamp
MakeTimestamp(const std::chrono::system_clock::time_point mtime) {
  google::protobuf::int64 usec =
      std::chrono::duration_cast<std::chrono::microseconds>(
          mtime.time_since_epoch()).count();
  return google::protobuf::util::TimeUtil::MicrosecondsToTimestamp(usec);
}

FileNode File::ToFileNode(const std::string& name) const {
  FileNode result;
  result.set_name(name);
  *result.mutable_digest() = digest;
  result.set_is_executable(executable);
  if (mtime_set) {
    auto node_properties = result.mutable_node_properties();
    node_properties->mutable_mtime()->CopyFrom(MakeTimestamp(mtime));
  }
  return result;
}

OutputFile File::ToOutputFile(const std::string& path) const {
  OutputFile result;
  result.set_path(path);
  *result.mutable_digest() = digest;
  result.set_is_executable(executable);
  if (mtime_set) {
    auto node_properties = result.mutable_node_properties();
    node_properties->mutable_mtime()->CopyFrom(MakeTimestamp(mtime));
  }
  return result;
}


void NestedDirectory::Add(const File& file, const char* relative_path) {
  const char *slash = strchr(relative_path, '/');
  if (slash) {
    const std::string subdir_key(relative_path,
                                static_cast<size_t>(slash - relative_path));
    if (subdir_key.empty())
      Add(file, slash + 1);
    else
      (*subdirs)[subdir_key].Add(file, slash + 1);
  } else {
    files[std::string(relative_path)] = file;
  }
}

void NestedDirectory::AddDirectory(const char* directory) {
  // A forward slash by itself is not a valid input directory
  if (strcmp(directory, "/") == 0)
    return;
  const char* slash = strchr(directory, '/');
  if (slash) {
    const std::string subdir_key(directory, slash - directory);
    if (subdir_key.empty())
      AddDirectory(slash + 1);
    else
      (*subdirs)[subdir_key].AddDirectory(slash + 1);
  } else {
    if ((*subdirs).count(directory) == 0) {
      (*subdirs)[directory] = NestedDirectory();
    }
  }
}

inline Digest MakeDigest(const std::string& blob) {
  return CASHash::Hash(blob);
}

inline Digest MakeDigest(const google::protobuf::MessageLite& message) {
  return MakeDigest(message.SerializeAsString());
}

Digest NestedDirectory::ToDigest(DigestStringMap *digest_map) const {
  // The 'files' and 'subdirs' maps make sure everything is sorted by
  // name thus the iterators will iterate lexicographically
  Directory dir_msg;
  for (const auto& file : files) {
    *dir_msg.add_files() = file.second.ToFileNode(file.first);
  }
  for (const auto& symlink : symlinks) {
    SymlinkNode symlink_node;
    symlink_node.set_name(symlink.first);
    symlink_node.set_target(symlink.second);
    *dir_msg.add_symlinks() = symlink_node;
  }
  for (const auto& subdir : *subdirs) {
    auto subdir_node = dir_msg.add_directories();
    subdir_node->set_name(subdir.first);
    auto subdirDigest = subdir.second.ToDigest(digest_map);
    *subdir_node->mutable_digest() = subdirDigest;
  }
  auto blob = dir_msg.SerializeAsString();
  auto digest = MakeDigest(blob);
  if (digest_map != nullptr)
    (*digest_map)[digest] = blob;
  return digest;
}

void BuildMerkleTree(const std::set<std::string>& deps, const std::string& cwd,
    NestedDirectory* nested_dir, DigestStringMap* digest_files) {
  for (auto& dep : deps) {
    std::string merklePath(dep);
    if (merklePath[0] != '/' && !cwd.empty())
      merklePath = cwd + "/" + merklePath;
    merklePath = StaticFileUtils::NormalizePath(merklePath.c_str());
    if (merklePath[0] == '/' &&
        !StaticFileUtils::HasPathPrefix(merklePath,
                                        RemoteSpawn::config->rbe_config.project_root)) {
      continue;
    }
    File file(dep.c_str());
    nested_dir->Add(file, merklePath.c_str());
    (*digest_files)[file.digest] = dep;
  }
}

std::string CommonAncestorPath(const std::set<std::string> &deps,
                               const std::set<std::string> &products,
                               const std::string &work_dir) {
  int parentsNeeded = 0;
  for (const auto& dep : deps) {
    parentsNeeded = std::max(parentsNeeded,
                             StaticFileUtils::ParentDirectoryLevel(dep));
  }
  for (const auto& product : products) {
    parentsNeeded = std::max(parentsNeeded,
        StaticFileUtils::ParentDirectoryLevel(product));
  }
  return StaticFileUtils::LastNSegments(work_dir, parentsNeeded);
}

build::bazel::remote::execution::v2::Command GenerateCommandProto(
    const std::vector<std::string>& command,
    const std::set<std::string>& outputs,
    const std::string& work_dir,
    const std::map<std::string, std::string> rbe_properties) {
  Command cmd_proto;
  for (const auto& arg : command)
    cmd_proto.add_arguments(arg);
  // REAPI v2.1 deprecated the `output_files` and `output_directories` fields
  // of the `Command` message, replacing them with `output_paths`.
  constexpr bool output_paths_supported = kREAPIVersion >= 2.1;
  for (const auto& file : outputs) {
    if (output_paths_supported) {
      cmd_proto.add_output_paths(file);
    } else {
      cmd_proto.add_output_files(file);
    }
  }
  cmd_proto.set_working_directory(work_dir);
  
  Platform* platform = cmd_proto.mutable_platform();
  for (const auto& prop_kv : rbe_properties) {
    auto* new_property = platform->add_properties();
    new_property->set_name(prop_kv.first);
    new_property->set_value(prop_kv.second);
  }
  return cmd_proto;
}

Action BuildAction(RemoteSpawn* spawn, const std::string& cwd,
    DigestStringMap* blobs, DigestStringMap* digest_files,
    std::set<std::string>& products) {
  std::set<std::string> deps;
  for (auto i : spawn->inputs)
    deps.insert(i);
  for (auto i : spawn->outputs)
    products.insert(i);

  NestedDirectory nested_dir;
  auto cmd_work_dir = CommonAncestorPath(deps, products, cwd);
  BuildMerkleTree(deps, cmd_work_dir, &nested_dir, digest_files);
  if (!cmd_work_dir.empty()) {
    cmd_work_dir = StaticFileUtils::NormalizePath(cmd_work_dir.c_str());
    nested_dir.AddDirectory(cmd_work_dir.c_str());
  }
  const auto dir_digest = nested_dir.ToDigest(blobs);
  const auto cmd_proto = GenerateCommandProto(spawn->arguments, products,
                                                 cmd_work_dir, spawn->config->rbe_config.rbe_properties);
  const auto cmd_digest = MakeDigest(cmd_proto);
  (*blobs)[cmd_digest] = cmd_proto.SerializeAsString();

  Action action;
  action.mutable_command_digest()->CopyFrom(cmd_digest);
  action.mutable_input_root_digest()->CopyFrom(dir_digest);
  action.set_do_not_cache(false); //spawn->config->action_uncachable
  // REAPI v2.2 allows setting the platform property list in the `Action`
  // message, which allows servers to immediately read it without having to
  // dereference the corresponding `Command`.
  if (kREAPIVersion >= 2.2) {
    action.mutable_platform()->CopyFrom(cmd_proto.platform());
  }
  return action;
}

bool BuildActionOutputs(RemoteSpawn* spawn, const std::string& cwd,
    DigestStringMap* blobs, DigestStringMap* digest_files,
    ActionResult *result) {
  std::set<std::string> products;
  std::set<std::string> deps;
  for (auto i : spawn->outputs)
    products.insert(i);
   for (auto i : spawn->inputs)
    deps.insert(i);
  //NestedDirectory nested_dir;
  auto cmd_work_dir = CommonAncestorPath(deps, products, cwd);
  // BuildMerkleTree(deps, cmd_work_dir, &nested_dir, digest_files);
  //file exist?
  // if(!StaticFileUtils::WaitForPipeClose(spawn->local_pipe_fd_))
  //     Fatal("local output produce failed!");
  for (auto& product : products) {
    if(product.find(".o.d")!=std::string::npos){
      continue;
    }
    std::string merklePath(product);
    if (merklePath[0] == '/' &&
        !StaticFileUtils::HasPathPrefix(merklePath,
                                        RemoteSpawn::config->rbe_config.project_root)) {
      continue;
    }
    char currentPath[PATH_MAX];
    getcwd(currentPath, sizeof(currentPath));
    strcat(currentPath,"/");
    strcat(currentPath,product.c_str());
    if(StaticFileUtils::IsSymlink(currentPath)){
      // return false;
       char target[PATH_MAX];
       ssize_t len=readlink(currentPath,target,sizeof(target)-1);
       std::string real_target;
       if(len==-1){
          Error("Error read symbol link %s",product.c_str());
       }else{
         target[len] = '\0';
         real_target=target;
       }
       OutputSymlink outsymlink;
       outsymlink.set_path(product);
       outsymlink.set_target(real_target);
       result->add_output_symlinks()->CopyFrom(outsymlink);
    }else{
      File file(product.c_str());
     (*digest_files)[file.digest] = product;
   
      auto  outputF=file.ToOutputFile(std::string(merklePath.c_str()));
      result->add_output_files()->CopyFrom(outputF);
    }
  }
  
  const auto cmd_proto = GenerateCommandProto(spawn->arguments, products,
                                                 cmd_work_dir,spawn->config->rbe_config.rbe_properties);
  const auto cmd_digest = MakeDigest(cmd_proto);
  (*blobs)[cmd_digest] = cmd_proto.SerializeAsString();

  result->set_exit_code(0);
  return true;
}

constexpr auto kMetadataToolName = "Ninja_Remote";
constexpr auto kMetadataToolVersion = "UnRelease";

std::string HostName() {
  int host_length = 100;
  char hostname[host_length + 1];
  hostname[host_length] = '\0';
  const int error = gethostname(hostname, sizeof(hostname) - 1);
  return error ? "" : hostname;
}

std::string ToolInvocationID() {
  return HostName() + ":" + std::to_string(getppid());
}

ConnectionOptions GetConnectOptions() {
  // For now, Server & CAS_Server & ActionCache_Server use the same
  ConnectionOptions option;
  option.SetUrl(RemoteExecutor::RemoteSpawn::config->rbe_config.grpc_url);
  option.SetInstanceName("");
  option.SetRetryLimit(0);
  option.SetRetryDelay(100);
  option.SetRequestTimeout(0);
  return option;
}

void ExecutionContext::Execute(int fd, RemoteExecutor::RemoteSpawn* spawn,
                              int& exit_code) {
  const std::string cwd = spawn->config->rbe_config.cwd;
  DigestStringMap blobs, digest_files;
  std::set<std::string> products;
  const auto action = BuildAction(spawn, cwd, &blobs, &digest_files, products);
  const auto action_digest = MakeDigest(action);

  auto connect_opt = GetConnectOptions();
  GRPCClient cas_grpc;
  cas_grpc.Init(connect_opt);
  GRPCClient exec_grpc;
  exec_grpc.Init(connect_opt);
  GRPCClient ac_grpc;
  ac_grpc.Init(connect_opt);

  cas_grpc.SetToolDetails(kMetadataToolName, kMetadataToolVersion);
  cas_grpc.SetRequestMetadata(toString(action_digest), ToolInvocationID());
  exec_grpc.SetToolDetails(kMetadataToolName, kMetadataToolVersion);
  exec_grpc.SetRequestMetadata(toString(action_digest), ToolInvocationID());
  ac_grpc.SetToolDetails(kMetadataToolName, kMetadataToolVersion);
  ac_grpc.SetRequestMetadata(toString(action_digest), ToolInvocationID());

  CASClient cas_client(&cas_grpc, DigestFunction_Value_SHA256);
  cas_client.Init();
  RemoteExecutionClient re_client(&exec_grpc, &ac_grpc);
  re_client.Init();

  bool cached = false;
  ActionResult result;
  cached = re_client.FetchFromActionCache(action_digest, products, &result);
   Warning("Execute locally CMD: %s,is it cached? %d", spawn->command.c_str(),cached);
  if(cached) 
    exit_code = result.exit_code();
  // Info("action cached is %d", cached);

  // 本地和远程共享一套 cache
  // 对于不能远程执行的任务，只能本地执行
  // 但如果一个任务即可本地执行又可远程执行，优先远程执行。认为远程资源充足无上限。
  // spawn->work.remote = false; 时，只能本地执行
  // spawn->work.remote = false;
  if (!cached && !spawn->can_remote) {
    // Execute locally
    SubprocessSet subprocset;
    Subprocess* subproc = subprocset.Add(spawn->command);
    if (!subproc) {
      Fatal("Error while `Execute locally and Update to ActionCache`"); 
    }    
    //wait for local run finished
    subproc = NULL;
    while ((subproc = subprocset.NextFinished()) == NULL) {
      bool interrupted = subprocset.DoWork();
      if (interrupted)
        return ;
    }
    if (subproc->Finish() == ExitSuccess) {
      Warning("Execute locally: %s", subproc->GetOutput().c_str());
    }
    delete subproc;

    DigestStringMap outblobs, outputs_digest_files;
     bool ret = BuildActionOutputs(spawn, cwd, &outblobs, &outputs_digest_files, &result);
      if(!ret){
        exit_code = 0;
        close(fd);
        return;
      }
    outblobs[action_digest] = action.SerializeAsString();
    try {
      // 上传文件至 CAS cache
      UploadResources(&cas_client, outblobs, outputs_digest_files);
    } catch (const std::exception& e) {
      Fatal("Error while uploading resources to CAS at \"%s\": %s",
            spawn->config->rbe_config.grpc_url.c_str(), e.what()); //CASServer
    }
    try {
      // 更新 action cache
      cached = re_client.UpdateToActionCache(action_digest, &result);
    } catch (const std::exception& e) {
      Error("Error while querying action cache at \"%s\": %s",
          spawn->config->rbe_config.grpc_url.c_str(), e.what()); //ActionServer
    }
    exit_code = 0;
    close(fd);
    return;  
  }

  // remote 执行
  if (!cached && spawn->can_remote) {
      blobs[action_digest] = action.SerializeAsString();
      UploadResources(&cas_client, blobs, digest_files);
      result = re_client.ExecuteAction(action_digest, *stop_requested_, false);
  }

  const int _exit_code = result.exit_code();
  exit_code = _exit_code;
    if ( exit_code != 0) {
        close(fd);
        return;
      }
  if (_exit_code == 0 && result.output_files_size() == 0 && products.size() != 0)
    Fatal("Action produced none of the of the expected output_files");

  const auto postfix = spawn->rule + "_" + GetRandomHexString(8);
  // Add stdout and stderr as output files if they aren't embedded.
  // Allows download of stdout, stderr and output files in a single batch.
  const std::string stdout_fn = ".remote_execute_stdout_" + postfix;
  const std::string stderr_fn = ".remote_execute_stderr_" + postfix;
  const std::string prefix = spawn->config->rbe_config.cwd + "/.remote_stdout_stderr/";
  if (result.has_stdout_digest()) {
    OutputFile output;
    output.mutable_digest()->CopyFrom(result.stdout_digest());
    output.set_path(prefix + stdout_fn);
    *result.add_output_files() = output;
  }
  if (result.has_stderr_digest()) {
    OutputFile output;
    output.mutable_digest()->CopyFrom(result.stderr_digest());
    output.set_path(prefix + stderr_fn);
    *result.add_output_files() = output;
  }

  auto root = spawn->config->rbe_config.cwd.c_str();
  FileDescriptor root_dirfd(open(root, O_RDONLY | O_DIRECTORY));
  if (root_dirfd.Get() < 0)
    Fatal("Error opening directory at path \"%s\".", root);
  re_client.DownloadOutputs(&cas_client, result, root_dirfd.Get());

  ssize_t len;
  if (result.has_stdout_digest()) {
    std::ifstream file(prefix + stdout_fn);
    std::stringstream output;
    output << file.rdbuf();
    len = write(fd, output.str().c_str(), output.str().length());
    unlink(stdout_fn.c_str());
  } else {
    std::string buf = result.stdout_raw();
    len = write(fd, buf.c_str(), buf.length());
  }
  if (result.has_stderr_digest()) {
    std::ifstream file(prefix + stderr_fn);
    std::stringstream output;
    output << file.rdbuf();
    len = write(fd, output.str().c_str(), output.str().length());
    unlink(stderr_fn.c_str());
  } else {
    std::string buf = result.stderr_raw();
    len = write(fd, buf.c_str(), buf.length());
  }
  if (len < 0)
    Warning("Wrote command output to fd failed.");
  close(fd);
}

void ExecutionContext::UploadResources(CASClient* client,
    const DigestStringMap& blobs, const DigestStringMap& digest_files) {
  std::vector<Digest> digests_upload;
  std::vector<Digest> missing_digests;
  for (const auto& i : blobs)
    digests_upload.push_back(i.first);
  for (const auto& i : digest_files)
    digests_upload.push_back(i.first);

  missing_digests = client->FindMissingBlobs(digests_upload);
  std::vector<CASClient::UploadRequest> upload_requests;
  upload_requests.reserve(missing_digests.size());
  for (const auto& digest : missing_digests) {
    // Finding the data in one of the source maps:
    if (blobs.count(digest)) {
      upload_requests.emplace_back(digest, blobs.at(digest));
    } else if (digest_files.count(digest)) {
      const auto path = digest_files.at(digest);
      upload_requests.push_back(
          CASClient::UploadRequest::FromPath(digest, path));
    } else {
      Fatal("FindMissingBlobs returned non-existent digest");
    }
  }
  client->UploadBlobs(upload_requests);
}

}  // namespace RemoteExecutor
