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

namespace grpc_core {

// A complete, validated assignment received from the autosharding server.
//
// The slices partition the keyspace [-infinity, +infinity) contiguously,
// without gaps or overlaps, in ascending key order.  The first slice
// implicitly starts at -infinity (the empty key); each subsequent slice
// implicitly starts at the previous slice's end_key.
struct AutoshardingAssignment {
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
  // Complete list of endpoint names, concatenated across chunks in chunk
  // order.
  std::vector<std::string> endpoint_names;
};

// Collects the AssignmentChunk messages of one logical assignment, and
// then builds and validates the assignment when the AssignmentMetadata
// message arrives (gRFC A119).
//
// Not thread-safe.
class AutoshardingAssignmentBuilder final {
 public:
  enum class Outcome {
    // The generation is not newer than the last accepted generation.
    // Send a NACK.  Do not tell the LB policy.
    kStale,
    // The assignment has no usable slice.  Send a NACK.  Report an error
    // to the LB policy if no valid assignment was reported before.
    kRejected,
    // The assignment is usable.  Send an ACK, and pass the assignment to
    // the LB policy.
    kAccepted,
  };

  struct Result {
    Outcome outcome;
    // Value for AssignmentAck.error_message.  Empty if there was no issue.
    // The message holds only counts, so it is always shorter than the
    // 512-character limit of that field.
    std::string error_message;
    // Set only if outcome is kAccepted.
    std::optional<AutoshardingAssignment> assignment;
  };

  // Copies the contents of chunk.  The caller can free chunk after the
  // call returns.
  void AddChunk(const google_cloud_autosharding_v1_AssignmentChunk* chunk);

  // Builds the assignment from the chunks added since the last call to
  // Build() or Reset(), and then clears those chunks.
  // generation is the value from the AssignmentMetadata message.
  Result Build(int64_t generation);

  // Discards all chunks added since the last call to Build().  Call this
  // when the stream restarts in the middle of an assignment.
  void Reset();

  // Generation of the last accepted assignment, or 0 if no assignment was
  // accepted yet.  An accepted generation is always greater than 0, because
  // Build() rejects any generation that is not greater than this value.
  int64_t last_accepted_generation() const { return last_accepted_generation_; }

 private:
  struct PendingSlice {
    std::string start_key;
    // Not set for +infinity.
    std::optional<std::string> end_key;
    std::vector<size_t> endpoints;
  };

  // Number of slices dropped for each kind of issue.
  struct IssueCounts {
    size_t long_start_key = 0;
    size_t long_end_key = 0;
    size_t start_key_not_less_than_end_key = 0;
    size_t invalid_endpoint_index = 0;
    size_t overlapping = 0;
  };

  // Formats the issues as a semicolon-separated string.
  static std::string IssueMessages(const IssueCounts& counts, size_t num_gaps);

  int64_t last_accepted_generation_ = 0;
  size_t num_chunks_ = 0;
  size_t num_slice_assignments_ = 0;
  // Slices that passed the checks that do not need the complete endpoint
  // list.
  std::vector<PendingSlice> slices_;
  std::vector<std::string> endpoint_names_;
  IssueCounts issue_counts_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LOAD_BALANCING_AUTOSHARDING_AUTOSHARDING_ASSIGNMENT_H
