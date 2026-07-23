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

#ifndef GRPC_SRC_CORE_LOAD_BALANCING_SLICER_SLICER_PICKER_H
#define GRPC_SRC_CORE_LOAD_BALANCING_SLICER_SLICER_PICKER_H

#include <grpc/support/port_platform.h>
#include <stddef.h>

#include <string>
#include <vector>

#include "src/core/load_balancing/lb_policy.h"
#include "src/core/load_balancing/slicer/slice_map.h"
#include "src/core/util/ref_counted_ptr.h"

namespace grpc_core {

// The data-plane picker for the slicer LB policy. It reads the request's
// slice key from a header, looks up the matching slice in an immutable
// SliceMap, and delegates the pick to one of the slice's endpoint pickers.
// Because the SliceMap is immutable, the picker holds no locks.
class SlicerPicker final : public LoadBalancingPolicy::SubchannelPicker {
 public:
  using PickArgs = LoadBalancingPolicy::PickArgs;
  using PickResult = LoadBalancingPolicy::PickResult;

  // `slice_key_header` is the name of the request header carrying the
  // application-defined key. `fallback_enabled` mirrors the policy's
  // enable_fallback config: when true, keys with no assignment (or slices in
  // fallback mode) are served from the global fallback pool instead of failing.
  SlicerPicker(RefCountedPtr<SliceMap> slice_map, std::string slice_key_header,
               bool fallback_enabled);

  PickResult Pick(PickArgs args) override;

 private:
  // Selects an endpoint from a non-fallback slice, applying the state-based
  // preference (READY > wake IDLE / queue > fail via TRANSIENT_FAILURE picker).
  PickResult PickFromSlice(const SliceEntry& slice, PickArgs args);
  // Serves a pick from the global fallback pool (a random endpoint).
  PickResult PickFromFallbackPool(PickArgs args);
  // Delegates to a random endpoint from `indices` (indices into
  // slice_map_->all_endpoints()).
  PickResult DelegateToRandom(const std::vector<size_t>& indices, PickArgs args);
  // Delegates the pick to `endpoint`'s picker, or queues if it has none.
  PickResult Delegate(const EndpointState& endpoint, PickArgs args);

  RefCountedPtr<SliceMap> slice_map_;
  std::string slice_key_header_;
  bool fallback_enabled_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LOAD_BALANCING_SLICER_SLICER_PICKER_H
