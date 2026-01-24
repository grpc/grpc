//
// Copyright 2015 gRPC authors.
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

#ifndef GRPC_SRC_CORE_CLIENT_CHANNEL_SUBCHANNEL_CONNECTIVITY_STATE_H
#define GRPC_SRC_CORE_CLIENT_CHANNEL_SUBCHANNEL_CONNECTIVITY_STATE_H

#include <grpc/support/port_platform.h>

#include <grpc/impl/connectivity_state.h>

#include "absl/status/status.h"

namespace grpc_core {

class SubchannelConnectivityState {
 public:
  explicit SubchannelConnectivityState(bool created_from_endpoint);

  // Sets the state parameters.
  // Note: These do NOT trigger a state update. Call CheckUpdate() to update
  // the state and status.
  void SetHasActiveConnections(bool has_active_connections);
  void SetConnectionAttemptInFlight(bool connection_attempt_in_flight);
  void SetHasRetryTimer(bool has_retry_timer);
  void SetLastFailureStatus(absl::Status status);

  // Updates the state and status based on the current parameters.
  // Returns true if the state or status changed.
  bool CheckUpdate();

  grpc_connectivity_state state() const { return state_; }
  const absl::Status& status() const { return status_; }

  bool created_from_endpoint() const { return created_from_endpoint_; }

 private:
  grpc_connectivity_state ComputeState() const;
  absl::Status ComputeStatus() const;

  const bool created_from_endpoint_;
  bool has_active_connections_ = false;
  bool connection_attempt_in_flight_ = false;
  bool has_retry_timer_ = false;
  absl::Status last_failure_status_;

  grpc_connectivity_state state_ = GRPC_CHANNEL_IDLE;
  absl::Status status_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CLIENT_CHANNEL_SUBCHANNEL_CONNECTIVITY_STATE_H
