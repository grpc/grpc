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
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace grpc_core {

// Manages the WatchShardingAssignment stream to the autosharding server for a
// fixed (transport, target) pair, handling chunk reassembly, ACK/NACKs, stream
// retry backoff, and the initial assignment timer (gRFC A119).
//
// Thread-safe.
class AutoshardingClient final
    : public InternallyRefCounted<AutoshardingClient> {
 public:
  // Receives each valid assignment, or an error before the first valid
  // assignment.  Never invoked after Orphan(), and destroyed with the client.
  // Invoked while holding the client's lock, so it must not call into the
  // client.  It must hop into another context (such as a WorkSerializer) to
  // do any real work.
  using ResultCallback = absl::AnyInvocable<void(
      absl::StatusOr<RefCountedPtr<const AutoshardingAssignment>>)>;

  static constexpr char kMethod[] =
      "/google.cloud.autosharding.v1.AutoshardingService/"
      "WatchShardingAssignment";

  AutoshardingClient(
      RefCountedPtr<XdsTransport> transport, std::string target,
      std::string client_uuid, Duration initial_assignment_timeout,
      std::shared_ptr<grpc_event_engine::experimental::EventEngine>
          event_engine,
      ResultCallback on_result);

  void Orphan() override;

  // Resets the stream retry backoff and the transport's connection backoff.
  // If the stream retry timer is pending, restarts the stream at once.
  void ResetBackoff();

 private:
  class StreamCall;

  void StartStreamLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_);
  void OnStreamFinishedLocked(bool seen_valid_assignment, absl::Status status)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_);
  void StartRetryTimerLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_);
  void OnRetryTimer();
  void OnInitialAssignmentTimer();
  void CancelInitialAssignmentTimerLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_);

  void ReportAssignmentLocked(
      RefCountedPtr<const AutoshardingAssignment> assignment)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_);
  void ReportErrorLocked(absl::Status status)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_);

  bool HasReportedAssignmentLocked() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_) {
    return builder_.last_accepted_generation() > 0;
  }

  const RefCountedPtr<XdsTransport> transport_;
  const std::string target_;
  const std::string client_uuid_;
  const std::shared_ptr<grpc_event_engine::experimental::EventEngine>
      event_engine_;

  Mutex mu_;
  ResultCallback on_result_ ABSL_GUARDED_BY(&mu_);
  bool orphaned_ ABSL_GUARDED_BY(&mu_) = false;
  // Kept across streams for InitialClientConfig.latest_generation.
  AutoshardingAssignmentBuilder builder_ ABSL_GUARDED_BY(&mu_);
  BackOff backoff_ ABSL_GUARDED_BY(&mu_);
  OrphanablePtr<StreamCall> stream_ ABSL_GUARDED_BY(&mu_);
  std::optional<grpc_event_engine::experimental::EventEngine::TaskHandle>
      retry_timer_handle_ ABSL_GUARDED_BY(&mu_);
  std::optional<grpc_event_engine::experimental::EventEngine::TaskHandle>
      initial_assignment_timer_handle_ ABSL_GUARDED_BY(&mu_);
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LOAD_BALANCING_AUTOSHARDING_AUTOSHARDING_CLIENT_H
