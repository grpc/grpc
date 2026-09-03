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

#include "src/core/load_balancing/slicer/slicer_picker.h"

#include <grpc/impl/connectivity_state.h>

#include "src/core/util/grpc_check.h"
#include "src/core/util/shared_bit_gen.h"
#include "absl/random/distributions.h"

namespace grpc_core {

namespace {

// Random index in [0, count).
size_t RandomIndex(size_t count) {
  GRPC_DCHECK_GT(count, 0);
  return absl::Uniform<size_t>(SharedBitGen(), 0, count);
}

LoadBalancingPolicy::PickResult Delegate(const EndpointState& endpoint,
                                         LoadBalancingPolicy::PickArgs args) {
  if (endpoint.connectivity_state() == GRPC_CHANNEL_IDLE) {
    endpoint.ExitIdle();
  }
  if (endpoint.picker() == nullptr)
    return LoadBalancingPolicy::PickResult::Queue();
  return endpoint.picker()->Pick(args);
}

LoadBalancingPolicy::PickResult DelegateToRandom(
    const std::vector<RefCountedPtr<EndpointState>>& all,
    const std::vector<size_t>& indices, LoadBalancingPolicy::PickArgs args) {
  if (indices.empty()) return LoadBalancingPolicy::PickResult::Queue();
  return Delegate(*all[indices[RandomIndex(indices.size())]], args);
}

}  // namespace

SlicerPicker::SlicerPicker(RefCountedPtr<SliceMap> slice_map,
                           std::string slice_key_header, bool fallback_enabled)
    : slice_map_(std::move(slice_map)),
      slice_key_header_(std::move(slice_key_header)),
      fallback_enabled_(fallback_enabled) {}

LoadBalancingPolicy::PickResult SlicerPicker::Pick(PickArgs args) {
  // Extract slice key from request header.
  std::string buffer;
  std::optional<absl::string_view> key =
      args.initial_metadata->Lookup(slice_key_header_, &buffer);
  if (!key.has_value()) {
    if (fallback_enabled_) return PickFromFallbackPool(args);
    return PickResult::Fail(absl::UnavailableError(absl::StrCat(
        "slicer: request has no \"", slice_key_header_, "\" header")));
  }
  const SliceMap::SliceEntry* slice = slice_map_->Lookup(*key);
  // Unmapped key range.
  if (slice == nullptr) {
    if (fallback_enabled_) return PickFromFallbackPool(args);
    return PickResult::Fail(
        absl::UnavailableError("slicer: no slice assignment for request key"));
  }
  // Slice in fallback mode.
  if (slice->in_fallback && fallback_enabled_) {
    return PickFromFallbackPool(args);
  }
  return PickFromSlice(*slice, args);
}

LoadBalancingPolicy::PickResult SlicerPicker::PickFromSlice(
    const SliceMap::SliceEntry& slice, PickArgs args) {
  const auto& all = slice_map_->all_endpoints();
  const std::vector<size_t>& flat = slice.all_endpoints_in_slice;
  // Queue if slice has no endpoints.
  if (flat.empty()) return PickResult::Queue();
  const std::vector<size_t>& ready =
      slice.endpoints_by_state[GRPC_CHANNEL_READY];
  const std::vector<size_t>& idle = slice.endpoints_by_state[GRPC_CHANNEL_IDLE];
  const std::vector<size_t>& connecting =
      slice.endpoints_by_state[GRPC_CHANNEL_CONNECTING];
  // Select random endpoint in slice.
  EndpointState& ep = *all[flat[RandomIndex(flat.size())]];
  switch (ep.connectivity_state()) {
    case GRPC_CHANNEL_READY:
      return Delegate(ep, args);
    case GRPC_CHANNEL_IDLE:
      // Exit IDLE and delegate to READY endpoint if available, else queue.
      ep.ExitIdle();
      if (!ready.empty()) return DelegateToRandom(all, ready, args);
      return PickResult::Queue();
    default:
      break;  // CONNECTING or TRANSIENT_FAILURE.
  }
  // Wake up one IDLE endpoint if any.
  for (size_t index : idle) {
    if (all[index]->ExitIdle()) break;
  }
  // Delegate to READY endpoint if available.
  if (!ready.empty()) return DelegateToRandom(all, ready, args);
  // Queue if connection in progress.
  bool idle_in_progress = false;
  for (size_t index : idle) {
    if (all[index]->connect_triggered()) {
      idle_in_progress = true;
      break;
    }
  }
  if (!connecting.empty() || idle_in_progress) return PickResult::Queue();
  // Delegate to TRANSIENT_FAILURE endpoint's picker.
  return Delegate(ep, args);
}

LoadBalancingPolicy::PickResult SlicerPicker::PickFromFallbackPool(
    PickArgs args) {
  const auto& all = slice_map_->all_endpoints();
  // Queue if no endpoints in fallback pool.
  if (all.empty()) return PickResult::Queue();
  return Delegate(*all[RandomIndex(all.size())], args);
}

}  // namespace grpc_core
