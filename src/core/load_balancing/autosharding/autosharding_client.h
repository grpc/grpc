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

// Talks to an autosharding server with the WatchShardingAssignment stream,
// and reports assignments to the autosharding LB policy (gRFC A119).
//
// The client:
// - Sends InitialClientConfig at the start of each stream.
// - Collects assignment chunks, validates each complete assignment, and
//   sends an AssignmentAck for it.
// - Restarts the stream with exponential backoff when it fails.  The
//   backoff is reset when a stream receives any response.
// - Runs the initial assignment timer.
//
// A client instance is scoped to one (channel factory key, autosharding
// target) pair.  The LB policy creates a new client whenever either of those
// changes, which resets the assignment generation: the new client may be
// talking to a different sharding service or a different target, so a
// generation from the previous client is not meaningful (gRFC A119).  The
// generation survives stream restarts within one client.  The client reports
// errors only until it has reported a valid assignment; after that, the LB
// policy keeps using the last valid assignment.
//
// All methods, including the constructor, must be called from the
// WorkSerializer.  The result callback is also called from the
// WorkSerializer.
class AutoshardingClient final
    : public InternallyRefCounted<AutoshardingClient> {
 public:
  // Receives each valid assignment, or an error if no valid assignment
  // was reported yet.  Not called after Orphan().  The callback is destroyed
  // with the client, which can be after Orphan(), so it can hold a ref to the
  // LB policy.
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

  // Resets the stream retry backoff and the transport's connection backoff.
  // Does not restart a stream whose retry timer is pending.
  void ResetBackoffLocked();

 private:
  class StreamCall;

  void StartStreamLocked();
  // Called when the current stream ends.  seen_response is true if the
  // stream received any response.
  void OnStreamFinishedLocked(bool seen_response, absl::Status status);
  void StartRetryTimerLocked();
  void OnRetryTimerLocked();
  void OnInitialAssignmentTimerLocked();
  void CancelInitialAssignmentTimerLocked();

  // Stores generation as the last accepted generation, and reports
  // assignment.
  void ReportAssignmentLocked(int64_t generation,
                              AutoshardingAssignment assignment);
  // Does nothing if a valid assignment was reported before.
  void ReportErrorLocked(absl::Status status);

  // An accepted generation is always greater than 0, so a non-zero value
  // means that a valid assignment was reported.
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

  // Generation of the last accepted assignment, or 0 if none.  Sent as
  // InitialClientConfig.latest_generation on each new stream.  Each stream
  // has its own AutoshardingAssignmentBuilder for the chunks.
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
