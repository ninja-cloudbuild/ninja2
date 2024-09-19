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

#ifndef NINJA_REMOTEEXECUTOR_REMOTEEXECUTIONCLIENT_H
#define NINJA_REMOTEEXECUTOR_REMOTEEXECUTIONCLIENT_H

#include <atomic>
#include <set>
#include <string>

#include "cas_client.h"

namespace RemoteExecutor {

class RemoteExecutionClient {
public:
  explicit RemoteExecutionClient(GRPCClient* exec_grpc,
                                 GRPCClient* ac_grpc)
      : exec_grpc_(exec_grpc), ac_grpc_(ac_grpc) {}

  void Init();
  bool FetchFromActionCache(const Digest &action_digest,
                            const std::set<std::string> &outputs,
                            ActionResult *result);
  bool UpdateToActionCache(const Digest &action_digest,
                            ActionResult *result);
  ActionResult ExecuteAction(const Digest &action_digest,
                             const std::atomic_bool &stop_requested,
                             bool skip_cache = false);
  void DownloadOutputs(CASClient *cas_client,
                       const ActionResult &action_result, int dirfd);
private:
  GRPCClient* exec_grpc_;
  GRPCClient* ac_grpc_;
  std::unique_ptr<Execution::StubInterface> exec_stub_;
  std::unique_ptr<Operations::StubInterface> op_stub_;
  std::unique_ptr<ActionCache::StubInterface> ac_stub_;
};

std::string GetRandomHexString(int width);

} // namespace RemoteExecutor

#endif // NINJA_REMOTEEXECUTOR_REMOTEEXECUTIONCLIENT_H
