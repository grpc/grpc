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

#include "src/core/client_channel/subchannel_connectivity_state.h"

#include <grpc/support/port_platform.h>

namespace grpc_core {

SubchannelConnectivityState::SubchannelConnectivityState(
    bool created_from_endpoint)
    : created_from_endpoint_(created_from_endpoint) {}

void SubchannelConnectivityState::SetHasActiveConnections(
    bool has_active_connections) {
  has_active_connections_ = has_active_connections;
}

void SubchannelConnectivityState::SetConnectionAttemptInFlight(
    bool connection_attempt_in_flight) {
  connection_attempt_in_flight_ = connection_attempt_in_flight;
}

void SubchannelConnectivityState::SetHasRetryTimer(bool has_retry_timer) {
  has_retry_timer_ = has_retry_timer;
}

void SubchannelConnectivityState::SetLastFailureStatus(absl::Status status) {
  last_failure_status_ = std::move(status);
}

grpc_connectivity_state SubchannelConnectivityState::ComputeState() const {
  // If we have at least one connection, report READY.
  if (has_active_connections_) return GRPC_CHANNEL_READY;
  // If we were created from an endpoint and the connection is closed,
  // we have no way to create a new connection, so we report
  // TRANSIENT_FAILURE, and we'll never leave that state.
  if (created_from_endpoint_) return GRPC_CHANNEL_TRANSIENT_FAILURE;
  // If there's a connection attempt in flight, report CONNECTING.
  if (connection_attempt_in_flight_) return GRPC_CHANNEL_CONNECTING;
  // If we're in backoff delay, report TRANSIENT_FAILURE.
  if (has_retry_timer_) {
    return GRPC_CHANNEL_TRANSIENT_FAILURE;
  }
  // Otherwise, report IDLE.
  return GRPC_CHANNEL_IDLE;
}

absl::Status SubchannelConnectivityState::ComputeStatus() const {
  // Report status in TRANSIENT_FAILURE state.
  if (state_ == GRPC_CHANNEL_TRANSIENT_FAILURE) return last_failure_status_;
  return absl::OkStatus();
}

bool SubchannelConnectivityState::CheckUpdate() {
  grpc_connectivity_state new_state = ComputeState();
  // We need to update state_ first because ComputeStatus() depends on it.
  bool changed = false;
  if (new_state != state_) {
    state_ = new_state;
    changed = true;
  }
  absl::Status new_status = ComputeStatus();
  if (new_status != status_) {
    status_ = std::move(new_status);
    changed = true;
  }
  return changed;
}

}  // namespace grpc_core
