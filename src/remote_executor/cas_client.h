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

#ifndef NINJA_REMOTEEXECUTOR_CASCLIENT_H
#define NINJA_REMOTEEXECUTOR_CASCLIENT_H

#include <memory>
#include <unordered_map>

#include <fcntl.h>
#include <openssl/evp.h>

#include "grpc_client.h"

namespace RemoteExecutor {

struct CASHash {
  static Digest Hash(int fd);
  static Digest Hash(const std::string& str);
};

class DigestContext {
public:
  virtual ~DigestContext();
  Digest FinalizeDigest();
  void Update(const char* data, size_t data_size);

private:
  EVP_MD_CTX *context_;
  int64_t data_size_ { 0 };
  bool finalized_ { false };

  DigestContext();
  void Init(const EVP_MD* digest_funct_struct);
  static std::string HashToHex(const unsigned char* hash_buffer,
                               unsigned int hash_size);
  friend class DigestGenerator;
};

class DigestGenerator {
public:
  explicit DigestGenerator(DigestFunction_Value digest_function =
      DigestFunction_Value::DigestFunction_Value_SHA256);

  Digest Hash(const std::string& data) const;
  Digest Hash(int fd) const;

  DigestContext CreateDigestContext() const;

private:
  const DigestFunction_Value digest_func_;
  const EVP_MD *digest_func_struct_;

  static const EVP_MD* GetDigestFunctionStruct(
      DigestFunction_Value digest_function_value);

  using IncrementalUpdateFunc = std::function<void(char *, size_t)>;
  static int64_t ProcessFile(int fd, const IncrementalUpdateFunc& update_func);
};

class CASClient {
public:
  CASClient(GRPCClient* grpc_client, DigestFunction_Value digest_function =
      DigestFunction_Value::DigestFunction_Value_SHA256)
    : grpc_client_(grpc_client), digest_generator_(digest_function) {}

  void Init();

  std::string FetchString(const Digest& digest,
                          GRPCClient::RequestStats* req_stats = nullptr);

  void Download(int fd, const Digest& digest,
                GRPCClient::RequestStats* req_stats = nullptr);
  void Upload(const std::string& data, const Digest& digest,
              GRPCClient::RequestStats* req_stats = nullptr);
  void Upload(int fd, const Digest& digest,
              GRPCClient::RequestStats* req_stats = nullptr);

  struct UploadRequest {
    Digest digest;
    std::string data;
    int dirfd;
    std::string path;
    int fd;

    UploadRequest(const Digest& _digest, const std::string& _data)
      : digest(_digest), data(_data), dirfd(AT_FDCWD), fd(-1){};

    static UploadRequest FromPath(const Digest& _digest,
                                  const std::string& _path) {
      auto request = UploadRequest(_digest);
      request.path = _path;
      return request;
    }
  private:
    UploadRequest(const Digest& _digest)
      : digest(_digest), dirfd(AT_FDCWD), fd(-1){};
  };

  using UploadRequests = std::vector<UploadRequest>;

  void UploadBlobs(const UploadRequests& requests,
      GRPCClient::RequestStats* req_stats = nullptr);

  using OutputMap =
      std::unordered_multimap<std::string, std::pair<std::string, bool>>;
  using DownloadBlobsResult =
      std::unordered_map<std::string,
                         std::pair<google::rpc::Status, std::string>>;
  using Digests = std::vector<Digest>;
  DownloadBlobsResult DownloadBlobs(const Digests& digests,
      GRPCClient::RequestStats* req_stats = nullptr);
  DownloadBlobsResult DownloadBlobsToDirectory(const Digests& digests,
      int temp_dirfd, GRPCClient::RequestStats* req_stats = nullptr);

  Digests FindMissingBlobs(const Digests& digests,
      GRPCClient::RequestStats* req_stats = nullptr);

  static size_t BytestreamChunkSizeBytes();

private:
  GRPCClient* grpc_client_;

  std::unique_ptr<ByteStream::StubInterface> bytestream_client_;
  std::unique_ptr<ContentAddressableStorage::StubInterface> cas_client_;
  std::unique_ptr<LocalContentAddressableStorage::StubInterface> local_cas_client_;

  size_t max_batch_total_size_;
  std::string uuid_;
  DigestGenerator digest_generator_;

  using WriteBlobCallback =
      std::function<void(const std::string& hash, const std::string& data)>;
  using DownloadResult = std::pair<Digest, google::rpc::Status>;
  using DownloadResults = std::vector<DownloadResult>;

  DownloadResults DownloadBlobs(const Digests& digests,
      const WriteBlobCallback& write_blob_callback, int temp_dirfd,
      GRPCClient::RequestStats* req_stats = nullptr);

  std::string MakeResourceName(const Digest& digest, bool is_upload);

  DownloadBlobsResult DownloadBlobs(const Digests& digests,
      int temp_dirfd, GRPCClient::RequestStats* req_stats);

  void BatchUpload(const UploadRequests& requests,
      const size_t start_index, const size_t end_index,
      GRPCClient::RequestStats* req_stats);

  DownloadResults BatchDownload(const Digests& digests,
      const size_t start_index, const size_t end_index,
      const WriteBlobCallback& write_blob_function,
      GRPCClient::RequestStats* req_stats, int temp_dirfd = -1);

  std::vector<std::pair<size_t, size_t>> MakeBatches(const Digests& digests);

  void DoUploadRequest(const UploadRequest& request,
                       GRPCClient::RequestStats* req_stats);
};

} // namespace RemoteExecutor

#endif //NINJA_REMOTEEXECUTOR_CASCLIENT_H
