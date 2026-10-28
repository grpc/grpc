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

#include "src/core/load_balancing/autosharding/autosharding_assignment.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/core/util/upb_utils.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

namespace {

// Maximum length of a slice key, as stated in the Slice message.
constexpr size_t kMaxKeyLength = 512;

}  // namespace

void AutoshardingAssignmentBuilder::AddChunk(
    const google_cloud_autosharding_v1_AssignmentChunk* chunk) {
  ++num_chunks_;
  // Slices.  The order across chunks does not matter, because Build()
  // sorts them.  Checks that do not need the complete endpoint list are
  // done here, so that invalid slices are not stored.
  size_t num_slices;
  const google_cloud_autosharding_v1_SliceAssignment* const* slice_assignments =
      google_cloud_autosharding_v1_AssignmentChunk_slice_assignments(
          chunk, &num_slices);
  num_slice_assignments_ += num_slices;
  for (size_t i = 0; i < num_slices; ++i) {
    // A SliceAssignment without a Slice is treated like an empty Slice,
    // which covers the whole keyspace ["", +infinity).
    PendingSlice pending;
    const google_cloud_autosharding_v1_Slice* slice =
        google_cloud_autosharding_v1_SliceAssignment_slice(
            slice_assignments[i]);
    if (slice != nullptr) {
      absl::string_view start_key =
          UpbStringToAbsl(google_cloud_autosharding_v1_Slice_start_key(slice));
      if (start_key.size() > kMaxKeyLength) {
        ++issue_counts_.long_start_key;
        continue;
      }
      if (google_cloud_autosharding_v1_Slice_has_end_key(slice)) {
        absl::string_view end_key =
            UpbStringToAbsl(google_cloud_autosharding_v1_Slice_end_key(slice));
        if (end_key.size() > kMaxKeyLength) {
          ++issue_counts_.long_end_key;
          continue;
        }
        if (start_key >= end_key) {
          ++issue_counts_.start_key_not_less_than_end_key;
          continue;
        }
        pending.end_key = std::string(end_key);
      }
      pending.start_key = std::string(start_key);
    }
    size_t num_endpoints;
    const google_cloud_autosharding_v1_PerSliceEndpointState* const* endpoints =
        google_cloud_autosharding_v1_SliceAssignment_endpoints(
            slice_assignments[i], &num_endpoints);
    pending.endpoints.reserve(num_endpoints);
    for (size_t j = 0; j < num_endpoints; ++j) {
      int32_t index =
          google_cloud_autosharding_v1_PerSliceEndpointState_endpoint_index(
              endpoints[j]);
      if (index < 0) break;
      pending.endpoints.push_back(static_cast<size_t>(index));
    }
    if (pending.endpoints.size() != num_endpoints) {
      ++issue_counts_.invalid_endpoint_index;
      continue;
    }
    slices_.push_back(std::move(pending));
  }
  // Endpoint names.  These must be concatenated in chunk order, because
  // the slices refer to them by index.
  size_t num_endpoints;
  const google_cloud_autosharding_v1_EndpointState* const* endpoints =
      google_cloud_autosharding_v1_AssignmentChunk_endpoints(chunk,
                                                             &num_endpoints);
  for (size_t i = 0; i < num_endpoints; ++i) {
    endpoint_names_.push_back(UpbStringToStdString(
        google_cloud_autosharding_v1_EndpointState_endpoint(endpoints[i])));
  }
}

void AutoshardingAssignmentBuilder::Reset() {
  num_chunks_ = 0;
  num_slice_assignments_ = 0;
  slices_.clear();
  endpoint_names_.clear();
  issue_counts_ = IssueCounts();
}

std::string AutoshardingAssignmentBuilder::IssueMessages(
    const IssueCounts& counts, size_t num_gaps) {
  std::vector<std::string> messages;
  if (counts.long_start_key > 0) {
    messages.push_back(absl::StrCat(counts.long_start_key,
                                    " slice(s) with start_key longer than ",
                                    kMaxKeyLength, " bytes"));
  }
  if (counts.long_end_key > 0) {
    messages.push_back(absl::StrCat(counts.long_end_key,
                                    " slice(s) with end_key longer than ",
                                    kMaxKeyLength, " bytes"));
  }
  if (counts.start_key_not_less_than_end_key > 0) {
    messages.push_back(absl::StrCat(counts.start_key_not_less_than_end_key,
                                    " slice(s) with start_key >= end_key"));
  }
  if (counts.invalid_endpoint_index > 0) {
    messages.push_back(absl::StrCat(counts.invalid_endpoint_index,
                                    " slice(s) with invalid endpoint indices"));
  }
  if (counts.overlapping > 0) {
    messages.push_back(absl::StrCat(counts.overlapping, " overlapping slices"));
  }
  if (num_gaps > 0) {
    messages.push_back(
        absl::StrCat("encountered ", num_gaps, " gap(s) in the assignment"));
  }
  return absl::StrJoin(messages, "; ");
}

