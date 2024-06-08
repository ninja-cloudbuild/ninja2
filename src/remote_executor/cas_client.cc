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

#include "cas_client.h"

#include <iomanip>
#include <sstream>
#include <uuid/uuid.h>

#include "static_file_utils.h"
#include "../util.h"

namespace RemoteExecutor {

constexpr size_t kHashBufferSizeBytes = 1024 * 64;

Digest CASHash::Hash(int fd) {
  return DigestGenerator(DigestFunction_Value_SHA256).Hash(fd);
}

Digest CASHash::Hash(const std::string& str) {
  return DigestGenerator(DigestFunction_Value_SHA256).Hash(str);
}

DigestContext::DigestContext() {
  context_ = EVP_MD_CTX_create();
  if (!context_)
    Fatal("Error creating `EVP_MD_CTX` context struct");
}

DigestContext::~DigestContext() {
  EVP_MD_CTX_destroy(context_);
}

void DigestContext::Init(const EVP_MD* digest_funct_struct) {
  if (EVP_DigestInit_ex(context_, digest_funct_struct, nullptr) == 0)
    Fatal("EVP_DigestInit_ex() failed.");
}

void DigestContext::Update(const char* data, size_t data_size) {
  if (finalized_)
    Fatal("Cannot update finalized digest");
  if (EVP_DigestUpdate(context_, data, data_size) == 0)
    Fatal("EVP_DigestUpdate() failed.");
  data_size_ += data_size;
}

Digest DigestContext::FinalizeDigest() {
  if (finalized_)
    Fatal("Digest already finalized");
  unsigned char hash_buffer[EVP_MAX_MD_SIZE];
  unsigned int message_length;
  if (EVP_DigestFinal_ex(context_, hash_buffer, &message_length) == 0)
    Fatal("EVP_DigestFinal_ex() failed.");
  finalized_ = true;
  const std::string hash = HashToHex(hash_buffer, message_length);
  Digest digest;
  digest.set_hash(hash);
  digest.set_size_bytes(static_cast<google::protobuf::int64>(data_size_));
  return digest;
}

std::string DigestContext::HashToHex(const unsigned char* hash_buffer,
                                     unsigned int hash_size) {
  std::ostringstream ss;
  for (unsigned int i = 0; i < hash_size; i++) {
    ss << std::hex << std::setw(2) << std::setfill('0')
       << static_cast<int>(hash_buffer[i]);
  }
  return ss.str();
}

DigestGenerator::DigestGenerator(DigestFunction_Value digest_function)
    : digest_func_(digest_function),
      digest_func_struct_(GetDigestFunctionStruct(digest_function)) {}

Digest DigestGenerator::Hash(const std::string& data) const {
  auto digest_context = CreateDigestContext();
  digest_context.Update(data.c_str(), data.size());
  return digest_context.FinalizeDigest();
}

Digest DigestGenerator::Hash(int fd) const {
  auto digest_context = CreateDigestContext();
  // Reading file in chunks and computing the hash incrementally:
  const auto update_function =
      [&digest_context](const char *buffer, size_t data_size) {
        digest_context.Update(buffer, data_size);
      };
  ProcessFile(fd, update_function);
  return digest_context.FinalizeDigest();
}

DigestContext DigestGenerator::CreateDigestContext() const {
  DigestContext context;
  context.Init(digest_func_struct_);
  return context;
}

const EVP_MD *DigestGenerator::GetDigestFunctionStruct(
    DigestFunction_Value digest_function_value) {
  switch (digest_function_value) {
    case DigestFunction_Value_MD5: return EVP_md5();
    case DigestFunction_Value_SHA1: return EVP_sha1();
    case DigestFunction_Value_SHA256: return EVP_sha256();
    case DigestFunction_Value_SHA384: return EVP_sha384();
    case DigestFunction_Value_SHA512: return EVP_sha512();
    default: Fatal("Digest function value not supported: %d",
                   digest_function_value);
  }
}

int64_t DigestGenerator::ProcessFile(int fd,
    const IncrementalUpdateFunc& update_func) {
  std::array<char, kHashBufferSizeBytes> buffer;
  int64_t total_bytes_read = 0;
  lseek(fd, 0, SEEK_SET);
  ssize_t bytes_read;
  while ((bytes_read = read(fd, buffer.data(), buffer.size())) > 0) {
    update_func(buffer.data(), static_cast<size_t>(bytes_read));
    total_bytes_read += bytes_read;
  }
  if (bytes_read == -1)
    Fatal("Error in read on file descriptor %d", fd);
  return total_bytes_read;
}

// Maximum number of bytes that can be sent in a single gRPC message.
// The default limit for gRPC messages is 4 MiB.
// Limit payload to 1 MiB to leave sufficient headroom for metadata.
constexpr size_t kBytestreamChunkSizeBytes = 1024 * 1024;
constexpr size_t kMaxMetadataSize = 1 << 16;

void CASClient::Init() {
  auto channel = grpc_client_->Channel();
  bytestream_client_ = ByteStream::NewStub(channel);
  cas_client_ = ContentAddressableStorage::NewStub(channel);
  std::unique_ptr<Capabilities::Stub> capabilities_client;
  local_cas_client_ = LocalContentAddressableStorage::NewStub(channel);
  max_batch_total_size_ =
      GRPC_DEFAULT_MAX_RECV_MESSAGE_LENGTH - kMaxMetadataSize;
  // Generate UUID to use for uploads
  uuid_t uu;
  uuid_generate(uu);
  uuid_ = std::string(36, 0);
  uuid_unparse_lower(uu, &uuid_[0]);
}

size_t CASClient::BytestreamChunkSizeBytes() {
  return kBytestreamChunkSizeBytes;
}

std::string CASClient::MakeResourceName(const Digest& digest, bool isUpload) {
  std::string name;
  if (!grpc_client_->InstanceName().empty())
    name.append(grpc_client_->InstanceName()).append("/");
  if (isUpload)
    name.append("uploads/").append(uuid_).append("/");
  name.append("blobs/").append(digest.hash()).append("/");
  name.append(std::to_string(digest.size_bytes()));
  return name;
}

std::string CASClient::FetchString(const Digest& digest,
                                   GRPCClient::RequestStats* req_stats) {
  const std::string resource_name = MakeResourceName(digest, false);
  std::string result;
  auto fetch_lambda = [&](grpc::ClientContext &context) {
    ReadRequest request;
    request.set_resource_name(resource_name);
    request.set_read_offset(0);
    auto reader = bytestream_client_->Read(&context, request);
    std::string downloaded_data;
    downloaded_data.reserve(digest.size_bytes());
    ReadResponse response;
    while (reader->Read(&response))
      downloaded_data += response.data();
    const grpc::Status read_status = reader->Finish();
    if (read_status.ok()) {
      const auto bytes_downloaded = downloaded_data.size();
      if (bytes_downloaded != (std::size_t)digest.size_bytes()) {
        Fatal("Expected %d bytes, but downloaded blob was %d bytes",
              digest.size_bytes(), bytes_downloaded);
      }
      const auto downloaded_digest = digest_generator_.Hash(downloaded_data);
      if (downloaded_digest != digest) {
        Fatal("Expected digest '%s', but downloaded digest '%s'",
              toString(digest), toString(downloaded_digest));
      }
      result = std::move(downloaded_data);
    }
    return read_status;
  };
  grpc_client_->IssueRequest(fetch_lambda, "ByteStream.Read()", req_stats);
  return result;
}

void CASClient::Download(int fd, const Digest& digest,
                         GRPCClient::RequestStats* req_stats) {
  const std::string resource_name = MakeResourceName(digest, false);
  int64_t bytes_downloaded = 0;
  auto digest_context = digest_generator_.CreateDigestContext();
  auto download_lambda = [&](grpc::ClientContext &context) {
    ReadRequest request;
    request.set_resource_name(resource_name);
    request.set_read_offset(bytes_downloaded);
    auto reader = bytestream_client_->Read(&context, request);
    ReadResponse response;
    while (reader->Read(&response)) {
      const auto data = response.data();
      if (write(fd, data.c_str(), data.size()) != (ssize_t)data.size())
        Fatal("Error in write to descriptor %d", fd);
      digest_context.Update(data.c_str(), data.size());
      bytes_downloaded += data.size();
    }
    const auto read_status = reader->Finish();
    if (read_status.ok()) {
      struct stat st;
      fstat(fd, &st);
      if (st.st_size != digest.size_bytes()) {
        Fatal("Expected %d bytes, but downloaded blob was %d bytes",
              digest.size_bytes(), st.st_size);
      }
      const auto downloaded_digest = digest_context.FinalizeDigest();
      if (downloaded_digest != digest) {
        Fatal("Expected digest '%s', but downloaded digest '%s'",
              toString(digest), toString(downloaded_digest));
      }
    }
    return read_status;
  };
  grpc_client_->IssueRequest(download_lambda, "ByteStream.Read()", req_stats);
}

void CASClient::Upload(const std::string& data, const Digest& digest,
                       GRPCClient::RequestStats* req_stats) {
  const auto data_size = static_cast<google::protobuf::int64>(data.size());
  if (data_size != digest.size_bytes()) {
    Fatal("Digest length of %d bytes for %s != data length of %d bytes",
          digest.size_bytes(), digest.hash().c_str(), data_size);
  }
  const std::string resource_name = MakeResourceName(digest, true);
  WriteResponse response;
  auto upload_lambda = [&](grpc::ClientContext &context) {
    auto writer = bytestream_client_->Write(&context, &response);
    size_t offset = 0;
    bool lastChunk = false;
    while (!lastChunk) {
      WriteRequest request;
      request.set_resource_name(resource_name);
      request.set_write_offset(offset);
      const size_t uploadLength =
          std::min(BytestreamChunkSizeBytes(), data.size() - offset);
      request.set_data(&data[offset], uploadLength);
      offset += uploadLength;
      lastChunk = (offset == data.size());
      if (lastChunk)
        request.set_finish_write(lastChunk);
      if (!writer->Write(request))
        break;
    }
    writer->WritesDone();
    auto status = writer->Finish();
    if (status.ok()) {
      if (response.committed_size() != digest.size_bytes()) {
        Fatal("Expected to upload %d bytes for %s, "
              "but server reports %d bytes committed",
              digest.size_bytes(), digest.hash().c_str(),
              response.committed_size());
        }
    }
    return status;
  };
  grpc_client_->IssueRequest(upload_lambda, "ByteStream.Write()", req_stats);
}

void CASClient::Upload(int fd, const Digest& digest,
                       GRPCClient::RequestStats* req_stats) {
  std::vector<char> buffer(BytestreamChunkSizeBytes());
  const std::string resource_name = MakeResourceName(digest, true);
  lseek(fd, 0, SEEK_SET);
  WriteResponse response;
  auto upload_lambda = [&](grpc::ClientContext &context) {
    auto writer = bytestream_client_->Write(&context, &response);
    int64_t offset = 0;
    bool lastChunk = false;
    while (!lastChunk) {
      const ssize_t bytesRead =
          read(fd, &buffer[0], BytestreamChunkSizeBytes());
      if (bytesRead < 0)
        Fatal("Error in read on descriptor %d", fd);
      WriteRequest request;
      request.set_resource_name(resource_name);
      request.set_write_offset(offset);
      request.set_data(&buffer[0], static_cast<size_t>(bytesRead));
      if (offset + bytesRead < digest.size_bytes()) {
        if (bytesRead == 0) {
          Fatal("Upload of %s failed: unexpected end of file",
                digest.hash().c_str());
        }
      } else {
        lastChunk = true;
        request.set_finish_write(true);
      }
      if (!writer->Write(request))
        break;
      offset += bytesRead;
    }
    writer->WritesDone();
    auto status = writer->Finish();
    if (status.ok()) {
      if (response.committed_size() != digest.size_bytes()) {
        Fatal("Expected to upload %d bytes for %s, "
              "but server reports %d bytes committed",
              digest.size_bytes(), digest.hash().c_str(),
              response.committed_size());
      }
    }
    return status;
  };
  grpc_client_->IssueRequest(upload_lambda, "ByteStream.Write()", req_stats);
}

void CASClient::DoUploadRequest(const UploadRequest& request,
                                GRPCClient::RequestStats* req_stats) {
  if (!request.path.empty()) {
    const FileDescriptor fd(openat(request.dirfd, request.path.c_str(),
                                   O_RDONLY));
    if (fd.Get() < 0)
      Fatal("Error in open for file \"%s\"", request.path.c_str());
    Upload(fd.Get(), request.digest, req_stats);
  } else if (request.fd >= 0) {
    Upload(request.fd, request.digest, req_stats);
  } else {
    Upload(request.data, request.digest);
  }
}

void CASClient::UploadBlobs(const UploadRequests& requests,
    GRPCClient::RequestStats* req_stats) {
  // We first sort the requests by their sizes in ascending order, so
  // that we can then iterate through that result greedily trying to add
  // as many digests as possible to each request.
  UploadRequests request_list(requests);
  std::sort(request_list.begin(), request_list.end(),
            [](const UploadRequest &r1, const UploadRequest &r2) {
              return r1.digest.size_bytes() < r2.digest.size_bytes();
            });
  // Grouping the requests into batches (we only need to look at the
  // Digests for their sizes):
  Digests digests;
  for (const auto& r : request_list)
    digests.push_back(r.digest);

  const auto batches = MakeBatches(digests);
  for (const auto& batch_range : batches) {
    const size_t batch_start = batch_range.first;
    const size_t batch_end = batch_range.second;
    BatchUpload(request_list, batch_start, batch_end, req_stats);
  }

  // Fetching all those digests that might need to be uploaded using the
  // Bytestream API. Those will be in the range [batch_end, batches.size()).
  const size_t batch_end = batches.empty() ? 0 : batches.rbegin()->second;
  for (auto d = batch_end; d < request_list.size(); d++)
    DoUploadRequest(request_list[d], req_stats);
}

CASClient::DownloadBlobsResult CASClient::DownloadBlobs(
    const Digests& digests, int temp_dirfd,
    GRPCClient::RequestStats* req_stats) {
  CASClient::DownloadBlobsResult downloaded_data;
  // Writing the data directly into the result. (We know that the status code
  // will be OK for each of these blobs.)
  auto write_blob = [&](const std::string &hash, const std::string &data) {
    google::rpc::Status status;
    status.set_code(grpc::StatusCode::OK);
    downloaded_data.emplace(hash, std::make_pair(status, data));
  };
  const CASClient::DownloadResults download_results =
      DownloadBlobs(digests, write_blob, temp_dirfd, req_stats);
  // And adding the codes of the hashes that failed into the result:
  for (const auto &entry : download_results) {
    const Digest& digest = entry.first;
    const google::rpc::Status &status = entry.second;
    if (status.code() != grpc::StatusCode::OK)
      downloaded_data.emplace(digest.hash(), std::make_pair(status, ""));
  }
  return downloaded_data;
}

CASClient::DownloadBlobsResult CASClient::DownloadBlobs(
    const Digests& digests,
    GRPCClient::RequestStats* req_stats) {
  return DownloadBlobs(digests, -1, req_stats);
}

CASClient::DownloadBlobsResult CASClient::DownloadBlobsToDirectory(
    const Digests& digests, int temp_dirfd,
    GRPCClient::RequestStats* req_stats) {
  return DownloadBlobs(digests, temp_dirfd, req_stats);
}

CASClient::DownloadResults CASClient::DownloadBlobs(
    const Digests& digests, const WriteBlobCallback &write_blob,
    int temp_dirfd, GRPCClient::RequestStats* req_stats) {
  DownloadResults download_results;
  download_results.reserve(digests.size());
  // We first sort the digests by their sizes in ascending order, so that
  // we can then iterate through that result greedily trying to add as
  // many digests as possible to each request.
  auto request_list(digests);
  std::sort(request_list.begin(), request_list.end(),
      [](const Digest& d1, const Digest& d2) {
        return d1.size_bytes() < d2.size_bytes();
      });
  const auto batches = MakeBatches(request_list);
  for (const auto &batch_range : batches) {
    const size_t batch_start = batch_range.first;
    const size_t batch_end = batch_range.second;
    const auto batch_results = BatchDownload(request_list, batch_start,
        batch_end, write_blob, req_stats, temp_dirfd);
    std::move(batch_results.cbegin(), batch_results.cend(),
              std::back_inserter(download_results));
  }
  // Fetching all those digests that might need to be downloaded using
  // the Bytestream API. Those will be in the range [batch_end,
  // batches.size()).
  size_t batch_end = batches.empty() ? 0 : batches.rbegin()->second;
  for (auto d = batch_end; d < request_list.size(); d++) {
    const Digest digest = request_list[d];
    google::rpc::Status status;
    if (temp_dirfd < 0) {
      const auto data = FetchString(digest, req_stats);
      write_blob(digest.hash(), data);
    } else {
      // Download blob directly into a file to avoid excessive
      // memory usage for large files.
      const auto path = digest.hash();
      FileDescriptor fd(openat(temp_dirfd, path.c_str(),
                                O_WRONLY | O_CREAT | O_TRUNC, 0600));
      if (fd.Get() < 0) {
        Fatal("CASClient::downloadBlobs: Failed to create file \"%s\""
              " in temporary directory", path.c_str());
      }
      Download(fd.Get(), digest, req_stats);
      write_blob(digest.hash(), path);
    }
    status.set_code(grpc::StatusCode::OK);
    download_results.emplace_back(digest, status);
  }
  return download_results;
}

void CASClient::BatchUpload(const UploadRequests& requests,
    const size_t start_index, const size_t end_index,
    GRPCClient::RequestStats* req_stats) {
  assert(start_index <= end_index);
  assert(end_index <= requests.size());
  BatchUpdateBlobsRequest request;
  request.set_instance_name(grpc_client_->InstanceName());

  for (auto d = start_index; d < end_index; d++) {
    const UploadRequest& upload_request = requests[d];
    auto entry = request.add_requests();
    entry->mutable_digest()->CopyFrom(upload_request.digest);
    if (!upload_request.path.empty()) {
      entry->set_data(StaticFileUtils::GetFileContents(
        upload_request.dirfd, upload_request.path.c_str()));
    } else if (upload_request.fd >= 0) {
      entry->set_data(StaticFileUtils::GetFileContents(upload_request.fd));
    } else {
      entry->set_data(upload_request.data);
    }
  }

  BatchUpdateBlobsResponse response;
  auto upload_lamda = [&](grpc::ClientContext& context) {
    auto status = cas_client_->BatchUpdateBlobs(&context, request, &response);
    return status;
  };
  grpc_client_->IssueRequest(upload_lamda, "BatchUpdateBlobs()", req_stats);
}

void WriteFile(int dirfd, const std::string& path, const char* buf, size_t n) {
  FileDescriptor fd(openat(dirfd, path.c_str(),
                           O_WRONLY | O_CREAT | O_TRUNC, 0666));
  if (fd.Get() < 0)
    Fatal("Failed to create file \"%s\" in temporary dir", path.c_str());
  while (n > 0) {
    ssize_t ret = write(fd.Get(), buf, n);
    if (ret < 0) {
      if (errno == EINTR)
        continue; // Interrupted by signal, retry
      Fatal("Failed to write file \"%s\" in temporary dir", path.c_str());
    }
    assert(ret != 0); // write() should never return 0
    buf += ret;
    n -= ret;
  }
}

CASClient::DownloadResults CASClient::BatchDownload(
    const Digests& digests, const size_t start_index,
    const size_t end_index, const WriteBlobCallback& write_blob_function,
    GRPCClient::RequestStats* req_stats, int temp_dirfd) {
  assert(start_index <= end_index);
  assert(end_index <= digests.size());
  BatchReadBlobsRequest request;
  request.set_instance_name(grpc_client_->InstanceName());

  for (auto d = start_index; d < end_index; d++) {
    auto digest = request.add_digests();
    digest->CopyFrom(digests[d]);
  }

  BatchReadBlobsResponse response;
  auto download_lamda = [&](grpc::ClientContext &context) {
    auto status = cas_client_->BatchReadBlobs(&context, request, &response);
    return status;
  };
  grpc_client_->IssueRequest(download_lamda, "BatchReadBlobs()", req_stats);

  DownloadResults download_results;
  download_results.reserve(static_cast<size_t>(response.responses_size()));
  for (const auto& down_resp : response.responses()) {
    if (down_resp.status().code() == GRPC_STATUS_OK) {
      const auto down_digest = digest_generator_.Hash(down_resp.data());
      if (down_digest != down_resp.digest()) {
        google::rpc::Status status;
        status.set_code(grpc::StatusCode::INTERNAL);
        std::ostringstream error;
        error << "Expected blob with digest "
              << toString(down_resp.digest())
              << ", but downloaded blob has digest "
              << toString(down_digest);
        status.set_message(error.str());
        download_results.emplace_back(down_resp.digest(), status);
        continue;
      }
      if (temp_dirfd < 0) {
        write_blob_function(down_resp.digest().hash(), down_resp.data());
      } else {
        const auto path = down_resp.digest().hash();
        const auto data = down_resp.data();
        WriteFile(temp_dirfd, path, data.c_str(), data.size());
        write_blob_function(down_resp.digest().hash(), path);
      }
    }
    download_results.emplace_back(down_resp.digest(), down_resp.status());
  }
  return download_results;
}

std::vector<std::pair<size_t, size_t>> CASClient::MakeBatches(
    const Digests& digests) {
  constexpr size_t kSizeoOfEstimatedTopLevelGRPCContainer = 256;
  constexpr size_t kBlobMetadataSize = 256;
  std::vector<std::pair<size_t, size_t>> batches;
  const size_t max_batch_size =
      max_batch_total_size_ - kSizeoOfEstimatedTopLevelGRPCContainer;
  size_t batch_start = 0;
  size_t batch_end = 0;
  while (batch_end < digests.size()) {
    if (digests[batch_end].size_bytes() >
        static_cast<int64_t>(max_batch_size - kBlobMetadataSize)) {
      // All digests from `batch_end` to the end of the list are
      // larger than what we can request; stop.
      return batches;
    }
    // Adding all the digests that we can until we run out or exceed
    // the batch request limit...
    size_t bytes_in_batch = 0;
    while (batch_end < digests.size() &&
           (std::size_t)digests[batch_end].size_bytes() <=
               (max_batch_size - bytes_in_batch - kBlobMetadataSize)) {
      bytes_in_batch += (digests[batch_end].size_bytes() + kBlobMetadataSize);
      batch_end++;
    }
    batches.emplace_back(std::make_pair(batch_start, batch_end));
    batch_start = batch_end;
  }
  return batches;
}

CASClient::Digests CASClient::FindMissingBlobs(const Digests& digests,
    GRPCClient::RequestStats* req_stats) {
  FindMissingBlobsRequest request;
  request.set_instance_name(grpc_client_->InstanceName());
  // We take the given digests and split them across requests to not exceed
  // the maximum size of a gRPC message:
  std::vector<FindMissingBlobsRequest> requests_to_issue;
  size_t batch_size = 0;
  for (const Digest& digest : digests) {
    const size_t digest_size = digest.ByteSizeLong();
    if (batch_size + digest_size > BytestreamChunkSizeBytes()) {
      requests_to_issue.push_back(request);
      request.clear_blob_digests();
      batch_size = 0;
    } else {
      batch_size += digest_size;
    }
    auto entry = request.add_blob_digests();
    entry->CopyFrom(digest);
  }
  requests_to_issue.push_back(request);

  Digests missing_blobs;
  for (const auto &request_to_issue : requests_to_issue) {
    FindMissingBlobsResponse response;
    auto find_lambda = [&](grpc::ClientContext &context) {
      const auto status = cas_client_->FindMissingBlobs(
          &context, request_to_issue, &response);
      return status;
    };
    grpc_client_->IssueRequest(find_lambda,
                               "FindMissingBlobs()", req_stats);
    missing_blobs.insert(missing_blobs.end(),
                         response.missing_blob_digests().cbegin(),
                         response.missing_blob_digests().cend());
  }
  return missing_blobs;
}

} // namespace RemoteExecutor
