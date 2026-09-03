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

#ifndef GRPC_SRC_CORE_LOAD_BALANCING_SLICER_SLICE_MAP_H
#define GRPC_SRC_CORE_LOAD_BALANCING_SLICER_SLICE_MAP_H

#include <grpc/impl/connectivity_state.h>
#include <stddef.h>
#include <stdint.h>

#include <array>
#include <atomic>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "src/core/load_balancing/lb_policy.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// Snapshot of an endpoint's connectivity state and picker.
// Holds an atomic latch tracking if ExitIdle() has been triggered.
class EndpointState final : public RefCounted<EndpointState> {
 public:
  // `exit_idle` is invoked once when leaving IDLE state.
  EndpointState(grpc_connectivity_state connectivity_state,
                RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker,
                absl::AnyInvocable<void()> exit_idle = nullptr)
      : connectivity_state_(connectivity_state),
        picker_(std::move(picker)),
        exit_idle_(std::move(exit_idle)) {}

  grpc_connectivity_state connectivity_state() const {
    return connectivity_state_;
  }
  const RefCountedPtr<LoadBalancingPolicy::SubchannelPicker>& picker() const {
    return picker_;
  }

  // Triggers exit-idle once. Returns true if this call triggered it.
  // Thread-safe.
  bool ExitIdle() const {
    bool expected = false;
    if (!connect_triggered_.compare_exchange_strong(
            expected, true, std::memory_order_relaxed)) {
      return false;
    }
    if (exit_idle_ != nullptr) {
      std::exchange(exit_idle_, nullptr)();
    }
    return true;
  }

  // True if exit-idle has been triggered.
  bool connect_triggered() const {
    return connect_triggered_.load(std::memory_order_relaxed);
  }

 private:
  grpc_connectivity_state connectivity_state_;
  RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker_;
  mutable absl::AnyInvocable<void()> exit_idle_;
  mutable std::atomic<bool> connect_triggered_{false};
};

// Immutable key-to-endpoint range map for slicer LB policy.
class SliceMap final : public RefCounted<SliceMap> {
 public:
  using EndpointMap = std::map<std::string, RefCountedPtr<EndpointState>>;

  struct SliceAssignment {
    std::string start_key;
    std::vector<std::string> endpoint_names;
  };

  struct LogicalAssignment {
    std::vector<SliceAssignment> slices;
    int64_t generation = 0;
  };

  // Represents a key-range slice and its assigned endpoints.
  struct SliceEntry {
    std::string start_key;
    std::vector<size_t> all_endpoints_in_slice;
    std::array<std::vector<size_t>, 5> endpoints_by_state;
    bool in_fallback = false;
  };

  // Builds a SliceMap. Missing endpoints in assignment are skipped.
  static absl::StatusOr<RefCountedPtr<SliceMap>> Make(
      const EndpointMap& endpoint_map, const LogicalAssignment* assignment);

  // Returns slice matching key, or nullptr for fallback.
  const SliceEntry* Lookup(absl::string_view key) const;

  int64_t generation() const { return generation_; }

  // Global fallback pool of all endpoints.
  const std::vector<RefCountedPtr<EndpointState>>& all_endpoints() const {
    return all_endpoints_;
  }

  const std::vector<SliceEntry>& slices() const { return slices_; }

 private:
  std::vector<RefCountedPtr<EndpointState>> all_endpoints_;
  std::vector<SliceEntry> slices_;
  int64_t generation_ = 0;
};

using SliceEntry = SliceMap::SliceEntry;

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LOAD_BALANCING_SLICER_SLICE_MAP_H
