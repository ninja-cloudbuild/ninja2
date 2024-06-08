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

#ifndef NINJA_REMOTEEXECUTOR_GRPCCLIENT_H
#define NINJA_REMOTEEXECUTOR_GRPCCLIENT_H

#include <functional>
#include <memory>

#include "build/bazel/remote/execution/v2/remote_execution.grpc.pb.h"
#include "build/buildgrid/local_cas.grpc.pb.h"
#include "google/bytestream/bytestream.grpc.pb.h"
#include "google/longrunning/operations.grpc.pb.h"
#include "grpcpp/channel.h"

namespace RemoteExecutor {

using namespace build::bazel::remote::execution::v2;
using namespace build::buildgrid;
using namespace google::bytestream;
using namespace google::longrunning;

struct ConnectionOptions {
  unsigned int retry_limit = 4;
  unsigned int retry_delay = 1000;
  unsigned int request_timeout = 0;
  std::string instance_name;
  std::string url;

  void SetRetryDelay(unsigned int value) { retry_delay = value; }
  void SetRetryLimit(unsigned int value) { retry_limit = value; }
  void SetRequestTimeout(unsigned int value) { request_timeout = value; }
  void SetInstanceName(const std::string& value) { instance_name = value; }
  void SetUrl(const std::string& value) { url = value; }

  std::shared_ptr<grpc::Channel> CreateChannel() const;
};

class RequestMetadataGenerator {
public:
  RequestMetadataGenerator() {}

  void AttachRequestMetadata(grpc::ClientContext* context) const;
  void SetToolDetails(const std::string& tool_name,
                      const std::string& tool_details);

  void SetActionID(const std::string& action_id)  {
    action_id_ = action_id;
  }
  void SetToolInvocationID(const std::string& tool_invocation_id) {
    tool_invocation_id_ = tool_invocation_id;
  }
  void SetCorrelatedInvocationsID(
      const std::string& correlated_invocations_id) {
    correlated_invocations_id_ = correlated_invocations_id;
  }

private:
  void AttachRequestMetadata(grpc::ClientContext *context,
      const std::string& action_id, const std::string& tool_invocation_id,
      const std::string& correlated_invocations_id) const;

protected:
  ToolDetails tool_details_;
  std::string action_id_;
  std::string tool_invocation_id_;
  std::string correlated_invocations_id_;

  RequestMetadata GenerateRequestMetadata(
      const std::string& action_id, const std::string& tool_invocation_id,
      const std::string& correlated_invocations_id) const;

  static const std::string HEADER_NAME;
};

class GRPCRetrier final {
public:
  using GRPCInvocation = std::function<grpc::Status(grpc::ClientContext&)>;
  using MetadataAttacher = std::function<void(grpc::ClientContext*)>;
  using GRPCStatusCodes = std::set<grpc::StatusCode>;

  GRPCRetrier(unsigned int retry_limit,
              std::chrono::milliseconds retry_delay_base,
              const GRPCInvocation& invocation,
              const std::string& invocation_name)
      : GRPCRetrier(retry_limit, retry_delay_base, invocation,
                    invocation_name, {}) {}

  GRPCRetrier(unsigned int retry_limit,
              std::chrono::milliseconds retry_delay_base,
              const GRPCInvocation& invocation,
              const std::string& invocation_name,
              const GRPCStatusCodes& retryableStatusCodes,
              const std::chrono::seconds& requestTimeout =
                  std::chrono::seconds::zero())
      : invocation_(invocation), invocation_name_(invocation_name),
        retry_limit_(retry_limit), retry_delay_base_(retry_delay_base),
        retryable_status_codes_(retryableStatusCodes),
        metadata_attacher_(nullptr), request_timeout_(requestTimeout) {
    retryable_status_codes_.insert(grpc::StatusCode::UNAVAILABLE);
    ok_status_codes_.insert(grpc::StatusCode::OK);
  }

  std::chrono::seconds RequestTimeout() const { return request_timeout_; }
  unsigned int RetryLimit() const { return retry_limit_; }
  std::chrono::milliseconds RetryDelayBase() const {
    return retry_delay_base_;
  }

  bool IssueRequest();

  const GRPCStatusCodes& RetryableStatusCodes() const {
    return retryable_status_codes_;
  }
  void addRetryableStatusCode(const grpc::StatusCode retryable_code) {
    retryable_status_codes_.insert(retryable_code);
  }

