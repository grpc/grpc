// Copyright 2024 gRPC authors.
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

#include "src/core/lib/resource_quota/connection_quota.h"

#include <grpc/support/port_platform.h>

#include <atomic>
#include <cstdint>

#include "absl/log/check.h"

namespace grpc_core {

ConnectionQuota::ConnectionQuota() = default;

void ConnectionQuota::SetMaxIncomingConnections(int max_incoming_connections) {
  // The maximum can only be configured once.
  CHECK_LT(max_incoming_connections, INT_MAX);
  CHECK(max_incoming_connections_.exchange(
            max_incoming_connections, std::memory_order_release) == INT_MAX);
}

// Returns true if the incoming connection is allowed to be accepted on the
// server.
bool ConnectionQuota::AllowIncomingConnection(MemoryQuotaRefPtr mem_quota,
                                              absl::string_view /*peer*/) {
  if (mem_quota->IsMemoryPressureHigh()) {
    return false;
  }

  if (max_incoming_connections_.load(std::memory_order_relaxed) == INT_MAX) {
    return true;
  }

  int curr_active_connections =
      active_incoming_connections_.load(std::memory_order_acquire);
  do {
    if (curr_active_connections >=
        max_incoming_connections_.load(std::memory_order_relaxed)) {
      return false;
    }
  } while (!active_incoming_connections_.compare_exchange_weak(
      curr_active_connections, curr_active_connections + 1,
      std::memory_order_acq_rel, std::memory_order_relaxed));
  return true;
}

// Mark connections as closed.
void ConnectionQuota::ReleaseConnections(int num_connections) {
  if (max_incoming_connections_.load(std::memory_order_relaxed) == INT_MAX) {
    return;
  }
  CHECK(active_incoming_connections_.fetch_sub(
            num_connections, std::memory_order_acq_rel) >= num_connections);
}

}  // namespace grpc_core