AutoshardingAssignmentBuilder::Result AutoshardingAssignmentBuilder::Build(
    int64_t generation) {
  // Take the pending chunks, so that the builder is empty for the next
  // assignment, whatever the outcome.
  const size_t num_chunks = num_chunks_;
  const size_t num_slice_assignments = num_slice_assignments_;
  std::vector<PendingSlice> slices = std::move(slices_);
  std::vector<std::string> endpoint_names = std::move(endpoint_names_);
  IssueCounts counts = issue_counts_;
  Reset();
  // Check the generation.
  if (generation <= last_accepted_generation_) {
    return {Outcome::kStale,
            absl::StrCat("received generation ", generation,
                         " is not greater than the latest generation ",
                         last_accepted_generation_),
            std::nullopt};
  }
  if (num_chunks == 0) {
    return {Outcome::kRejected,
            absl::StrCat("received metadata with generation ", generation,
                         ", but no chunks were received"),
            std::nullopt};
  }
  // Sort the slices by start_key.  The server can send them in any order.
  std::sort(slices.begin(), slices.end(),
            [](const PendingSlice& a, const PendingSlice& b) {
              return a.start_key < b.start_key;
            });
  // Walk the slices in key order.  Drop each slice with an invalid
  // endpoint index.  Drop every slice that overlaps another slice: a slice
  // overlaps an earlier slice if it starts before the largest end_key seen
  // so far, and it overlaps a later slice if the next slice starts before
  // it ends.  So a slice is appended only when the next slice is known not
  // to overlap it; until then, it is held as the candidate.
  AutoshardingAssignment assignment;
  size_t num_gaps = 0;
  PendingSlice* candidate = nullptr;
  auto append_candidate = [&]() {
    absl::string_view prev_end_key = assignment.slices.empty()
                                         ? absl::string_view()
                                         : assignment.slices.back().end_key;
    if (candidate->start_key > prev_end_key) {
      assignment.slices.push_back({std::move(candidate->start_key), {}, false});
      ++num_gaps;
    }
    // An explicitly empty endpoint list disables fallback for this slice.
    const bool fallback_disabled = candidate->endpoints.empty();
    assignment.slices.push_back(
        {std::move(candidate->end_key).value_or(std::string()),
         std::move(candidate->endpoints), fallback_disabled});
  };
  // Largest end_key seen so far. An empty string_view represents
  // -infinity (the state before seeing any slice); std::nullopt represents
  // +infinity. Points into the end_key of a slice in slices.  When that
  // slice is the candidate, append_candidate() moves its end_key out, so
  // max_end_key is reassigned right after each call.
  std::optional<absl::string_view> max_end_key = absl::string_view();
  for (PendingSlice& slice : slices) {
    bool has_invalid_endpoint = false;
    for (size_t index : slice.endpoints) {
      if (index >= endpoint_names.size()) {
        has_invalid_endpoint = true;
        break;
      }
    }
    if (has_invalid_endpoint) {
      ++counts.invalid_endpoint_index;
      continue;
    }
    if (!max_end_key.has_value() || slice.start_key < *max_end_key) {
      ++counts.overlapping;
      // While there is a candidate, the largest end_key seen so far is the
      // candidate's end_key, so this slice overlaps the candidate too.
      if (candidate != nullptr) {
        ++counts.overlapping;
        candidate = nullptr;
      }
      if (!slice.end_key.has_value()) {
        max_end_key.reset();
      } else if (max_end_key.has_value() && *slice.end_key > *max_end_key) {
        max_end_key = *slice.end_key;
      }
    } else {
      if (candidate != nullptr) {
        append_candidate();
      }
      candidate = &slice;
      max_end_key = slice.end_key;
    }
  }
  if (candidate != nullptr) {
    append_candidate();
  }
  if (assignment.slices.empty()) {
    return {Outcome::kRejected,
            absl::StrCat(
                "no valid slice assignments in generation ", generation, ": ",
                num_slice_assignments == 0 ? "no slice assignments received"
                                           : IssueMessages(counts, num_gaps)),
            std::nullopt};
  }
  // Fill the gap at the end of the keyspace, if any.
  if (!assignment.slices.back().end_key.empty()) {
    assignment.slices.push_back({std::string(), {}, false});
    ++num_gaps;
  }
  assignment.endpoint_names = std::move(endpoint_names);
  last_accepted_generation_ = generation;
  return {Outcome::kAccepted, IssueMessages(counts, num_gaps),
          std::move(assignment)};
}

}  // namespace grpc_core
