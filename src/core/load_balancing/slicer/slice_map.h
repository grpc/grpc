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
#include <grpc/support/port_platform.h>
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

// The state of a single endpoint as seen by the slicer LB policy: its current
// connectivity state and the picker used to route a pick to it. This mirrors
// the RLS policy's ChildPolicyWrapper (which similarly bundles a connectivity
// state and a picker); the child policy that produces these is owned by the LB
// policy, not by this snapshot. The connectivity state and picker are
// immutable: when either changes, a new SliceMap is built with a new
// EndpointState. The one piece of mutable state is a latch recording whether a
// connection attempt has been triggered for this generation (see ExitIdle()).
class EndpointState final : public RefCounted<EndpointState> {
 public:
  // `exit_idle` is invoked (at most once, by the first ExitIdle() call) to ask
  // the endpoint's child policy to leave IDLE and start connecting. The LB
  // policy supplies a closure that hops to the control plane; it may be null
  // (e.g. in tests), in which case ExitIdle() only flips the latch.
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

  // Asks this endpoint's child policy to leave IDLE and start connecting. The
  // request is issued at most once for the life of this EndpointState (the
  // first caller wins); later calls are no-ops. Returns true iff this call was
  // the one that triggered the request. Safe to call concurrently from picks.
  bool ExitIdle() const {
    bool expected = false;
    if (!connect_triggered_.compare_exchange_strong(
            expected, true, std::memory_order_relaxed)) {
      return false;
    }
    if (exit_idle_ != nullptr) exit_idle_();
    return true;
  }

  // True once a connection attempt has been triggered on this endpoint.
  bool connect_triggered() const {
    return connect_triggered_.load(std::memory_order_relaxed);
  }

 private:
  grpc_connectivity_state connectivity_state_;
  RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker_;
  mutable absl::AnyInvocable<void()> exit_idle_;
  mutable std::atomic<bool> connect_triggered_{false};
};

// One key-range entry within a SliceMap. The range starts at start_key
// (inclusive) and extends to the start_key of the next slice.
struct SliceEntry {
  // The (inclusive) lower bound of this slice's key range. Keys are opaque byte
  // strings; std::string comparison gives the required lexicographic ordering.
  std::string start_key;
  // Indices (into SliceMap::all_endpoints()) of all endpoints assigned to this
  // slice. The picker selects a random endpoint from this list first, then
  // consults the per-state lists below.
  std::vector<size_t> all_endpoints_in_slice;
  // The same indices, bucketed by connectivity state. Indexed directly by
  // grpc_connectivity_state (GRPC_CHANNEL_IDLE..GRPC_CHANNEL_SHUTDOWN == 0..4).
  std::array<std::vector<size_t>, 5> endpoints_by_state;
  // Precomputed: true if this slice has no assigned endpoints or if all of its
  // assigned endpoints are in TRANSIENT_FAILURE. When true, picks for this
  // slice are routed to the global fallback pool.
  bool in_fallback = false;
};

// An immutable structure that maps a request key to the set of endpoints that
// should serve it, produced by combining the resolver's endpoints with the
// key-range assignment from the sharding service. Because it is immutable, the
// picker can read it without synchronization; a new SliceMap is built whenever
// the assignment or any endpoint's connectivity state changes.
//
// The global fallback pool is simply all_endpoints(): a pick that falls back
// selects (at random) from every endpoint, regardless of state.
class SliceMap final : public RefCounted<SliceMap> {
 public:
  // Maps an endpoint's identity (its hostname) to its current state. Owned by
  // the caller; used only during Make().
  using EndpointMap = std::map<std::string, RefCountedPtr<EndpointState>>;

  // A single key-range assignment from the sharding service: the endpoints
  // (identified by the keys of the EndpointMap) that serve the range starting
  // at start_key.
  struct SliceAssignment {
    std::string start_key;
    std::vector<std::string> endpoint_names;
  };

  // The full assignment received from the sharding service.
  struct LogicalAssignment {
    std::vector<SliceAssignment> slices;
    int64_t generation = 0;
  };

  // Builds a SliceMap from the resolver's endpoints and an assignment. If
  // `assignment` is null (no assignment yet from the sharding service), the
  // result is a "total fallback" map with no slices, whose every pick uses the
  // fallback pool. Returns an error if any endpoint named by the assignment is
  // absent from `endpoint_map`.
  static absl::StatusOr<RefCountedPtr<SliceMap>> Make(
      const EndpointMap& endpoint_map, const LogicalAssignment* assignment);

  // Returns the slice whose key range contains `key` (the slice with the
  // greatest start_key that is <= key), or null if there are no slices or `key`
  // sorts before the first slice's start_key. A null result means the caller
  // should use the fallback pool.
  const SliceEntry* Lookup(absl::string_view key) const;

  // The generation number of the assignment this map was built from (0 for a
  // total-fallback map). Echoed back to the sharding service so it can skip
  // resending an already-acknowledged assignment after a stream failure.
  int64_t generation() const { return generation_; }

  // All endpoints known to this map (assigned endpoints first, then any
  // resolver endpoints not named by the assignment). This is also the global
  // fallback pool.
  const std::vector<RefCountedPtr<EndpointState>>& all_endpoints() const {
    return all_endpoints_;
  }

  // The slices, sorted by start_key. Exposed primarily for testing.
  const std::vector<SliceEntry>& slices() const { return slices_; }

 private:
  std::vector<RefCountedPtr<EndpointState>> all_endpoints_;
  std::vector<SliceEntry> slices_;
  int64_t generation_ = 0;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LOAD_BALANCING_SLICER_SLICE_MAP_H
