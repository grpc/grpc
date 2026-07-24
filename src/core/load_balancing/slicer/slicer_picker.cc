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
#include <grpc/support/port_platform.h>
#include <stddef.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/core/util/shared_bit_gen.h"
#include "absl/random/distributions.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

namespace {

// Returns a uniformly random index in [0, count). `count` must be > 0.
size_t RandomIndex(size_t count) {
  return absl::Uniform<size_t>(SharedBitGen(), 0, count);
}

}  // namespace

SlicerPicker::SlicerPicker(RefCountedPtr<SliceMap> slice_map,
                           std::string slice_key_header, bool fallback_enabled)
    : slice_map_(std::move(slice_map)),
      slice_key_header_(std::move(slice_key_header)),
      fallback_enabled_(fallback_enabled) {}

LoadBalancingPolicy::PickResult SlicerPicker::Pick(PickArgs args) {
  // Extract the slice key from the request header. A present-but-empty value is
  // a valid key; an absent header means the request carries no key at all.
  std::string buffer;
  std::optional<absl::string_view> key =
      args.initial_metadata->Lookup(slice_key_header_, &buffer);
  // Without a key we cannot route the request to a slice. Rather than silently
  // treating it as some slice's key, serve it from the fallback pool if enabled
  // and otherwise fail the pick.
  if (!key.has_value()) {
    if (fallback_enabled_) return PickFromFallbackPool(args);
    return PickResult::Fail(absl::UnavailableError(absl::StrCat(
        "slicer: request has no \"", slice_key_header_, "\" header")));
  }
  const SliceEntry* slice = slice_map_->Lookup(*key);
  // No assignment covers this key (only happens before any good assignment has
  // been received from the sharding service).
  if (slice == nullptr) {
    if (fallback_enabled_) return PickFromFallbackPool(args);
    return PickResult::Fail(
        absl::UnavailableError("slicer: no slice assignment for request key"));
  }
  // The matching slice is in fallback mode.
  if (slice->in_fallback && fallback_enabled_)
    return PickFromFallbackPool(args);
  return PickFromSlice(*slice, args);
}

LoadBalancingPolicy::PickResult SlicerPicker::PickFromSlice(
    const SliceEntry& slice, PickArgs args) {
  const auto& all = slice_map_->all_endpoints();
  const std::vector<size_t>& flat = slice.all_endpoints_in_slice;
  // Queue the pick when the slice has no endpoints. This happens when the name
  // resolver update trails the assignment.
  if (flat.empty()) return PickResult::Queue();
  const std::vector<size_t>& ready =
      slice.endpoints_by_state[GRPC_CHANNEL_READY];
  const std::vector<size_t>& idle = slice.endpoints_by_state[GRPC_CHANNEL_IDLE];
  const std::vector<size_t>& connecting =
      slice.endpoints_by_state[GRPC_CHANNEL_CONNECTING];
  // Pick a random endpoint from the slice, then branch on its state.
  EndpointState& ep = *all[flat[RandomIndex(flat.size())]];
  switch (ep.connectivity_state()) {
    case GRPC_CHANNEL_READY:
      return Delegate(ep, args);
    case GRPC_CHANNEL_IDLE:
      // Trigger a connection attempt, then delegate to a READY endpoint if one
      // exists, else queue.
      ep.ExitIdle();
      if (!ready.empty()) return DelegateToRandom(ready, args);
      return PickResult::Queue();
    default:
      break;  // CONNECTING or TRANSIENT_FAILURE.
  }
  // The randomly selected endpoint is CONNECTING or TRANSIENT_FAILURE. Wake up
  // one not-yet-triggered IDLE endpoint, if any.
  for (size_t index : idle) {
    if (all[index]->ExitIdle()) break;
  }
  // Prefer a READY endpoint if one exists.
  if (!ready.empty()) return DelegateToRandom(ready, args);
  // Queue if anything is (or is now) making progress toward READY.
  bool idle_in_progress = false;
  for (size_t index : idle) {
    if (all[index]->connect_triggered()) {
      idle_in_progress = true;
      break;
    }
  }
  if (!connecting.empty() || idle_in_progress) return PickResult::Queue();
  // Nothing is connecting; delegate to the TRANSIENT_FAILURE endpoint's picker
  // so the RPC fails with the underlying connection status.
  return Delegate(ep, args);
}

LoadBalancingPolicy::PickResult SlicerPicker::PickFromFallbackPool(
    PickArgs args) {
  const auto& all = slice_map_->all_endpoints();
  // Queue if there are no endpoints at all.
  if (all.empty()) return PickResult::Queue();
  return Delegate(*all[RandomIndex(all.size())], args);
}

LoadBalancingPolicy::PickResult SlicerPicker::DelegateToRandom(
    const std::vector<size_t>& indices, PickArgs args) {
  const auto& all = slice_map_->all_endpoints();
  return Delegate(*all[indices[RandomIndex(indices.size())]], args);
}

LoadBalancingPolicy::PickResult SlicerPicker::Delegate(
    const EndpointState& endpoint, PickArgs args) {
  if (endpoint.connectivity_state() == GRPC_CHANNEL_IDLE) {
    endpoint.ExitIdle();
  }
  if (endpoint.picker() == nullptr) return PickResult::Queue();
  return endpoint.picker()->Pick(args);
}

}  // namespace grpc_core
