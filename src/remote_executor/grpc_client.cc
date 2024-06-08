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

#include "grpc_client.h"

#include <cmath>
#include <sstream>
#include <thread>

#include "google/rpc/error_details.grpc.pb.h"
#include "google/protobuf/util/time_util.h"
#include "grpcpp/create_channel.h"

#include "../util.h"

namespace RemoteExecutor {

constexpr auto kGRPCPrefix = "grpc://";

bool StartsWith(const std::string &s1, const char *s2) {
  return s1.substr(0, strlen(s2)) == s2;
}

std::shared_ptr<grpc::Channel> ConnectionOptions::CreateChannel() const {
  if (!StartsWith(url, kGRPCPrefix))
    Fatal("Unsupported URL scheme");
  std::string target = url.substr(strlen(kGRPCPrefix));
  grpc::ChannelArguments channel_args;
  auto creds = grpc::InsecureChannelCredentials();
  return grpc::CreateCustomChannel(target, creds, channel_args);
}

const std::string RequestMetadataGenerator::HEADER_NAME =
    "build.bazel.remote.execution.v2.requestmetadata-bin";

void RequestMetadataGenerator::AttachRequestMetadata(
    grpc::ClientContext* context) const {
  AttachRequestMetadata(context, action_id_, tool_invocation_id_,
                        correlated_invocations_id_);
}

void RequestMetadataGenerator::SetToolDetails(
    const std::string& tool_name, const std::string& tool_version) {
  tool_details_.set_tool_name(tool_name);
  tool_details_.set_tool_version(tool_version);
}

RequestMetadata RequestMetadataGenerator::GenerateRequestMetadata(
    const std::string& action_id, const std::string& tool_invocation_id,
    const std::string& correlated_invocations_id) const {
  RequestMetadata metadata;
  metadata.mutable_tool_details()->CopyFrom(tool_details_);
  metadata.set_action_id(action_id);
  metadata.set_tool_invocation_id(tool_invocation_id);
  metadata.set_correlated_invocations_id(correlated_invocations_id);
  return metadata;
}

void RequestMetadataGenerator::AttachRequestMetadata(
    grpc::ClientContext* context, const std::string& action_id,
    const std::string& tool_invocation_id,
    const std::string& correlated_invocations_id) const {
  const auto metadata = GenerateRequestMetadata(
      action_id, tool_invocation_id, correlated_invocations_id);
  context->AddMetadata(HEADER_NAME, metadata.SerializeAsString());
}

std::string RetryingInvocationWarningMessage(
    const std::string& invocation_name, const grpc::Status& grpc_error,
    unsigned int attempt_number, unsigned int total_attempts,
    double retry_delay_ms) {
  std::stringstream s;
  s << "Attempt " << attempt_number + 1 << "/" << total_attempts + 1;
  if (!invocation_name.empty()) {
    s << " for \"" << invocation_name << "\"";
  }
  s << " failed with gRPC error [" << grpc_error.error_code() << ": "
    << grpc_error.error_message() << "], "
    << "retrying in " << retry_delay_ms << " ms...";
  return s.str();
}

std::string RetryAttemptsExceededErrorMessage(
    const std::string& invocation_name, const grpc::Status& grpc_error,
    unsigned int retry_limit) {
  std::stringstream s;
  s << "Retry limit (" << retry_limit << ") exceeded";
  if (!invocation_name.empty()) {
    s << " for \"" << invocation_name << "\"";
  }
  s << ", last gRPC error was [" << grpc_error.error_code() << ": "
    << grpc_error.error_message() << "]";
  return s.str();
}

bool GRPCRetrier::IssueRequest() {
  retry_attempts_ = 0;
  while (true) {
    grpc::ClientContext context;
    if (metadata_attacher_)
      metadata_attacher_(&context);
    std::chrono::system_clock::time_point deadline;
    if (request_timeout_ > std::chrono::seconds::zero()) {
      deadline = std::chrono::system_clock::now() + request_timeout_;
      context.set_deadline(deadline);
    }
    status_ = invocation_(context);
    if (StatusOK(status_) || !StatusRetryable(status_)) {
      if (!StatusOK(status_)) {
        std::string extra_log_context = "";
        if (status_.error_code() == grpc::DEADLINE_EXCEEDED &&
            request_timeout_ > std::chrono::seconds::zero()) {
          extra_log_context = (std::chrono::system_clock::now() < deadline)
              ? " (server timeout)" : " (client timeout)";
        }
        Error("%s failed with: %s: %s", invocation_name_,
              std::to_string(status_.error_code()),
              status_.error_message() + extra_log_context);
      }
      return true;
    }
    // The error might contain a `RetryInfo` message specifying a number of
    // seconds to wait before retrying. If so, use it for the base value.
    if (retry_attempts_ == 0 && !status_.error_details().empty()) {
      google::rpc::Status error_details;
      if (error_details.ParseFromString(status_.error_details())) {
        google::rpc::RetryInfo retryInfo;
        // This is a repeated Any field, so go through each one
        // and see if there's a RetryInfo in there
        for (const auto& detailed_error : error_details.details()) {
          if (detailed_error.UnpackTo(&retryInfo)) {
            auto serverDelay = google::protobuf::util::
                TimeUtil::DurationToMilliseconds(retryInfo.retry_delay());
            if (serverDelay > 0) {
              retry_delay_base_ = std::chrono::milliseconds(serverDelay);
            }
          }
        }
      }
    }
    // The call failed and could be retryable on its own.
    if (retry_attempts_ >= retry_limit_) {
      const auto errorMessage = RetryAttemptsExceededErrorMessage(
          invocation_name_, status_, retry_limit_);
      Error(errorMessage.c_str());
      return false;
    }
    // Delay the next call based on the number of attempts made:
    const auto retryDelay = retry_delay_base_ * pow(1.6, retry_attempts_);
    Warning(RetryingInvocationWarningMessage(invocation_name_, status_,
        retry_attempts_, retry_limit_, retryDelay.count()).c_str());
    std::this_thread::sleep_for(retryDelay);
    retry_attempts_++;
  }
}

bool GRPCRetrier::StatusRetryable(const grpc::Status &status) const {
  return retryable_status_codes_.count(status.error_code());
}

bool GRPCRetrier::StatusOK(const grpc::Status &status) const {
  return ok_status_codes_.count(status.error_code());
}

void GRPCClient::Init(const ConnectionOptions &options) {
  std::shared_ptr<grpc::Channel> channel = options.CreateChannel();
  retry_limit_ = options.retry_limit;
  retry_delay_ = options.retry_delay;
  request_timeout_ = std::chrono::seconds(options.request_timeout);
  channel_ = channel;
  instance_name_ = options.instance_name;
}

void GRPCClient::SetToolDetails(const std::string& tool_name,
                                const std::string& tool_version) {
  metadata_generator_.SetToolDetails(tool_name, tool_version);
}

void GRPCClient::SetRequestMetadata(
    const std::string& action_id, const std::string& tool_invocation_id,
    const std::string& correlated_invocations_id) {
  metadata_generator_.SetActionID(action_id);
  metadata_generator_.SetToolInvocationID(tool_invocation_id);
  metadata_generator_.SetCorrelatedInvocationsID(correlated_invocations_id);
}

void GRPCClient::IssueRequest(const GRPCRetrier::GRPCInvocation& invocation,
                              const std::string& invocation_name,
                              RequestStats* req_stats) const {
  IssueRequest(invocation, invocation_name, std::chrono::seconds::zero(),
               req_stats);
}

void GRPCClient::IssueRequest(const GRPCRetrier::GRPCInvocation &invocation,
                              const std::string& invocation_name,
                              const std::chrono::seconds& req_timeout,
                              RequestStats* req_stats) const {
  auto retrier = MakeRetrier(invocation, invocation_name, req_timeout);
  retrier.IssueRequest();
  if (req_stats != nullptr)
    req_stats->retry_count += retrier.RetryAttempts();
  auto status = retrier.Status();
  if (!status.ok()) {
    Fatal("GRPC error %d: %s",
          status.error_code(), status.error_message().c_str());
  }
}

GRPCRetrier GRPCClient::MakeRetrier(
    const GRPCRetrier::GRPCInvocation& invocation,
    const std::string& invocation_name,
    const std::chrono::seconds& req_timeout) const {
  // Pick the minimum non-zero timeout (from connectionoptions or override)
  auto min_nonzero = [](std::chrono::seconds a,
                             std::chrono::seconds b) {
    bool a_nonzero = a != std::chrono::seconds::zero();
    bool b_nonzero = b != std::chrono::seconds::zero();
    return (a_nonzero && b_nonzero && a < b) || (!b_nonzero);
  };
  auto shortest_timeout = std::min(req_timeout, request_timeout_, min_nonzero);
  GRPCRetrier retrier(retry_limit_, std::chrono::milliseconds(retry_delay_),
                      invocation, invocation_name, {}, shortest_timeout);
  retrier.SetMetadataAttacher(metadata_attach_func_);
  return retrier;
}

} // namespace RemoteExecutor
