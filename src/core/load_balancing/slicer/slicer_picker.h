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

// Data-plane picker for slicer LB policy.
class SlicerPicker final : public LoadBalancingPolicy::SubchannelPicker {
 public:
  using PickArgs = LoadBalancingPolicy::PickArgs;
  using PickResult = LoadBalancingPolicy::PickResult;

  SlicerPicker(RefCountedPtr<SliceMap> slice_map, std::string slice_key_header,
               bool fallback_enabled);

  PickResult Pick(PickArgs args) override;

 private:
  PickResult PickFromSlice(const SliceEntry& slice, PickArgs args);
  PickResult PickFromFallbackPool(PickArgs args);
  PickResult DelegateToRandom(const std::vector<size_t>& indices,
                              PickArgs args);
  PickResult Delegate(const EndpointState& endpoint, PickArgs args);

  RefCountedPtr<SliceMap> slice_map_;
  std::string slice_key_header_;
  bool fallback_enabled_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LOAD_BALANCING_SLICER_SLICER_PICKER_H
