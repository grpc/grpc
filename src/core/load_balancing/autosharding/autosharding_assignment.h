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

#ifndef GRPC_SRC_CORE_LOAD_BALANCING_AUTOSHARDING_AUTOSHARDING_ASSIGNMENT_H
#define GRPC_SRC_CORE_LOAD_BALANCING_AUTOSHARDING_AUTOSHARDING_ASSIGNMENT_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "proto/autosharding/v1/autosharding.upb.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "absl/status/statusor.h"

namespace grpc_core {

// A complete, validated assignment received from the autosharding server.
//
// The slices partition the keyspace [-infinity, +infinity) contiguously,
// without gaps or overlaps, in ascending key order.  The first slice
// implicitly starts at -infinity (the empty key); each subsequent slice
// implicitly starts at the previous slice's end_key.
struct AutoshardingAssignment final
    : public RefCounted<AutoshardingAssignment, NonPolymorphicRefCount> {
  struct Slice {
    // Exclusive end key.  Empty for +infinity.
    std::string end_key;
    // Indices into AutoshardingAssignment::endpoint_names.
    std::vector<size_t> endpoints;
    // True if the autosharding server explicitly assigned no endpoints to
    // this slice.  False for a slice that fills a gap in the assignment.
    bool fallback_disabled = false;
  };

  std::vector<Slice> slices;
  std::vector<std::string> endpoint_names;
};

// Reassembles and validates an AutoshardingAssignment from AssignmentChunk
// messages (gRFC A119).
class AutoshardingAssignmentBuilder final {
 public:
  struct Result {
    RefCountedPtr<AutoshardingAssignment> assignment;
    // AssignmentAck.error_message when accepted with dropped slices or gaps,
    // or empty if there were no issues.
    std::string error_message;
  };

  AutoshardingAssignmentBuilder() = default;

  AutoshardingAssignmentBuilder(const AutoshardingAssignmentBuilder&) = delete;
  AutoshardingAssignmentBuilder& operator=(
      const AutoshardingAssignmentBuilder&) = delete;

  void AddChunk(const google_cloud_autosharding_v1_AssignmentChunk* chunk);

  // Builds and validates the assignment from buffered chunks, resetting the
  // builder state regardless of outcome.  Fails (with the NACK error message)
  // if generation <= last_accepted_generation or if no valid slices remain.
  // Pass last_accepted_generation = 0 if no assignment has been accepted yet.
  absl::StatusOr<Result> Build(int64_t generation,
                               int64_t last_accepted_generation);

 private:
  struct PendingSlice {
    std::string start_key;
    // Not set for +infinity.
    std::optional<std::string> end_key;
    std::vector<size_t> endpoints;
  };

  struct IssueCounts {
    size_t long_start_key = 0;
    size_t long_end_key = 0;
    size_t start_key_not_less_than_end_key = 0;
    size_t invalid_endpoint_index = 0;
    size_t overlapping = 0;
  };

  // Formats the issues as a semicolon-separated string.
  static std::string IssueMessages(const IssueCounts& counts, size_t num_gaps);

  size_t num_chunks_ = 0;
  size_t num_slice_assignments_ = 0;
  std::vector<PendingSlice> slices_;
  std::vector<std::string> endpoint_names_;
  IssueCounts issue_counts_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LOAD_BALANCING_AUTOSHARDING_AUTOSHARDING_ASSIGNMENT_H
