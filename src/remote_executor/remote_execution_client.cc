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

#include "remote_execution_client.h"

#include <iomanip>
#include <random>
#include <sstream>

#include "google/rpc/code.pb.h"

#include "static_file_utils.h"
#include "../util.h"

#define POLL_WAIT std::chrono::seconds(1)

namespace RemoteExecutor {

std::string GetRandomHexString(int width) {
  std::random_device random_device;
  std::uniform_int_distribution<uint32_t> random_distribution;
  std::stringstream stream;
  stream << std::hex << std::setw(width) << std::setfill('0')
         << random_distribution(random_device);
  return stream.str();
}

void RemoteExecutionClient::Init() {
  if (exec_grpc_) {
    exec_stub_ = Execution::NewStub(exec_grpc_->Channel());
    op_stub_ = Operations::NewStub(exec_grpc_->Channel());
  }
  if (ac_grpc_) {
    ac_stub_ = ActionCache::NewStub(ac_grpc_->Channel());
  }
}

ActionResult GetActionResult(const Operation &operation) {
  if (!operation.done())
    Fatal("Called getActionResult on an unfinished Operation");
  else if (operation.has_error())
    Fatal("Operation failed: %s", operation.error().message());
  else if (!operation.response().Is<ExecuteResponse>())
    Fatal("Server returned invalid Operation result");

  ExecuteResponse executeResponse;
  if (!operation.response().UnpackTo(&executeResponse))
    Fatal("Operation response unpacking failed");
  const auto executeStatus = executeResponse.status();
  if (executeStatus.code() != google::rpc::Code::OK)
    Fatal("Execution failed: %s", executeStatus.message());
  const ActionResult actionResult = executeResponse.result();
  if (actionResult.exit_code() != 0 && !executeResponse.message().empty())
    Info("Remote execution message: %s", executeResponse.message().c_str());
  return actionResult;
}

void CancelOperation(const std::string &op_name,
                     Operations::StubInterface* op_sub,
                     GRPCClient* exec_grpc) {
  CancelOperationRequest cancel_req;
  cancel_req.set_name(op_name);
  auto cancel_lambda = [&](grpc::ClientContext &context) {
    google::protobuf::Empty empty;
    return op_sub->CancelOperation(&context, cancel_req, &empty);
  };
  exec_grpc->IssueRequest(cancel_lambda, "Operations.CancelOperation()", nullptr);
  Info("Cancelled job %s", op_name);
}

using ReaderPointer = grpc::ClientAsyncReaderInterface<Operation>*;

bool ReadOperation(grpc::CompletionQueue* cq, ReaderPointer reader,
                   Operation* op, const std::atomic_bool& stop_req,
                   Operations::StubInterface* op_sub,
                   GRPCClient* exec_grpc) {
  bool logged = false;
  reader->Read(op, nullptr);
  while (true) {
    void *tag;
    bool ok;
    const auto deadline = std::chrono::system_clock::now() + POLL_WAIT;
    const auto status = cq->AsyncNext(&tag, &ok, deadline);
    if (status == grpc::CompletionQueue::GOT_EVENT) {
      if (!ok)
        break; // No more messages in the stream
      if (!logged && !op->name().empty())
        logged = true;
      if (op->done())
        break;
      // Previous read is complete, start read of next message
      reader->Read(op, nullptr);
    } else if (stop_req) {
      cq->Shutdown();
      Warning("Cancelling job, operation name: %s", op->name().c_str());
      // Cancel the operation if the execution service gave it a name
      if (!op->name().empty())
        CancelOperation(op->name(), op_sub, exec_grpc);
      return false; // Indicate cancellation
    }
  }
  return true;
}

bool RemoteExecutionClient::FetchFromActionCache(const Digest &action_digest,
    const std::set<std::string> &outputs, ActionResult *result) {
  if (!ac_stub_)
    Fatal("ActionCache Stub not Configured");
  GetActionResultRequest action_req;
  action_req.set_instance_name(ac_grpc_->InstanceName());
  action_req.set_inline_stdout(true);
  action_req.set_inline_stderr(true);
  for (const auto &o : outputs) {
    action_req.add_inline_output_files(o);
  }
  *action_req.mutable_action_digest() = action_digest;

  ActionResult action_result;
  bool found = false;
  auto result_lambda = [&](grpc::ClientContext &context) {
    const grpc::Status status = ac_stub_->GetActionResult(
        &context, action_req, &action_result);
    if (status.ok())
      found = true;
    else if (status.error_code() == grpc::StatusCode::NOT_FOUND)
      return grpc::Status::OK; // Shouldn't throw an exception
    return status;
  };
  ac_grpc_->IssueRequest(result_lambda, "ActionCache.GetActionResult()",
                         nullptr);
  if (found && result != nullptr)
    *result = action_result;
  return found;
}

bool RemoteExecutionClient::UpdateToActionCache(const Digest &action_digest, ActionResult *result) {
  if (!ac_stub_)
    Fatal("ActionCache Stub not Configured");
  UpdateActionResultRequest action_req;
  action_req.set_instance_name(ac_grpc_->InstanceName());
  action_req.mutable_action_digest()->CopyFrom(action_digest);
  *action_req.mutable_action_result() = *result;

  ActionResult update_result;
  bool update =false;
  auto result_lambda = [&](grpc::ClientContext &context) {
    const grpc::Status status = ac_stub_->UpdateActionResult(
        &context, action_req,&update_result);
        if(status.ok())
         update=true;
     if (status.error_code() == grpc::StatusCode::ALREADY_EXISTS){
      update=true;
      return grpc::Status::OK;
      } // Shouldn't throw an exception
    return status;
  };
  ac_grpc_->IssueRequest(result_lambda, "ActionCache.UpdateActionResult()",
                         nullptr);
   return update; 
}

ActionResult RemoteExecutionClient::ExecuteAction(const Digest &action_digest,
    const std::atomic_bool &stop_requested, bool skip_cache) {
  if (!(exec_stub_ && op_stub_))
    Fatal("Execution Stubs not Configured");
  ExecuteRequest execute_request;
  execute_request.set_instance_name(exec_grpc_->InstanceName());
  *execute_request.mutable_action_digest() = action_digest;
  execute_request.set_skip_cache_lookup(skip_cache);

  Operation operation;
  auto execute_lambda = [&](grpc::ClientContext &context) {
    grpc::CompletionQueue cq;
    void *tag;
    bool ok;
    auto reader_ptr = exec_stub_->AsyncExecute(&context, execute_request,
                                               &cq, nullptr);
    if (!cq.Next(&tag, &ok) || !ok) {
      return grpc::Status(grpc::UNAVAILABLE,
                          "Failed to send Execute request to the server");
    }
    if (!ReadOperation(&cq, reader_ptr.get(), &operation, stop_requested,
                       op_stub_.get(), exec_grpc_)) {
      return grpc::Status(grpc::CANCELLED, "Operation was cancelled");
    }

    grpc::Status status;
    reader_ptr->Finish(&status, nullptr);
    if (!cq.Next(&tag, &ok) || !ok) {
      return grpc::Status(grpc::UNAVAILABLE,
                          "Failed to finish Execute client stream");
    }
    return status;
  };
  exec_grpc_->IssueRequest(execute_lambda, "Execution.Execute()", nullptr);

  if (!operation.done())
    Fatal("Server closed stream before Operation finished");
  return GetActionResult(operation);
}

void CheckDownloadBlobsResult(const CASClient::DownloadBlobsResult &results) {
  std::vector<std::string> missing_blobs;
  for (const auto &result : results) {
    const auto &status = result.second.first;
    if (status.code() == grpc::StatusCode::NOT_FOUND) {
      missing_blobs.push_back(result.first);
    } else if (status.code() != grpc::StatusCode::OK) {
      Fatal("Failed to download output blob %s: [%s] %s", result.first,
            std::to_string(status.code()), status.message());
    }
  }
  if (!missing_blobs.empty()) {
    std::ostringstream error;
    error << missing_blobs.size()
          << " output blobs missing from ActionResult: ";
    bool first = true;
    for (const auto& hash : missing_blobs) {
      if (!first) {
        error << ", ";
      }
      error << hash;
      first = false;
    }
    Fatal(error.str().c_str());
  }
}

Digest AddDirectoryToMap(std::unordered_map<Digest, Directory> *map,
                         const Directory &directory) {
  const auto digest = CASHash::Hash(directory.SerializeAsString());
  (*map)[digest] = directory;
  return digest;
}

void CreateParentDirectory(int dirfd, const std::string &path) {
  const auto pos = path.rfind('/');
  if (pos != std::string::npos) {
    const std::string parent_path = path.substr(0, pos);
    StaticFileUtils::CreateDirectory(dirfd, parent_path.c_str());
  }
}

void StageDownloadedFile(int dirfd, const std::string &path,
    const Digest &digest, bool is_executable, int temp_dirfd,
    const CASClient::DownloadBlobsResult &downloaded_files,
    const std::unordered_set<Digest> &duplicate_file_digests) {
  auto temp_path = downloaded_files.at(digest.hash()).second;
  if (duplicate_file_digests.find(digest) != duplicate_file_digests.end()) {
    // Digest is used by multiple output files, create a copy
    auto temp_copy_path = temp_path + GetRandomHexString(8);
    StaticFileUtils::CopyFile(temp_dirfd, temp_path.c_str(), temp_dirfd,
                              temp_copy_path.c_str());
    temp_path = temp_copy_path;
  }
  mode_t mode = 0644;
  if (is_executable) {
    mode |= S_IXUSR | S_IXGRP | S_IXOTH;
  }
  if (fchmodat(temp_dirfd, temp_path.c_str(), mode, 0) < 0) {
    Fatal("Failed to set file mode of downloaded file");
  }
  if (renameat(temp_dirfd, temp_path.c_str(), dirfd, path.c_str()) < 0) {
    Fatal("Failed to move downloaded file to final location: %s",
          path.c_str());
  }
}

void StageDownloadedDirectory(
    int dirfd, const std::string &path, const Digest &dir_digest,
    int temp_dirfd,
    const std::unordered_map<Digest, Directory> &digest_directory_map,
    const CASClient::DownloadBlobsResult &downloaded_files,
    const std::unordered_set<Digest> &duplicate_file_digests) {
  const auto &directory = digest_directory_map.at(dir_digest);
  StaticFileUtils::CreateDirectory(dirfd, path.c_str());
  FileDescriptor current_dirfd(openat(dirfd, path.c_str(),
                                      O_RDONLY | O_DIRECTORY));
  if (current_dirfd.Get() < 0)
    Fatal("Failed to open newly created subdirectory");
  for (const auto &file_node : directory.files()) {
    StageDownloadedFile(current_dirfd.Get(), file_node.name(),
                        file_node.digest(), file_node.is_executable(),
                        temp_dirfd, downloaded_files,
                        duplicate_file_digests);
  }
  for (const auto &dir_node : directory.directories()) {
    StageDownloadedDirectory(current_dirfd.Get(), dir_node.name(),
                             dir_node.digest(), temp_dirfd,
                             digest_directory_map, downloaded_files,
                             duplicate_file_digests);
  }
  for (const auto &symlink_node : directory.symlinks()) {
    if (symlinkat(symlink_node.target().c_str(), current_dirfd.Get(),
                  symlink_node.name().c_str()) < 0) {
      Fatal("Failed to create symlink");
    }
  }
}

void RemoteExecutionClient::DownloadOutputs(
    CASClient *cas_client, const ActionResult &action_result,
    int dirfd) {
  std::unordered_set<Digest> tree_digests;
  for (const auto &dir : action_result.output_directories()) {
    tree_digests.insert(dir.tree_digest());
  }

  const auto downloaded_trees = cas_client->DownloadBlobs(
    std::vector<Digest>(tree_digests.cbegin(), tree_digests.cend()));
  CheckDownloadBlobsResult(downloaded_trees);

  std::unordered_set<Digest> file_digests, duplicate_file_digests;
  std::unordered_map<Digest, Directory> digest_directory_map;
  // Map from Digest of Tree to Digest of root directory of that tree
  std::unordered_map<Digest, Digest> tree_digest_root_digest_map;

  for (const auto &file : action_result.output_files()) {
    const bool inserted = file_digests.insert(file.digest()).second;
    if (!inserted) {
      duplicate_file_digests.insert(file.digest());
    }
  }
  for (const auto &dir : action_result.output_directories()) {
    Tree tree;
    const auto serialized_tree =
      downloaded_trees.at(dir.tree_digest().hash()).second;
    if (!tree.ParseFromString(serialized_tree)) {
      Fatal("Could not deserialize downloaded Tree");
    }
    const auto root_digest =
      AddDirectoryToMap(&digest_directory_map, tree.root());
    tree_digest_root_digest_map[dir.tree_digest()] = root_digest;

    for (const auto &tree_child : tree.children()) {
      AddDirectoryToMap(&digest_directory_map, tree_child);
    }
  }

  for (const auto &digest_dir_iter : digest_directory_map) {
    // All directories are already in digest_directory_map, there is
    // no need for recursion.
    for (const auto &file_node : digest_dir_iter.second.files()) {
      const bool inserted =
        file_digests.insert(file_node.digest()).second;
      if (!inserted) {
        duplicate_file_digests.insert(file_node.digest());
      }
    }
  }

  const auto tempDirectoryName = ".reclient-" + GetRandomHexString(8);
  if (mkdirat(dirfd, tempDirectoryName.c_str(), 0700) < 0) {
    Fatal("Failed to create temporary directory");
  }
  FileDescriptor temp_dirfd(
    openat(dirfd, tempDirectoryName.c_str(), O_RDONLY | O_DIRECTORY));
  if (temp_dirfd.Get() == -1) {
    Fatal("Failed to open temporary directory");
  }

  const auto downloaded_files = cas_client->DownloadBlobsToDirectory(
      std::vector<Digest>(file_digests.cbegin(), file_digests.cend()),
      temp_dirfd.Get());
  CheckDownloadBlobsResult(downloaded_files);

  for (const auto &file : action_result.output_files()) {
    CreateParentDirectory(dirfd, file.path());
    StageDownloadedFile(dirfd, file.path(), file.digest(),
                        file.is_executable(), temp_dirfd.Get(),
                        downloaded_files, duplicate_file_digests);
  }
  for (const auto &symlink : action_result.output_symlinks()) {
    CreateParentDirectory(dirfd, symlink.path());
    if (symlinkat(symlink.target().c_str(), dirfd,
                  symlink.path().c_str()) < 0) {
      Fatal("Failed to create symlink");
    }
  }
  for (const auto &dir : action_result.output_directories()) {
    const auto dir_digest =
        tree_digest_root_digest_map[dir.tree_digest()];
    CreateParentDirectory(dirfd, dir.path());
    StageDownloadedDirectory(dirfd, dir.path(), dir_digest,
        temp_dirfd.Get(), digest_directory_map,
        downloaded_files, duplicate_file_digests);
  }
  StaticFileUtils::DeleteDirectory(dirfd, tempDirectoryName.c_str());
}

} // namespace RemoteExecutor
