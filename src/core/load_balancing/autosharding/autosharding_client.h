//
// Copyright 2026 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef GRPC_SRC_CORE_LOAD_BALANCING_AUTOSHARDING_AUTOSHARDING_CLIENT_H
#define GRPC_SRC_CORE_LOAD_BALANCING_AUTOSHARDING_AUTOSHARDING_CLIENT_H

#include <grpc/event_engine/event_engine.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "src/core/load_balancing/autosharding/autosharding_assignment.h"
#include "src/core/util/backoff.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/util/work_serializer.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace grpc_core {

// Manages the WatchShardingAssignment stream to the autosharding server for a
// fixed (transport, target) pair, handling chunk reassembly, ACK/NACKs, stream
// retry backoff, and the initial assignment timer (gRFC A119).
//
// All methods (including the constructor) and ResultCallback run on the
// WorkSerializer.
class AutoshardingClient final
    : public InternallyRefCounted<AutoshardingClient> {
 public:
  // Receives each valid assignment, or an error before the first valid
  // assignment.  Never invoked after Orphan(), and destroyed with the client.
  using ResultCallback = absl::AnyInvocable<void(
      absl::StatusOr<RefCountedPtr<AutoshardingAssignment>>)>;

  static constexpr char kMethod[] =
      "/google.cloud.autosharding.v1.AutoshardingService/"
      "WatchShardingAssignment";

  AutoshardingClient(
      RefCountedPtr<XdsTransport> transport, std::string target,
      std::string client_uuid, Duration initial_assignment_timeout,
      std::shared_ptr<WorkSerializer> work_serializer,
      std::shared_ptr<grpc_event_engine::experimental::EventEngine>
          event_engine,
      ResultCallback on_result);

  void Orphan() override;

  // Resets the stream retry backoff and the transport's connection backoff.
  // Does not restart a stream whose retry timer is pending.
  void ResetBackoffLocked();

 private:
  class StreamCall;

  void StartStreamLocked();
  void OnStreamFinishedLocked(bool seen_valid_assignment, absl::Status status);
  void StartRetryTimerLocked();
  void OnRetryTimerLocked();
  void OnInitialAssignmentTimerLocked();
  void CancelInitialAssignmentTimerLocked();

  void ReportAssignmentLocked(int64_t generation,
                              RefCountedPtr<AutoshardingAssignment> assignment);
  void ReportErrorLocked(absl::Status status);

  bool HasReportedAssignmentLocked() const {
    return last_accepted_generation_ > 0;
  }

  const RefCountedPtr<XdsTransport> transport_;
  const std::string target_;
  const std::string client_uuid_;
  const std::shared_ptr<WorkSerializer> work_serializer_;
  const std::shared_ptr<grpc_event_engine::experimental::EventEngine>
      event_engine_;
  ResultCallback on_result_;

  // Sent as InitialClientConfig.latest_generation (0 if none accepted yet).
  int64_t last_accepted_generation_ = 0;
  BackOff backoff_;
  OrphanablePtr<StreamCall> stream_;
  std::optional<grpc_event_engine::experimental::EventEngine::TaskHandle>
      retry_timer_handle_;
  std::optional<grpc_event_engine::experimental::EventEngine::TaskHandle>
      initial_assignment_timer_handle_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LOAD_BALANCING_AUTOSHARDING_AUTOSHARDING_CLIENT_H