  const GRPCStatusCodes& OKStatusCodes() const { return ok_status_codes_; }
  void AddOKStatusCode(const grpc::StatusCode ok_code) {
    ok_status_codes_.insert(ok_code);
  }

  void SetMetadataAttacher(const MetadataAttacher &metadata_attacher) {
    metadata_attacher_ = metadata_attacher;
  }

  grpc::Status Status() const { return status_; }
  unsigned int RetryAttempts() const { return retry_attempts_; }

private:
  const GRPCInvocation invocation_;
  const std::string invocation_name_;
  const unsigned int retry_limit_;
  std::chrono::milliseconds retry_delay_base_;

  GRPCStatusCodes retryable_status_codes_;
  GRPCStatusCodes ok_status_codes_;

  MetadataAttacher metadata_attacher_;

  grpc::Status status_;
  unsigned int retry_attempts_;
  std::chrono::seconds request_timeout_; // 0 indicates no timeout

  bool StatusRetryable(const grpc::Status& status) const;
  bool StatusOK(const grpc::Status& status) const;
};

class GRPCClient {
public:
  GRPCClient() {}

  void Init(const ConnectionOptions &options);

  std::shared_ptr<grpc::Channel> Channel() { return channel_; }

  void SetToolDetails(const std::string& tool_name,
                      const std::string& tool_version);
  void SetRequestMetadata(const std::string& action_id,
                          const std::string& tool_invocation_id,
                          const std::string& correlated_invocations_id = "");

  struct RequestStats {
    unsigned int retry_count { 0 };
  };

  std::string InstanceName() const { return instance_name_; }
  void SetInstanceName(const std::string& instance_name) {
    instance_name_ = instance_name;
  }

  void IssueRequest(const GRPCRetrier::GRPCInvocation& invocation,
                    const std::string& invocation_name,
                    RequestStats* req_stats) const;
  void IssueRequest(const GRPCRetrier::GRPCInvocation& invocation,
                    const std::string& invocation_name,
                    const std::chrono::seconds& req_timeout,
                    RequestStats* req_stats) const;

  GRPCRetrier MakeRetrier(const GRPCRetrier::GRPCInvocation& invocation,
      const std::string& name,
      const std::chrono::seconds& req_timeout = std::chrono::seconds::zero()) const;

  unsigned int RetryLimit() const { return retry_limit_; }
  void SetRetryLimit(unsigned int limit) { retry_limit_ = limit; }

  std::chrono::seconds requestTimeout() const { return request_timeout_; }
  void setRequestTimeout(std::chrono::seconds& requestTimeout) {
    request_timeout_ = requestTimeout;
  }

private:
  unsigned int retry_limit_ = 0;
  int retry_delay_ = 100;
  std::chrono::seconds request_timeout_ = std::chrono::seconds::zero();
  std::shared_ptr<grpc::Channel> channel_;
  std::string instance_name_;

  RequestMetadataGenerator metadata_generator_;
  const std::function<void(grpc::ClientContext*)> metadata_attach_func_ =
      [&](grpc::ClientContext *context) {
        metadata_generator_.AttachRequestMetadata(context);
      };
};

} // namespace RemoteExecutor

// Allow Digests to be used as unordered_hash keys.
namespace std {
template<> struct hash<RemoteExecutor::Digest> {
  std::size_t operator()(const RemoteExecutor::Digest& d) const noexcept {
    return std::hash<std::string>{}(d.hash());
  }
};
} // namespace std

namespace build {
namespace bazel {
namespace remote {
namespace execution {
namespace v2 {

inline bool operator==(const Digest& a, const Digest& b) {
  return a.hash() == b.hash() && a.size_bytes() == b.size_bytes();
}

inline bool operator!=(const Digest& a, const Digest& b) {
  return !(a == b);
}

inline bool operator<(const Digest& a, const Digest& b) {
  if (a.hash() != b.hash())
    return a.hash() < b.hash();
  return a.size_bytes() < b.size_bytes();
}

inline std::string toString(const Digest& digest) {
  return digest.hash() + "/" + std::to_string(digest.size_bytes());
}

} // namespace v2
} // namespace execution
} // namespace remote
} // namespace bazel
} // namespace build

#endif // NINJA_REMOTEEXECUTOR_GRPCCLIENT_H
