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

#include "src/core/load_balancing/slicer/slice_map.h"

#include <grpc/support/port_platform.h>

#include <algorithm>
#include <cstddef>
#include <utility>

#include "absl/strings/str_cat.h"
#include "src/core/util/ref_counted_ptr.h"

namespace grpc_core {

absl::StatusOr<RefCountedPtr<SliceMap>> SliceMap::Make(
    const EndpointMap& endpoint_map, const LogicalAssignment* assignment) {
  auto slice_map = MakeRefCounted<SliceMap>();
  // No assignment yet: build a "total fallback" map containing every endpoint
  // and no slices. Every pick will use the fallback pool (all_endpoints_).
  if (assignment == nullptr || assignment->slices.empty()) {
    slice_map->all_endpoints_.reserve(endpoint_map.size());
    for (const auto& [name, state] : endpoint_map) {
      slice_map->all_endpoints_.push_back(state);
    }
    return slice_map;
  }
  slice_map->generation_ = assignment->generation;
  // Assign a stable index to each endpoint, with the assignment's endpoints
  // first (in the order they appear) followed by any resolver endpoints not
  // named by the assignment. `index_for_name` maps hostname -> index into
  // all_endpoints_ and is local to Make(): only the resulting indices are
  // retained in the SliceMap.
  std::map<absl::string_view, size_t> index_for_name;
  auto index_of = [&](const std::string& name) -> absl::StatusOr<size_t> {
    auto it = index_for_name.find(name);
    if (it != index_for_name.end()) return it->second;
    auto ep_it = endpoint_map.find(name);
    if (ep_it == endpoint_map.end()) {
      return absl::NotFoundError(absl::StrCat(
          "endpoint \"", name, "\" named by slice assignment not found"));
    }
    size_t index = slice_map->all_endpoints_.size();
    slice_map->all_endpoints_.push_back(ep_it->second);
    index_for_name.emplace(ep_it->first, index);
    return index;
  };
  // Build the slices, indexing each assigned endpoint as we go.
  slice_map->slices_.reserve(assignment->slices.size());
  for (const auto& slice_assignment : assignment->slices) {
    SliceEntry entry;
    entry.start_key = slice_assignment.start_key;
    for (const auto& name : slice_assignment.endpoint_names) {
      auto index = index_of(name);
      if (!index.ok()) return index.status();
      grpc_connectivity_state state =
          slice_map->all_endpoints_[*index]->connectivity_state();
      entry.all_endpoints_in_slice.push_back(*index);
      entry.endpoints_by_state[state].push_back(*index);
    }
    // A slice falls back if it has no endpoints, or if all of its endpoints are
    // in TRANSIENT_FAILURE.
    entry.in_fallback =
        entry.all_endpoints_in_slice.empty() ||
        entry.endpoints_by_state[GRPC_CHANNEL_TRANSIENT_FAILURE].size() ==
            entry.all_endpoints_in_slice.size();
    slice_map->slices_.push_back(std::move(entry));
  }
  // Append any resolver endpoints not named by the assignment so they remain
  // reachable via the fallback pool.
  for (const auto& [name, state] : endpoint_map) {
    if (index_for_name.find(name) == index_for_name.end()) {
      slice_map->all_endpoints_.push_back(state);
    }
  }
  // The sharding service does not guarantee sorted slices; sort by start_key so
  // Lookup() can binary-search.
  std::sort(slice_map->slices_.begin(), slice_map->slices_.end(),
            [](const SliceEntry& a, const SliceEntry& b) {
              return a.start_key < b.start_key;
            });
  return slice_map;
}

const SliceEntry* SliceMap::Lookup(absl::string_view key) const {
  if (slices_.empty()) return nullptr;
  // Find the first slice whose start_key is strictly greater than `key`; the
  // slice immediately before it is the one whose range contains `key`.
  auto it = std::upper_bound(
      slices_.begin(), slices_.end(), key,
      [](absl::string_view k, const SliceEntry& e) { return k < e.start_key; });
  if (it == slices_.begin()) return nullptr;
  return &*(it - 1);
}

}  // namespace grpc_core
