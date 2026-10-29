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

// Talks to an autosharding server with the WatchShardingAssignment stream,
// and reports assignments to the autosharding LB policy (gRFC A119).
//
// The client:
// - Sends InitialClientConfig at the start of each stream.
// - Collects assignment chunks, validates each complete assignment, and
//   sends an AssignmentAck for it.
// - Restarts the stream with exponential backoff when it fails.  The
//   backoff is reset when a stream receives a valid assignment.
// - Runs the initial assignment timer.
//
// All methods, including the constructor, must be called from the
// WorkSerializer.  The result callback is also called from the
// WorkSerializer.
class AutoshardingClient final
    : public InternallyRefCounted<AutoshardingClient> {
 public:
  // Receives each valid assignment, or an error if no valid assignment
  // was reported yet.  Not called after Orphan().
  using ResultCallback =
      absl::AnyInvocable<void(absl::StatusOr<AutoshardingAssignment>)>;

  struct Args {
    // Transport to the autosharding server.
    RefCountedPtr<XdsTransport> transport;
    // Sent as InitialClientConfig.target.
    std::string target;
    // Sent as InitialClientConfig.client_uuid.  The same value is used on
    // every stream.
    std::string client_uuid;
    // How long to wait for the first result before reporting an error.
    Duration initial_assignment_timeout;
    std::shared_ptr<WorkSerializer> work_serializer;
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine;
    ResultCallback on_result;
  };

  static constexpr char kMethod[] =
      "/google.cloud.autosharding.v1.AutoshardingService/"
      "WatchShardingAssignment";

  // Starts the stream and the initial assignment timer.
  explicit AutoshardingClient(Args args);

  // Stops the stream and the timers.
  void Orphan() override;

 private:
  class StreamCall;

  void StartStreamLocked();
  // Called when the current stream ends.  seen_valid_assignment is true if
  // the stream received a valid assignment.
  void OnStreamFinishedLocked(bool seen_valid_assignment, absl::Status status);
  void StartRetryTimerLocked();
  void OnRetryTimerLocked();
  void OnInitialAssignmentTimerLocked();
  void CancelInitialAssignmentTimerLocked();

  void ReportAssignmentLocked(AutoshardingAssignment assignment);
  // Does nothing if a valid assignment was reported before.
  void ReportErrorLocked(absl::Status status);

  RefCountedPtr<XdsTransport> transport_;
  const std::string target_;
  const std::string client_uuid_;
  std::shared_ptr<WorkSerializer> work_serializer_;
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_;
  ResultCallback on_result_;

  AutoshardingAssignmentBuilder builder_;
  BackOff backoff_;
  OrphanablePtr<StreamCall> stream_;
  std::optional<grpc_event_engine::experimental::EventEngine::TaskHandle>
      retry_timer_handle_;
  std::optional<grpc_event_engine::experimental::EventEngine::TaskHandle>
      initial_assignment_timer_handle_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LOAD_BALANCING_AUTOSHARDING_AUTOSHARDING_CLIENT_H
