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

#include "proto/autosharding/v1/autosharding.upb.h"
#include "src/core/util/upb_utils.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
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
  // Validate slice bounds and negative indices upfront so invalid slices are
  // not buffered; Build() sorts slices and checks endpoint bounds/overlaps.
  size_t num_slices;
  const google_cloud_autosharding_v1_SliceAssignment* const* slice_assignments =
      google_cloud_autosharding_v1_AssignmentChunk_slice_assignments(
          chunk, &num_slices);
  num_slice_assignments_ += num_slices;
  slices_.reserve(slices_.size() + num_slices);
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
      const int32_t index =
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
  size_t num_endpoints;
  const google_cloud_autosharding_v1_EndpointState* const* endpoints =
      google_cloud_autosharding_v1_AssignmentChunk_endpoints(chunk,
                                                             &num_endpoints);
  endpoint_names_.reserve(endpoint_names_.size() + num_endpoints);
  for (size_t i = 0; i < num_endpoints; ++i) {
    endpoint_names_.push_back(UpbStringToStdString(
        google_cloud_autosharding_v1_EndpointState_endpoint(endpoints[i])));
  }
}

std::string AutoshardingAssignmentBuilder::IssueMessages(
    const IssueCounts& counts, size_t num_gaps) {
  std::vector<std::string> issues;
  if (counts.long_start_key > 0) {
    issues.push_back(absl::StrCat(counts.long_start_key,
                                  " slice(s) with start_key longer than ",
                                  kMaxKeyLength, " bytes"));
  }
  if (counts.long_end_key > 0) {
    issues.push_back(absl::StrCat(counts.long_end_key,
                                  " slice(s) with end_key longer than ",
                                  kMaxKeyLength, " bytes"));
  }
  if (counts.start_key_not_less_than_end_key > 0) {
    issues.push_back(absl::StrCat(counts.start_key_not_less_than_end_key,
                                  " slice(s) with start_key >= end_key"));
  }
  if (counts.invalid_endpoint_index > 0) {
    issues.push_back(absl::StrCat(counts.invalid_endpoint_index,
                                  " slice(s) with invalid endpoint indices"));
  }
  if (counts.overlapping > 0) {
    issues.push_back(absl::StrCat(counts.overlapping, " overlapping slices"));
  }
  if (num_gaps > 0) {
    issues.push_back(
        absl::StrCat("encountered ", num_gaps, " gap(s) in the assignment"));
  }
  return absl::StrJoin(issues, "; ");
}

absl::StatusOr<AutoshardingAssignmentBuilder::Result>
AutoshardingAssignmentBuilder::Build(int64_t generation,
                                     int64_t last_accepted_generation) {
  const size_t num_chunks = std::exchange(num_chunks_, 0);
  const size_t num_slice_assignments = std::exchange(num_slice_assignments_, 0);
  std::vector<PendingSlice> slices = std::exchange(slices_, {});
  std::vector<std::string> endpoint_names = std::exchange(endpoint_names_, {});
  IssueCounts counts = std::exchange(issue_counts_, {});
  if (generation <= last_accepted_generation) {
    return absl::InvalidArgumentError(
        absl::StrCat("received generation ", generation,
                     " is not greater than the latest generation ",
                     last_accepted_generation));
  }
  if (num_chunks == 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("received metadata with generation ", generation,
                     ", but no chunks were received"));
  }
  std::sort(slices.begin(), slices.end(),
            [](const PendingSlice& a, const PendingSlice& b) {
              return a.start_key < b.start_key;
            });
  // Hold each valid slice as candidate until the next slice confirms no
  // overlap, so both sides of an overlapping pair are dropped.
  auto assignment = MakeRefCounted<AutoshardingAssignment>();
  assignment->slices.reserve(slices.size());
  size_t num_gaps = 0;
  PendingSlice* candidate = nullptr;
  auto append_candidate = [&]() {
    // The first slice implicitly starts at -infinity; compare before
    // push_back() can reallocate assignment->slices.
    if (assignment->slices.empty()
            ? !candidate->start_key.empty()
            : candidate->start_key > assignment->slices.back().end_key) {
      assignment->slices.push_back(
          {std::move(candidate->start_key), {}, false});
      ++num_gaps;
    }
    const bool fallback_disabled = candidate->endpoints.empty();
    assignment->slices.push_back(
        {std::move(candidate->end_key).value_or(std::string()),
         std::move(candidate->endpoints), fallback_disabled});
  };
  // Largest end_key seen so far.  An empty view represents -infinity (the
  // state before seeing any slice); std::nullopt represents +infinity.
  std::optional<absl::string_view> max_end_key = absl::string_view();
  for (PendingSlice& slice : slices) {
    if (std::any_of(
            slice.endpoints.begin(), slice.endpoints.end(),
            [&](size_t index) { return index >= endpoint_names.size(); })) {
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
      max_end_key = slice.end_key;
      if (candidate != nullptr) {
        append_candidate();
      }
      candidate = &slice;
    }
  }
  if (candidate != nullptr) {
    append_candidate();
  }
  if (assignment->slices.empty()) {
    return num_slice_assignments == 0
               ? absl::InvalidArgumentError(absl::StrCat(
                     "no valid slice assignments in generation ", generation,
                     ": no slice assignments received"))
               : absl::InvalidArgumentError(absl::StrCat(
                     "no valid slice assignments in generation ", generation,
                     ": ", IssueMessages(counts, num_gaps)));
  }
  // Fill the gap at the end of the keyspace, if any.
  if (!assignment->slices.back().end_key.empty()) {
    assignment->slices.push_back({std::string(), {}, false});
    ++num_gaps;
  }
  assignment->endpoint_names = std::move(endpoint_names);
  return Result{std::move(assignment), IssueMessages(counts, num_gaps)};
}

}  // namespace grpc_core
